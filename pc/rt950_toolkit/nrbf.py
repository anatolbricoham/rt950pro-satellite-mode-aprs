"""
nrbf.py - Minimal .NET Remoting Binary Format ([MS-NRBF]) reader/writer

The maker's CPS saves codeplugs (.dat) with .NET BinaryFormatter.  This
module parses such a stream into a list of records and can write the very
same records back (byte-identical round trip), so values can be edited in
place without understanding every class.

Only the record types used by BinaryFormatter for plain data classes are
implemented: header, library, class records (with/without types, by id),
strings, primitives, references, nulls, single-dimension arrays.
"""

from __future__ import annotations

import io
import struct
from typing import Any, Dict, List, Optional

# primitive type ids
P_BOOL, P_BYTE, P_CHAR = 1, 2, 3          # 4 is unused in [MS-NRBF]
P_DECIMAL, P_DOUBLE, P_I16, P_I32, P_I64, P_SBYTE, P_SINGLE, \
    P_TIMESPAN, P_DATETIME, P_U16, P_U32, P_U64, P_NULL, P_STRING = range(5, 19)

_PFMT = {P_BOOL: "<?", P_BYTE: "<B", P_DOUBLE: "<d", P_I16: "<h", P_I32: "<i", P_I64: "<q",
         P_SBYTE: "<b", P_SINGLE: "<f", P_TIMESPAN: "<q", P_DATETIME: "<Q", P_U16: "<H",
         P_U32: "<I", P_U64: "<Q"}

# binary type ids (member type info)
B_PRIMITIVE, B_STRING, B_OBJECT, B_SYSTEMCLASS, B_CLASS, B_OBJECTARRAY, B_STRINGARRAY, B_PRIMITIVEARRAY = range(8)


class R:
    """Reader helpers."""

    def __init__(self, data: bytes):
        self.s = io.BytesIO(data)

    def u8(self):
        return self.s.read(1)[0]

    def i32(self):
        return struct.unpack("<i", self.s.read(4))[0]

    def lps(self) -> str:
        n, shift = 0, 0
        while True:
            b = self.u8()
            n |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                break
        return self.s.read(n).decode("utf-8")

    def prim(self, t):
        if t == P_STRING:
            return self.lps()
        if t == P_CHAR:
            # UTF-8 char: read 1..4 bytes
            b = self.s.read(1)
            n = 1 if b[0] < 0x80 else 2 if b[0] < 0xE0 else 3 if b[0] < 0xF0 else 4
            return (b + self.s.read(n - 1)).decode("utf-8")
        if t == P_DECIMAL:
            return self.lps()
        f = _PFMT[t]
        return struct.unpack(f, self.s.read(struct.calcsize(f)))[0]


def _w_lps(out: bytearray, s: str):
    b = s.encode("utf-8")
    n = len(b)
    while True:
        x = n & 0x7F
        n >>= 7
        out.append(x | (0x80 if n else 0))
        if not n:
            break
    out += b


def _w_prim(out: bytearray, t, v):
    if t in (P_STRING, P_DECIMAL):
        _w_lps(out, v)
    elif t == P_CHAR:
        out += v.encode("utf-8")
    else:
        out += struct.pack(_PFMT[t], v)


