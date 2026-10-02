"""
csvio.py - Channel import/export: native CSV and CHIRP CSV

Native CSV has one column per channel field (lossless).  CHIRP CSV is the
common exchange format (CHIRP, RT-950/950Pro Editor, RepeaterBook exports).
"""

from __future__ import annotations

import csv
import io
from typing import Iterable, List, Optional, Tuple

from .codeplug import Channel, Codeplug, CHANNELS, POWER, BANDWIDTH, PTT_ID, ENCRYPTION

NATIVE_COLUMNS = ["Number", "Zone", "Name", "RX MHz", "TX MHz", "RX Tone", "TX Tone", "Power",
                  "Bandwidth", "Scramble", "Busy Lock", "Scan Add", "Encryption", "TX Enable",
                  "RX Mod", "Signal Code", "PTT-ID"]

CHIRP_COLUMNS = ["Location", "Name", "Frequency", "Duplex", "Offset", "Tone", "rToneFreq",
                 "cToneFreq", "DtcsCode", "DtcsPolarity", "RxDtcsCode", "CrossMode", "Mode",
                 "TStep", "Skip", "Power", "Comment", "URCALL", "RPT1CALL", "RPT2CALL", "DVCODE"]


def mhz(hz: Optional[int]) -> str:
    return "" if hz is None else "%.5f" % (hz / 1e6)


def parse_mhz(s: str) -> Optional[int]:
    s = (s or "").strip().replace(",", ".")
    return int(round(float(s) * 1e6)) if s else None


# ------------------------------------------------------------------ native

def export_native(cp: Codeplug, path: str, only_used: bool = True):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(NATIVE_COLUMNS)
        for ch in cp.channels():
            if only_used and ch.empty:
                continue
            w.writerow([ch.number, ch.zone + 1, ch.name, mhz(ch.rx_hz), mhz(ch.tx_hz), ch.rx_tone,
                        ch.tx_tone, ch.power, ch.bandwidth, ch.scramble, ch.busy_lock, ch.scan_add,
                        ch.encryption, ch.tx_enable, ch.rx_am, ch.signal_code, ch.ptt_id])


