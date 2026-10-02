"""
codeplug.py - RT-950 / RT-950 Pro codeplug model

Memory map and encodings come from two public sources that agree with each
other:
  * the radio maker's own model definition (data/rt950pro_schema.json, the
    "findSettingByModel" document the OEM app/CPS downloads), which lists
    every setting with address, bit position and option names;
  * the reverse-engineering notes of the open firmware project
    (include/drivers/flash_layout.h).

Channel, tone and text encodings, the transfer plan (block list) and the
APRS page access were cross-checked against RT-950/950Pro Editor (MIT,
KK4OXN), which is verified on real radios.

Only the fields the user edits are changed; every other byte read from the
radio is written back untouched.
"""

from __future__ import annotations

import base64
import json
import os
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA_PATH = os.path.join(HERE, "data", "rt950pro_schema.json")

# name, field base address, size -- the transfer plan of the stock CPS.
# Field addresses follow the maker's model file; the APRS page is addressed
# there as 0xFFFF.. but transferred with the 'T'/'X' commands at offset 0.
REGIONS: List[Tuple[str, int, int]] = [
    ("channels",   0x0000, 0x7C00),   # 990 x 32 bytes (+64 padding)
    ("vfo",        0x8000, 0x80),
    ("settings",   0x9000, 0x80),
    ("dtmf",       0xA000, 0x180),
    ("modulation", 0xB000, 0x100),    # FM / AM / SSB memories
    ("zones",      0xC000, 0x100),
    ("mod_names",  0xD000, 0x300),
    ("aprs",       0xFFFF, 0x80),
]
# (read cmd, write cmd, transfer address) per region
TRANSFER = {n: (0x52, 0x57, a) for n, a, s in REGIONS}
TRANSFER["aprs"] = (0x54, 0x58, 0x0000)
CHANNELS = 990
ZONES = 10
CH_PER_ZONE = 99
ZONE_NAME_ADDR = 0xC000
ZONE_NAME_STRIDE = 16

POWER = ["High", "Mid", "Low"]
SCRAMBLE = ["OFF"] + [str(i) for i in range(1, 9)]
TEXT_ENCODING = "gbk"              # the radio uses GB2312/GBK (code page 936)
BANDWIDTH = ["Wide", "Narrow"]
PTT_ID = ["OFF", "BOT", "EOT", "BOTH"]
ENCRYPTION = ["OFF", "DCP1", "DCP2", "DCP3"]
OFF_ON = ["OFF", "ON"]


def load_schema() -> dict:
    with open(SCHEMA_PATH, encoding="utf-8") as f:
        return json.load(f)


_SCHEMA = None


def schema() -> dict:
    global _SCHEMA
    if _SCHEMA is None:
        _SCHEMA = load_schema()
    return _SCHEMA


def tone_list() -> List[str]:
    """OFF + 50 CTCSS + 105 DCS-N + 105 DCS-I (261 entries)."""
    for c in schema()["channel"]:
        if c["titleEn"] == "Rx Ctcss":
            return list(c["selectsEn"])
    raise RuntimeError("tone list missing in schema")


def dcs_codes() -> List[str]:
    return [t[1:4] for t in tone_list() if t.startswith("D") and t.endswith("N")]


def decode_tone(b0: int, b1: int) -> str:
    """2 bytes: 00 00 = off; DCS: b0 = 1..105 (N) / 106..210 (I), b1 = 0;
    CTCSS: little-endian frequency x10 (67.0 Hz -> 0x029E)."""
    if (b0, b1) in ((0, 0), (0xFF, 0xFF)):
        return "OFF"
    if b1 == 0:
        codes = dcs_codes()
        if 1 <= b0 <= len(codes):
            return "D%sN" % codes[b0 - 1]
        if len(codes) < b0 <= 2 * len(codes):
            return "D%sI" % codes[b0 - 1 - len(codes)]
        return "OFF"
    v = b0 | (b1 << 8)
    return "%.1f" % (v / 10.0)


def encode_tone(t: str) -> bytes:
    t = (t or "OFF").strip().upper()
    if t in ("", "OFF", "NONE"):
        return b"\x00\x00"
    if t.startswith("D") and len(t) == 5:
        codes = dcs_codes()
        i = codes.index(t[1:4])
        return bytes([1 + i + (len(codes) if t[4] == "I" else 0), 0])
    v = int(round(float(t) * 10))
    if not 0 < v <= 0xFFFF:
        raise ValueError("bad CTCSS tone %r" % t)
    return bytes([v & 0xFF, v >> 8])