class Stream:
    """Parsed NRBF stream: `records` (in order) and `objects` by id."""

    def __init__(self, data: bytes):
        self.records: List[dict] = []
        self.objects: Dict[int, dict] = {}
        self.classes: Dict[int, dict] = {}       # object id -> class record (metadata)
        r = R(data)
        while True:
            rec = self._record(r)
            self.records.append(rec)
            if rec["rt"] == 11:
                break

    # ---------------------------------------------------------- parsing
    def _member_info(self, r: R, n: int):
        btypes = [r.u8() for _ in range(n)]
        extra = []
        for bt in btypes:
            if bt in (B_PRIMITIVE, B_PRIMITIVEARRAY):
                extra.append(r.u8())
            elif bt == B_SYSTEMCLASS:
                extra.append(r.lps())
            elif bt == B_CLASS:
                extra.append((r.lps(), r.i32()))
            else:
                extra.append(None)
        return btypes, extra

    def _value(self, r: R, bt, extra):
        if bt == B_PRIMITIVE:
            return {"prim": extra, "v": r.prim(extra)}
        return self._record(r)                    # nested record

    def _class_values(self, r: R, meta: dict) -> list:
        vals = []
        names = meta["names"]
        i = 0
        while i < len(names):
            bt, ex = (meta["btypes"][i], meta["extra"][i]) if meta.get("btypes") else (B_OBJECT, None)
            v = self._value(r, bt, ex)
            vals.append(v)
            if isinstance(v, dict) and v.get("rt") in (13, 14):   # null multiple
                for _ in range(v["count"] - 1):
                    vals.append({"rt": "nullfill"})
                i += v["count"]
                continue
            i += 1
        return vals

    def _record(self, r: R) -> dict:
        rt = r.u8()
        rec: Dict[str, Any] = {"rt": rt}
        if rt == 0:            # SerializedStreamHeader
            rec["raw"] = r.s.read(16)
        elif rt == 12:         # BinaryLibrary
            rec["id"] = r.i32()
            rec["name"] = r.lps()
        elif rt in (4, 5, 3, 2):
            rec["id"] = r.i32()
            rec["cls"] = r.lps()
            n = r.i32()
            rec["names"] = [r.lps() for _ in range(n)]
            if rt in (4, 5):
                rec["btypes"], rec["extra"] = self._member_info(r, n)
            if rt in (5, 3):
                rec["lib"] = r.i32()
            self.classes[rec["id"]] = rec
            self.objects[rec["id"]] = rec
            rec["values"] = self._class_values(r, rec)
        elif rt == 1:          # ClassWithId (metadata reused)
            rec["id"] = r.i32()
            rec["meta"] = r.i32()
            meta = self.classes[rec["meta"]]
            self.classes[rec["id"]] = meta
            self.objects[rec["id"]] = rec
            rec["values"] = self._class_values(r, meta)
        elif rt == 6:          # BinaryObjectString
            rec["id"] = r.i32()
            rec["v"] = r.lps()
            self.objects[rec["id"]] = rec
        elif rt == 7:          # BinaryArray
            rec["id"] = r.i32()
            rec["atype"] = r.u8()
            rank = r.i32()
            rec["lengths"] = [r.i32() for _ in range(rank)]
            if rec["atype"] in (3, 4, 5):
                rec["lower"] = [r.i32() for _ in range(rank)]
            bt = r.u8()
            rec["btype"] = bt
            if bt in (B_PRIMITIVE, B_PRIMITIVEARRAY):
                rec["bextra"] = r.u8()
            elif bt == B_SYSTEMCLASS:
                rec["bextra"] = r.lps()
            elif bt == B_CLASS:
                rec["bextra"] = (r.lps(), r.i32())
            else:
                rec["bextra"] = None
            total = 1
            for x in rec["lengths"]:
                total *= x
            rec["items"] = self._items(r, total, bt, rec["bextra"])
            self.objects[rec["id"]] = rec
        elif rt == 15:         # ArraySinglePrimitive
            rec["id"] = r.i32()
            n = r.i32()
            rec["prim"] = r.u8()
            if rec["prim"] == P_BYTE:
                rec["items"] = r.s.read(n)
            else:
                rec["items"] = [r.prim(rec["prim"]) for _ in range(n)]
            self.objects[rec["id"]] = rec
        elif rt in (16, 17):   # ArraySingleObject / ArraySingleString
            rec["id"] = r.i32()
            n = r.i32()
            rec["items"] = self._items(r, n, B_OBJECT, None)
            self.objects[rec["id"]] = rec
        elif rt == 8:          # MemberPrimitiveTyped
            rec["prim"] = r.u8()
            rec["v"] = r.prim(rec["prim"])
        elif rt == 9:          # MemberReference
            rec["ref"] = r.i32()
        elif rt == 10:         # ObjectNull
            pass
        elif rt == 13:
            rec["count"] = r.u8()
        elif rt == 14:
            rec["count"] = r.i32()
        elif rt == 11:
            pass
        else:
            raise ValueError("unsupported NRBF record type %d" % rt)
        return rec

    def _items(self, r: R, n: int, bt, extra):
        items = []
        while len(items) < n:
            if bt == B_PRIMITIVE:
                items.append({"prim": extra, "v": r.prim(extra)})
                continue
            v = self._record(r)
            items.append(v)
            if v["rt"] in (13, 14):
                items.extend({"rt": "nullfill"} for _ in range(v["count"] - 1))
        return items

    # ---------------------------------------------------------- writing
    def dump(self) -> bytes:
        out = bytearray()
        for rec in self.records:
            self._w(out, rec)
        return bytes(out)

    def _w(self, out: bytearray, rec: dict):
        rt = rec["rt"]
        if rt == "nullfill":
            return
        out.append(rt)
        i32 = lambda v: out.extend(struct.pack("<i", v))
        if rt == 0:
            out += rec["raw"]
        elif rt == 12:
            i32(rec["id"]); _w_lps(out, rec["name"])
        elif rt in (2, 3, 4, 5):
            i32(rec["id"]); _w_lps(out, rec["cls"]); i32(len(rec["names"]))
            for nme in rec["names"]:
                _w_lps(out, nme)
            if rt in (4, 5):
                out += bytes(rec["btypes"])
                for bt, ex in zip(rec["btypes"], rec["extra"]):
                    if bt in (B_PRIMITIVE, B_PRIMITIVEARRAY):
                        out.append(ex)
                    elif bt == B_SYSTEMCLASS:
                        _w_lps(out, ex)
                    elif bt == B_CLASS:
                        _w_lps(out, ex[0]); i32(ex[1])
            if rt in (3, 5):
                i32(rec["lib"])
            self._w_values(out, rec["values"])
        elif rt == 1:
            i32(rec["id"]); i32(rec["meta"])
            self._w_values(out, rec["values"])
        elif rt == 6:
            i32(rec["id"]); _w_lps(out, rec["v"])
        elif rt == 7:
            i32(rec["id"]); out.append(rec["atype"]); i32(len(rec["lengths"]))
            for x in rec["lengths"]:
                i32(x)
            for x in rec.get("lower", []):
                i32(x)
            out.append(rec["btype"])
            bt, ex = rec["btype"], rec["bextra"]
            if bt in (B_PRIMITIVE, B_PRIMITIVEARRAY):
                out.append(ex)
            elif bt == B_SYSTEMCLASS:
                _w_lps(out, ex)
            elif bt == B_CLASS:
                _w_lps(out, ex[0]); i32(ex[1])
            self._w_values(out, rec["items"])
        elif rt == 15:
            i32(rec["id"]); i32(len(rec["items"])); out.append(rec["prim"])
            if rec["prim"] == P_BYTE:
                out += bytes(rec["items"])
            else:
                for v in rec["items"]:
                    _w_prim(out, rec["prim"], v)
        elif rt in (16, 17):
            i32(rec["id"]); i32(len(rec["items"]))
            self._w_values(out, rec["items"])
        elif rt == 8:
            out.append(rec["prim"]); _w_prim(out, rec["prim"], rec["v"])
        elif rt == 9:
            i32(rec["ref"])
        elif rt == 13:
            out.append(rec["count"])
        elif rt == 14:
            i32(rec["count"])

    def _w_values(self, out, vals):
        for v in vals:
            if "prim" in v and "rt" not in v:
                _w_prim(out, v["prim"], v["v"])
            else:
                self._w(out, v)

    # ---------------------------------------------------------- access
    def deref(self, v):
        if isinstance(v, dict) and v.get("rt") == 9:
            return self.objects.get(v["ref"])
        return v

    def get(self, obj: dict, name: str):
        meta = self.classes[obj["id"]]
        v = obj["values"][meta["names"].index(name)]
        return self.value(v)

    def value(self, v):
        v = self.deref(v)
        if v is None:
            return None
        if "prim" in v and "rt" not in v:
            return v["v"]
        if v.get("rt") == 6:
            return v["v"]
        if v.get("rt") in (10, 13, 14, "nullfill"):
            return None
        return v

    def set(self, obj: dict, name: str, value):
        """Set a primitive or string member.  Strings get a fresh inline
        BinaryObjectString (the CPS shares identical strings by reference,
        so editing the shared object would change every channel)."""
        meta = self.classes[obj["id"]]
        i = meta["names"].index(name)
        v = obj["values"][i]
        if isinstance(v, dict) and "prim" in v and "rt" not in v:
            v["v"] = value
            return
        btype = meta["btypes"][i] if meta.get("btypes") else B_OBJECT
        tgt = self.deref(v)
        is_str = btype == B_STRING or (isinstance(tgt, dict) and tgt.get("rt") == 6) or \
            (isinstance(v, dict) and v.get("rt") == 10 and btype in (B_STRING, B_OBJECT))
        if not is_str:
            raise TypeError("member %s is not a primitive/string" % name)
        new_id = max(self.objects) + 1
        rec = {"rt": 6, "id": new_id, "v": str(value)}
        self.objects[new_id] = rec
        obj["values"][i] = rec

    def find_class(self, cls_suffix: str) -> List[dict]:
        out = []
        for o in self.objects.values():
            if o.get("rt") in (1, 2, 3, 4, 5) and self.classes[o["id"]]["cls"].endswith(cls_suffix):
                out.append(o)
        return out

    def members(self, obj: dict) -> List[str]:
        return list(self.classes[obj["id"]]["names"])