def import_native(cp: Codeplug, path: str) -> int:
    n = 0
    with open(path, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            idx = int(row["Number"]) - 1
            if not 0 <= idx < CHANNELS:
                continue
            rx = parse_mhz(row.get("RX MHz", ""))
            if rx is None:
                cp.clear_channel(idx)
                continue
            ch = Channel(idx, rx, parse_mhz(row.get("TX MHz", "")) or rx,
                         row.get("RX Tone", "OFF") or "OFF", row.get("TX Tone", "OFF") or "OFF",
                         row.get("Power", "High"), row.get("Bandwidth", "Wide"), row.get("Scramble", "OFF"),
                         row.get("Busy Lock", "OFF"), row.get("Scan Add", "ON"), row.get("Encryption", "OFF"),
                         row.get("TX Enable", "ON"), row.get("RX Mod", "FM"),
                         int(row.get("Signal Code") or 1), row.get("PTT-ID", "OFF"), row.get("Name", ""))
            cp.set_channel(ch)
            n += 1
    return n


# ------------------------------------------------------------------- CHIRP

def _tone_to_chirp(rx: str, tx: str):
    """-> Tone, rToneFreq, cToneFreq, DtcsCode, DtcsPolarity, RxDtcsCode, CrossMode"""
    def kind(t):
        if t == "OFF":
            return None
        return "D" if t.startswith("D") else "T"
    kr, kt = kind(rx), kind(tx)
    r_tone, c_tone, dcs, pol, rxdcs, cross = "88.5", "88.5", "023", "NN", "023", "Tone->Tone"
    if kt == "T":
        r_tone = tx
    if kr == "T":
        c_tone = rx
    if kt == "D":
        dcs = tx[1:4]
    if kr == "D":
        rxdcs = rx[1:4]
    if kt == "D" or kr == "D":
        pol = (tx[-1] if kt == "D" else "N") + (rx[-1] if kr == "D" else "N")
    if kt is None and kr is None:
        mode = ""
    elif kt == "T" and kr is None:
        mode = "Tone"
    elif kt == "T" and kr == "T" and tx == rx:
        mode = "TSQL"
    elif kt == "D" and kr == "D" and tx[1:4] == rx[1:4]:
        mode = "DTCS"
    else:
        mode = "Cross"
        cross = "%s->%s" % ({"T": "Tone", "D": "DTCS", None: ""}[kt], {"T": "Tone", "D": "DTCS", None: ""}[kr])
    return mode, r_tone, c_tone, dcs, pol, rxdcs if mode == "Cross" else dcs, cross


def _tone_from_chirp(row: dict) -> Tuple[str, str]:
    mode = (row.get("Tone") or "").strip()
    rt, ct = (row.get("rToneFreq") or "88.5").strip(), (row.get("cToneFreq") or "88.5").strip()
    dcs, rxdcs = (row.get("DtcsCode") or "023").strip().zfill(3), (row.get("RxDtcsCode") or "023").strip().zfill(3)
    pol = (row.get("DtcsPolarity") or "NN").strip().upper()
    pol = (pol + "NN")[:2]
    fmt = lambda f: "%.1f" % float(f)
    if mode == "Tone":
        return "OFF", fmt(rt)
    if mode == "TSQL":
        return fmt(ct), fmt(ct)
    if mode == "DTCS":
        return "D%s%s" % (dcs, pol[1]), "D%s%s" % (dcs, pol[0])
    if mode == "Cross":
        cm = (row.get("CrossMode") or "Tone->Tone").strip()
        txk, rxk = (cm.split("->") + [""])[:2]
        tx = fmt(rt) if txk == "Tone" else ("D%s%s" % (dcs, pol[0]) if txk == "DTCS" else "OFF")
        rx = fmt(ct) if rxk == "Tone" else ("D%s%s" % (rxdcs, pol[1]) if rxk == "DTCS" else "OFF")
        return rx, tx
    return "OFF", "OFF"


def export_chirp(cp: Codeplug, path: str, only_used: bool = True):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(CHIRP_COLUMNS)
        for ch in cp.channels():
            if only_used and ch.empty:
                continue
            dup, off = ch.duplex()
            offs = "%.6f" % (off / 1e6) if off else "0.000000"
            mode, rt, ct, dcs, pol, rxdcs, cross = _tone_to_chirp(ch.rx_tone, ch.tx_tone)
            w.writerow([ch.index, ch.name, "%.6f" % (ch.rx_hz / 1e6), dup, offs, mode, rt, ct, dcs, pol,
                        rxdcs, cross, "AM" if ch.rx_am == "AM" else ("NFM" if ch.bandwidth == "Narrow" else "FM"),
                        "12.50", "" if ch.scan_add == "ON" else "S",
                        {"High": "5.0W", "Mid": "2.5W", "Low": "1.0W"}.get(ch.power, "5.0W"),
                        "", "", "", "", ""])


def import_chirp(cp: Codeplug, path_or_text: str, start: Optional[int] = None,
                 overwrite: bool = False, text: bool = False) -> List[int]:
    """Import CHIRP rows. start=None keeps the CHIRP Location numbers,
    otherwise rows are placed sequentially from `start` (0-based), skipping
    used slots unless overwrite=True. Returns the slots written."""
    src = io.StringIO(path_or_text) if text else open(path_or_text, newline="", encoding="utf-8-sig")
    written = []
    with src as f:
        pos = start
        for row in csv.DictReader(f):
            try:
                rx = parse_mhz(row.get("Frequency", ""))
            except ValueError:
                continue
            if rx is None:
                continue
            dup = (row.get("Duplex") or "").strip().lower()
            off = parse_mhz(row.get("Offset", "0")) or 0
            tx, txen = rx, "ON"
            if dup == "+":
                tx = rx + off
            elif dup == "-":
                tx = rx - off
            elif dup == "split":
                tx = off
            elif dup == "off":
                txen = "OFF"
            rxt, txt = _tone_from_chirp(row)
            m = (row.get("Mode") or "FM").strip().upper()
            pw = (row.get("Power") or "").strip().upper().rstrip("W")
            try:
                pwv = float(pw)
                power = "High" if pwv >= 4 else ("Mid" if pwv >= 2 else "Low")
            except ValueError:
                power = {"HIGH": "High", "MID": "Mid", "LOW": "Low"}.get(pw, "High")
            if start is None:
                idx = int(row.get("Location") or 0)
                if idx >= CHANNELS:
                    continue
            else:
                while pos < CHANNELS and not overwrite and not cp.channel(pos).empty:
                    pos += 1
                if pos >= CHANNELS:
                    break
                idx = pos
                pos += 1
            ch = Channel(idx, rx, tx, rxt, txt, power, "Narrow" if m == "NFM" else "Wide", "OFF", "OFF",
                         "OFF" if (row.get("Skip") or "").strip() else "ON", "OFF", txen,
                         "AM" if m == "AM" else "FM", 1, "OFF", (row.get("Name") or "")[:12])
            cp.set_channel(ch)
            written.append(idx)
    return written


# ------------------------------------------------- RT-950/950Pro Editor CSV
# Same columns as the maker's .dat channel objects (and KK4OXN's editor
# template): slot, rxFreq, txFreq, rxQT, txQT, signallingGroup, pttId,
# txPower, scram, learnFHSS, bandWide, encrypt, busyLockout, scanAdd,
# enableTx, rxModulation, fhssCode, chName  (numbers are raw radio indexes)
EDITOR_COLUMNS = ["slot", "rxFreq", "txFreq", "rxQT", "txQT", "signallingGroup", "pttId", "txPower", "scram",
                  "learnFHSS", "bandWide", "encrypt", "busyLockout", "scanAdd", "enableTx", "rxModulation",
                  "fhssCode", "chName"]


def export_editor(cp: Codeplug, path: str, only_used: bool = True):
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(EDITOR_COLUMNS)
        from .codeplug import SCRAMBLE
        for ch in cp.channels():
            if only_used and ch.empty:
                continue
            fh = cp.peek(ch.index * 32 + 15) >> 7
            w.writerow([ch.number, "%.5f" % (ch.rx_hz / 1e6), "%.5f" % ((ch.tx_hz or ch.rx_hz) / 1e6),
                        ch.rx_tone, ch.tx_tone, ch.signal_code - 1, PTT_ID.index(ch.ptt_id), POWER.index(ch.power),
                        SCRAMBLE.index(ch.scramble) if ch.scramble in SCRAMBLE else 0, fh,
                        BANDWIDTH.index(ch.bandwidth), ENCRYPTION.index(ch.encryption),
                        1 if ch.busy_lock == "ON" else 0, 1 if ch.scan_add == "ON" else 0,
                        1 if ch.tx_enable == "ON" else 0, 1 if ch.rx_am == "AM" else 0, "", ch.name])


def import_editor(cp: Codeplug, path: str) -> int:
    from .codeplug import SCRAMBLE
    n = 0
    with open(path, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            try:
                idx = int(row["slot"]) - 1
            except (KeyError, ValueError):
                continue
            if not 0 <= idx < CHANNELS:
                continue
            rx = parse_mhz(row.get("rxFreq", ""))
            if rx is None:
                cp.clear_channel(idx)
                continue
            g = lambda k, d=0: int((row.get(k) or "").strip() or d)
            ch = Channel(idx, rx, parse_mhz(row.get("txFreq", "")) or rx, (row.get("rxQT") or "OFF").strip(),
                         (row.get("txQT") or "OFF").strip(), POWER[g("txPower") % 3], BANDWIDTH[g("bandWide") & 1],
                         SCRAMBLE[min(8, g("scram"))], "ON" if g("busyLockout") else "OFF",
                         "ON" if g("scanAdd", 1) else "OFF", ENCRYPTION[g("encrypt") & 3],
                         "ON" if g("enableTx", 1) else "OFF", "AM" if g("rxModulation") else "FM",
                         g("signallingGroup") + 1, PTT_ID[g("pttId") & 3], (row.get("chName") or "")[:12])
            cp.set_channel(ch)
            n += 1
    return n


# ------------------------------------------------- RT-950 Editor zones / FM-AM-SSB
ZONE_COLUMNS = ["zone", "zone_name"]
MOD_COLUMNS = ["index", "label", "fmFreq", "fmName", "amFreq", "amName", "ssbFreq", "ssbBandwidth",
               "ssbBeatFreqOffset", "ssbName", "notes"]
CLEAR = "<blank>"


def export_zones(cp: Codeplug, path: str):
    from .codeplug import ZONES
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(ZONE_COLUMNS)
        for z in range(ZONES):
            w.writerow([z + 1, cp.zone_name(z)])


def import_zones(cp: Codeplug, path: str) -> int:
    from .codeplug import ZONES
    n = 0
    with open(path, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            try:
                z = int(row["zone"]) - 1
            except (KeyError, ValueError):
                continue
            if 0 <= z < ZONES and row.get("zone_name") is not None:
                cp.set_zone_name(z, row["zone_name"].strip()[:16])
                n += 1
    return n


def _mod_map():
    """index (0 = Cur) -> dict of field objects for the FM/AM/SSB memories."""
    from .codeplug import numeric_fields, text_fields
    m = {}
    for f in numeric_fields():
        if f.group != "FM/AM/SSB":
            continue
        kind, _, rest = f.title.partition(" ")
        if kind == "SSB" and rest.startswith("Off"):
            i, key = int(rest.split()[-1]), "ssbBeatFreqOffset"
        else:
            i = 0 if rest == "Cur" else int(rest)
            key = kind.lower() + "Freq"
        m.setdefault(i, {})[key] = f
    for t in text_fields():
        if t.group == "FM/AM/SSB" and " CH Name " in t.title:
            kind, i = t.title.split()[0], int(t.title.split()[-1])
            m.setdefault(i, {})[kind.lower() + "Name"] = t
    for i, d in m.items():               # SSB rows: freq(2) bandwidth(1) offset(2)
        if "ssbFreq" in d:
            d["ssbBandwidth"] = d["ssbFreq"].addr + 2
    return m


def export_modulation(cp: Codeplug, path: str):
    m = _mod_map()
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(MOD_COLUMNS)
        for i in sorted(m):
            d, row = m[i], {"index": i, "label": "Cur Freq" if i == 0 else i}
            for k, fld in d.items():
                if k == "ssbBandwidth":
                    b = cp.peek(fld)
                    row[k] = "" if b == 0xFF else b
                elif k.endswith("Name"):
                    row[k] = cp.text_value(fld)
                else:
                    v = cp.num_value(fld)
                    row[k] = "" if v is None else ("%.2f" % v if fld.scale == 100 else "%d" % v)
            w.writerow([row.get(c, "") for c in MOD_COLUMNS])


def import_modulation(cp: Codeplug, path: str) -> int:
    """Blank cell = keep the current value, '<blank>' = clear it."""
    m = _mod_map()
    n = 0
    with open(path, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            try:
                d = m[int(row["index"])]
            except (KeyError, ValueError):
                continue
            for k, fld in d.items():
                val = (row.get(k) or "").strip()
                if not val:
                    continue
                clear = val == CLEAR
                if k == "ssbBandwidth":
                    cp.poke(fld, 0xFF if clear else int(val) & 0xFF)
                elif k.endswith("Name"):
                    cp.set_text_value(fld, "" if clear else val)
                else:
                    cp.set_num_value(fld, None if clear else float(val.replace(",", ".")))
                n += 1
    return n