# ----------------------------------------------------------------- encoders

def bcd_to_hz(b: bytes) -> Optional[int]:
    """4 bytes packed BCD, least significant byte first, unit 10 Hz."""
    if all(x == 0xFF for x in b) or all(x == 0x00 for x in b):
        return None
    v = 0
    for x in reversed(b):
        hi, lo = x >> 4, x & 15
        if hi > 9 or lo > 9:
            return None
        v = v * 100 + hi * 10 + lo
    return v * 10


def hz_to_bcd(hz: Optional[int]) -> bytes:
    if hz is None:
        return b"\xFF" * 4
    v = int(round(hz / 10.0))
    if not 0 <= v <= 99999999:
        raise ValueError("frequency out of range")
    out = bytearray(4)
    for i in range(4):
        two = v % 100
        v //= 100
        out[i] = ((two // 10) << 4) | (two % 10)
    return bytes(out)


def digits_to_hz(b: bytes, int_digits: int = 3) -> Optional[int]:
    """Digit-per-byte (VFO): [1,4,7,2,6,5,0,0] = 147.26500 MHz."""
    if any(x > 9 for x in b):
        return None
    s = "".join(str(x) for x in b)
    mhz = float(s[:int_digits] + "." + s[int_digits:])
    return int(round(mhz * 1e6))


def hz_to_digits(hz: int, ndigits: int, int_digits: int = 3) -> bytes:
    frac = ndigits - int_digits
    v = int(round(hz / 10 ** (6 - frac)))
    s = str(v).rjust(ndigits, "0")
    if len(s) > ndigits:
        raise ValueError("frequency out of range")
    return bytes(int(c) for c in s)


def ascii_get(b: bytes) -> str:
    """Radio text: GBK, terminated/padded with 0xFF or 0x00."""
    out = bytearray()
    for x in b:
        if x in (0xFF, 0x00):
            break
        out.append(x)
    return out.decode(TEXT_ENCODING, "replace").rstrip()


def ascii_put(s: str, n: int, pad: int = 0xFF) -> bytes:
    raw = s.encode(TEXT_ENCODING, "replace")[:n]
    try:
        raw.decode(TEXT_ENCODING)
    except UnicodeDecodeError:          # do not cut a 2-byte character
        raw = raw[:-1]
    return raw + bytes([pad]) * (n - len(raw))


# ----------------------------------------------------------------- channel

@dataclass
class Channel:
    index: int                      # 0..989 (radio shows 1..990)
    rx_hz: Optional[int] = None     # None = empty slot
    tx_hz: Optional[int] = None
    rx_tone: str = "OFF"
    tx_tone: str = "OFF"
    power: str = "High"
    bandwidth: str = "Wide"
    scramble: str = "OFF"
    busy_lock: str = "OFF"
    scan_add: str = "ON"
    encryption: str = "OFF"
    tx_enable: str = "ON"
    rx_am: str = "FM"
    signal_code: int = 1            # 1..16
    ptt_id: str = "OFF"
    name: str = ""

    @property
    def empty(self) -> bool:
        return self.rx_hz is None

    @property
    def zone(self) -> int:
        return self.index // CH_PER_ZONE

    @property
    def number(self) -> int:
        return self.index + 1

    def duplex(self) -> Tuple[str, int]:
        """('', 0) simplex, ('+'/'-', offset), ('split', tx_hz) or ('off', 0)."""
        if self.tx_enable == "OFF" or self.tx_hz is None:
            return "off", 0
        d = self.tx_hz - (self.rx_hz or 0)
        if d == 0:
            return "", 0
        if abs(d) < 10_000_000:
            return ("+" if d > 0 else "-"), abs(d)
        return "split", self.tx_hz


def _bits_get(byte: int, positions: List[int]) -> int:
    v = 0
    for i, p in enumerate(positions):
        v |= ((byte >> p) & 1) << i
    return v


def _bits_set(byte: int, positions: List[int], value: int) -> int:
    for i, p in enumerate(positions):
        if (value >> i) & 1:
            byte |= 1 << p
        else:
            byte &= ~(1 << p) & 0xFF
    return byte


def decode_channel(idx: int, rec: bytes, tones: List[str]) -> Channel:
    ch = Channel(idx)
    ch.rx_hz = bcd_to_hz(rec[0:4])
    if ch.rx_hz is None:
        return ch
    ch.tx_hz = bcd_to_hz(rec[4:8])

    ch.rx_tone = decode_tone(rec[8], rec[9])
    ch.tx_tone = decode_tone(rec[10], rec[11])
    ch.signal_code = (rec[12] + 1) if rec[12] < 16 else 1
    ch.ptt_id = PTT_ID[rec[13]] if rec[13] < 4 else "OFF"
    p = rec[14] & 0x0F
    ch.power = POWER[p] if p < 3 else "High"
    sc = (rec[14] >> 4) & 0x0F
    ch.scramble = SCRAMBLE[sc] if sc < len(SCRAMBLE) else "OFF"
    f = rec[15]
    ch.rx_am = "AM" if f & 0x01 else "FM"
    ch.tx_enable = OFF_ON[(f >> 1) & 1]
    ch.scan_add = OFF_ON[(f >> 2) & 1]
    ch.busy_lock = OFF_ON[(f >> 3) & 1]
    ch.encryption = ENCRYPTION[(f >> 4) & 3]
    ch.bandwidth = BANDWIDTH[(f >> 6) & 1]
    ch.name = ascii_get(rec[20:32])
    return ch


def encode_channel(ch: Channel, tones: List[str], old: Optional[bytes] = None) -> bytes:
    if ch.empty:
        return b"\xFF" * 32
    fresh = not old or old[0] == 0xFF
    rec = bytearray(b"\x00" * 16 + b"\xFF" * 16 if fresh else old)
    rec[0:4] = hz_to_bcd(ch.rx_hz)
    rec[4:8] = hz_to_bcd(ch.tx_hz if ch.tx_hz is not None else ch.rx_hz)
    rec[8:10] = encode_tone(ch.rx_tone)
    rec[10:12] = encode_tone(ch.tx_tone)
    rec[12] = max(0, min(15, int(ch.signal_code) - 1))
    rec[13] = PTT_ID.index(ch.ptt_id) if ch.ptt_id in PTT_ID else 0
    sc = SCRAMBLE.index(ch.scramble) if ch.scramble in SCRAMBLE else (1 if ch.scramble == "ON" else 0)
    rec[14] = (POWER.index(ch.power) if ch.power in POWER else 0) | (sc << 4)
    f = 0 if fresh else rec[15] & 0x80          # bit 7 = learn FHSS: preserved
    f |= 1 if ch.rx_am == "AM" else 0
    f |= (1 if ch.tx_enable == "ON" else 0) << 1
    f |= (1 if ch.scan_add == "ON" else 0) << 2
    f |= (1 if ch.busy_lock == "ON" else 0) << 3
    f |= (ENCRYPTION.index(ch.encryption) if ch.encryption in ENCRYPTION else 0) << 4
    f |= (1 if ch.bandwidth == "Narrow" else 0) << 6
    rec[15] = f
    if fresh:
        rec[16:20] = b"\xFF" * 4              # FHSS code: none
    rec[20:32] = ascii_put(ch.name, 12)
    return bytes(rec)


# ----------------------------------------------------------- schema fields

@dataclass
class Field:
    section: str
    title: str
    addr: int                 # absolute CPS address of the byte
    positions: List[int]      # bit positions, or [] for the whole byte
    options: List[str]
    group: str = ""

    def get(self, cp: "Codeplug") -> int:
        b = cp.peek(self.addr)
        return b if not self.positions else _bits_get(b, self.positions)

    def set(self, cp: "Codeplug", value: int):
        b = cp.peek(self.addr)
        cp.poke(self.addr, value & 0xFF if not self.positions else _bits_set(b, self.positions, value))

    def text(self, cp: "Codeplug") -> str:
        v = self.get(cp)
        return self.options[v] if 0 <= v < len(self.options) else "0x%02X" % v


def _group_for(addr: int) -> str:
    if 0x8000 <= addr < 0x8080:
        return "VFO " + "ABC"[(addr - 0x8000) // 0x20]
    if 0x9000 <= addr < 0x9030:
        return "General"
    if 0x9030 <= addr < 0x9080:
        return "Keys"
    if 0xA000 <= addr < 0xB000:
        return "DTMF"
    if 0xB000 <= addr < 0xC000 or 0xD000 <= addr < 0xD300:
        return "FM/AM/SSB"
    if 0xC000 <= addr < 0xD000:
        return "Zones"
    if 0xA000 <= addr < 0xB000:
        return "DTMF"
    if addr >= 0xFFFF:
        return "APRS"
    return "Other"


def select_fields() -> List[Field]:
    """All single-byte/bit-field select settings of VFO + optional sections."""
    out: List[Field] = []
    for sec in ("freq", "optional"):
        for c in schema()[sec]:
            d, t = c["data"], c["type"]
            if not t or t[0] != 0 or not c["selectsEn"] or len(d) < 4 or d[0] == 0:
                continue
            base, off, nbits = d[0], d[1], d[2]
            pos = [] if nbits == 8 else list(d[3:3 + nbits])
            if nbits != 8 and len(pos) != nbits:
                continue
            addr = base + off
            out.append(Field(sec, c["titleEn"], addr, pos, list(c["selectsEn"]), _group_for(addr)))
    return out


@dataclass
class TextField:
    group: str
    title: str
    addr: int
    length: int
    upper: bool = False
    pad_space: bool = True


def text_fields() -> List[TextField]:
    """ASCII text entries (schema type 2, kinds 1/3/5): zone names, FM/AM/SSB
    channel names, APRS call sign, custom routes and custom message."""
    out: List[TextField] = []
    for c in schema()["optional"]:
        d, t = c["data"], c["type"]
        if not t or t[0] != 2 or len(d) % 4 or len(d) < 4:
            continue
        labels = c["selectsEn"]
        for i in range(len(d) // 4):
            addr, kind, off, ln = d[i * 4:(i + 1) * 4]
            if kind not in (1, 3, 5) or ln < 2:
                continue
            lab = labels[i] if i < len(labels) else str(i + 1)
            title = "%s %s" % (c["titleEn"], lab) if c["titleEn"] != "APRS Info" else lab
            upper = kind == 1
            a = addr + off
            if not any(ra <= a and a + ln <= ra + rs for _, ra, rs in REGIONS):
                continue            # e.g. a typo'd address in the maker's file
            out.append(TextField(_group_for(a), title, a, ln, upper, pad_space=(kind != 5)))
    return out


@dataclass
class NumField:
    """FM/AM/SSB memory values (little-endian 16-bit, schema kinds 2/4)."""
    group: str
    title: str
    addr: int
    signed: bool
    scale: int          # FM: 100 (value / 100 = MHz); AM/SSB: 1 (kHz)
    lo: float
    hi: float


@dataclass
class DtmfField:
    """DTMF code: one digit per byte (0-9, A-D = 10-13, * = 14, # = 15), 0xFF pad."""
    group: str
    title: str
    addr: int
    length: int


DTMF_CHARS = "0123456789ABCD*#"


def numeric_fields() -> List[NumField]:
    out: List[NumField] = []
    for c in schema()["optional"]:
        d, t = c["data"], c["type"]
        if not t or t[0] != 2 or len(d) % 4:
            continue
        labels = c["selectsEn"]
        for i in range(len(d) // 4):
            addr, kind, off, ln = d[i * 4:(i + 1) * 4]
            if kind not in (2, 4) or ln != 2:
                continue
            title = c["titleEn"].split("(")[0].strip()
            lab = labels[i] if i < len(labels) else str(i + 1)
            fm = title.startswith("FM")
            rng = [x for x in t[2:]] if len(t) > 2 else [0, 65535]
            # SSB/AM byte offsets in the schema are in bytes from the 16-byte row
            a = addr + off
            if not any(ra <= a and a + 2 <= ra + rs for _, ra, rs in REGIONS):
                continue
            name = title if lab == title or len(d) == 4 else "%s %s" % (title, lab)
            signed = kind == 4 or "Offest" in name or "Offset" in name   # one schema row lacks the sign flag
            out.append(NumField(_group_for(a), name, a, signed, 100 if fm else 1,
                                float(min(rng)), float(max(rng))))
    return out


def dtmf_fields() -> List[DtmfField]:
    out: List[DtmfField] = []
    for c in schema()["optional"]:
        d, t = c["data"], c["type"]
        if c["titleEn"] != "DTMF" or len(d) % 4:
            continue
        for i in range(len(d) // 4):
            addr, kind, off, ln = d[i * 4:(i + 1) * 4]
            out.append(DtmfField("DTMF", c["selectsEn"][i] if i < len(c["selectsEn"]) else str(i), addr + off, ln))
    return out


# ----------------------------------------------------------------- codeplug

class Codeplug:
    def __init__(self):
        self.mem: Dict[str, bytearray] = {n: bytearray(b"\xFF" * s) for n, a, s in REGIONS}
        self.tones = tone_list()
        self.dirty_regions = set()
        self.meta = {"model": "RT-950 Pro", "source": "new"}
        # Regions whose bytes are a complete, real image (read from a radio or
        # a CPS file). Other regions are only written by merging the bytes the
        # user changed (`touched`) into a fresh read of the radio.
        self.valid_regions = {"channels"}     # an all-empty channel table is a real image
        self.touched = set()

    def mark_valid(self, regions=None):
        self.valid_regions = set(regions or [n for n, a, s in REGIONS])

    # --- raw access by absolute address
    def _loc(self, addr: int) -> Tuple[str, int]:
        for n, a, s in REGIONS:
            if a <= addr < a + s:
                return n, addr - a
        raise KeyError("address 0x%05X outside codeplug" % addr)

    def peek(self, addr: int) -> int:
        n, o = self._loc(addr)
        return self.mem[n][o]

    def poke(self, addr: int, v: int):
        n, o = self._loc(addr)
        self.touched.add(addr)
        if self.mem[n][o] != v:
            self.mem[n][o] = v
            self.dirty_regions.add(n)

    def read(self, addr: int, length: int) -> bytes:
        return bytes(self.peek(addr + i) for i in range(length))

    def write(self, addr: int, data: bytes):
        for i, b in enumerate(data):
            self.poke(addr + i, b)

    # --- channels
    def channel(self, idx: int) -> Channel:
        return decode_channel(idx, bytes(self.mem["channels"][idx * 32:(idx + 1) * 32]), self.tones)

    def channels(self) -> List[Channel]:
        return [self.channel(i) for i in range(CHANNELS)]

    def set_channel(self, ch: Channel):
        old = bytes(self.mem["channels"][ch.index * 32:(ch.index + 1) * 32])
        new = encode_channel(ch, self.tones, old)
        self.write(ch.index * 32, new)

    def clear_channel(self, idx: int):
        self.write(idx * 32, b"\xFF" * 32)

    def first_free(self, start: int = 0) -> Optional[int]:
        for i in range(start, CHANNELS):
            if self.mem["channels"][i * 32] == 0xFF:
                return i
        return None

    # --- zones
    def zone_name(self, z: int) -> str:
        return ascii_get(self.read(ZONE_NAME_ADDR + z * ZONE_NAME_STRIDE, ZONE_NAME_STRIDE))

    def set_zone_name(self, z: int, name: str):
        self.write(ZONE_NAME_ADDR + z * ZONE_NAME_STRIDE, ascii_put(name, ZONE_NAME_STRIDE))

    # --- VFO frequencies (digit per byte)
    def vfo_freq(self, v: int) -> Optional[int]:
        return digits_to_hz(self.read(0x8000 + 0x20 * v, 8))

    def set_vfo_freq(self, v: int, hz: int):
        self.write(0x8000 + 0x20 * v, hz_to_digits(hz, 8))

    def vfo_offset(self, v: int) -> Optional[int]:
        return digits_to_hz(self.read(0x8014 + 0x20 * v, 7))

    def set_vfo_offset(self, v: int, hz: int):
        self.write(0x8014 + 0x20 * v, hz_to_digits(hz, 7))

    # --- text fields described by the schema (zone names, APRS, ...)
    def text_value(self, f: "TextField") -> str:
        return ascii_get(self.read(f.addr, f.length))

    def set_text_value(self, f: "TextField", value: str):
        self.write(f.addr, ascii_put(value.upper() if f.upper else value, f.length,
                                     pad=0xFF))

    # --- numeric (FM/AM/SSB) and DTMF values
    def num_value(self, f: NumField) -> Optional[float]:
        lo, hi = self.peek(f.addr), self.peek(f.addr + 1)
        if lo == 0xFF and hi == 0xFF:
            return None
        v = lo | hi << 8
        if f.signed and v >= 0x8000:
            v -= 0x10000
        return v / f.scale

    def set_num_value(self, f: NumField, value: Optional[float]):
        if value is None:
            self.write(f.addr, b"\xFF\xFF")
            return
        if not f.lo <= value <= f.hi:
            raise ValueError("%s: %g outside %g-%g" % (f.title, value, f.lo, f.hi))
        v = int(round(value * f.scale)) & 0xFFFF
        self.write(f.addr, bytes([v & 0xFF, v >> 8]))

    def dtmf_value(self, f: DtmfField) -> str:
        out = ""
        for b in self.read(f.addr, f.length):
            if b >= len(DTMF_CHARS):
                break
            out += DTMF_CHARS[b]
        return out

    def set_dtmf_value(self, f: DtmfField, text: str):
        text = text.strip().upper()[:f.length]
        bad = [c for c in text if c not in DTMF_CHARS]
        if bad:
            raise ValueError("DTMF: invalid character %r" % bad[0])
        self.write(f.addr, bytes(DTMF_CHARS.index(c) for c in text) + b"\xFF" * (f.length - len(text)))

    # --- APRS shortcuts
    def aprs_callsign(self) -> Tuple[str, int]:
        call = self.text_value(TextField("APRS", "Call Sign", 0x1000F + 1, 6, True))
        ssid = self.peek(0x1000F + 7)
        return call, (ssid if ssid < 16 else 0)

    def set_aprs_callsign(self, call: str, ssid: int):
        self.set_text_value(TextField("APRS", "Call Sign", 0x1000F + 1, 6, True), call)
        self.poke(0x1000F + 7, max(0, min(15, ssid)))

    # --- persistence
    def merged_region(self, name: str, base: bytes) -> bytes:
        """`base` (a fresh read of the radio) with the bytes changed here."""
        a0 = dict((n, a) for n, a, s in REGIONS)[name]
        out = bytearray(base)
        mine = self.mem[name]
        for addr in self.touched:
            o = addr - a0
            if 0 <= o < len(out):
                out[o] = mine[o]
        return bytes(out)

    def to_json(self) -> dict:
        partial = [(a, a + s) for n, a, s in REGIONS if n not in self.valid_regions]
        meta = dict(self.meta, valid_regions=sorted(self.valid_regions),
                    touched=sorted(t for t in self.touched if any(lo <= t < hi for lo, hi in partial)))
        return {"format": "rt950-toolkit-codeplug", "version": 1, "meta": meta,
                "regions": {n: {"addr": a, "data": base64.b64encode(bytes(self.mem[n])).decode()}
                            for n, a, s in REGIONS}}

    @classmethod
    def from_json(cls, d: dict) -> "Codeplug":
        if d.get("format") != "rt950-toolkit-codeplug":
            raise ValueError("not an RT-950 Toolkit codeplug file")
        cp = cls()
        cp.meta = dict(d.get("meta", {}))
        vr = cp.meta.pop("valid_regions", None)
        cp.touched = set(cp.meta.pop("touched", []) or [])
        cp.valid_regions = set(vr) if vr is not None else set(n for n, a, s in REGIONS)
        for n, a, s in REGIONS:
            if n in d["regions"]:
                raw = base64.b64decode(d["regions"][n]["data"])
                cp.mem[n][:len(raw[:s])] = raw[:s]
        return cp

    def save(self, path: str):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.to_json(), f, indent=1)
        self.dirty_regions.clear()

    @classmethod
    def load(cls, path: str) -> "Codeplug":
        with open(path, encoding="utf-8") as f:
            return cls.from_json(json.load(f))

    def export_raw(self, path: str):
        """Concatenated regions (same order as REGIONS) - for diffing."""
        with open(path, "wb") as f:
            for n, a, s in REGIONS:
                f.write(self.mem[n])

    @classmethod
    def import_raw(cls, path: str) -> "Codeplug":
        cp = cls()
        with open(path, "rb") as f:
            for n, a, s in REGIONS:
                chunk = f.read(s)
                cp.mem[n][:len(chunk)] = chunk
        cp.mark_valid()
        return cp
