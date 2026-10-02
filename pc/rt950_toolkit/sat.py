#!/usr/bin/env python3
"""
rt950_sat.py - Satellite support tool for the Radtel RT-950 / RT-950 Pro

  * Downloads current Keplerian elements (Celestrak / AMSAT / SatNOGS)
  * Matches them with a frequency database (sat_freqs.json)
  * Predicts passes for your QTH (Maidenhead locator or lat/lon)
  * Writes a pass report (HTML + CSV + iCalendar)
  * Builds the satellite database for the custom firmware (satdb.bin) and
    uploads it over the programming cable (TLE + frequencies + passes + UTC)
  * For the STOCK firmware: generates Doppler-stepped split channels as a
    CHIRP CSV that RT-950/950Pro Editor (KK4OXN) opens and writes to the radio,
    plus a per-pass "which channel when" schedule.

Usage examples
  python rt950_sat.py all --locator IM98IB                 # everything, no radio
  python rt950_sat.py all --locator IM98IB --port COM5     # + upload (custom FW)
  python rt950_sat.py passes --locator IM98IB --days 2 --min-el 10
  python rt950_sat.py chirp --locator IM98IB --start-channel 900
  python rt950_sat.py upload --port /dev/ttyUSB0 --db out/satdb.bin
  python rt950_sat.py gui                                   # RT-950 Toolkit, satellite tab

Requirements:  pip install sgp4 pyserial      (Python 3.8+)
License: GPL-3.0 (same as the RT-950 Pro custom firmware project)
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import html
import io
import json
import math
import os
import struct
import sys
import time
import urllib.request
import zlib
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

try:
    from sgp4.api import Satrec, WGS72
except ImportError:  # pragma: no cover
    print("This tool needs the 'sgp4' package:  pip install sgp4", file=sys.stderr)
    raise

__version__ = "1.0.0"

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_FREQS = os.path.join(HERE, "data", "sat_freqs.json")
DEFAULT_OUT = os.path.join(os.getcwd(), "rt950_sat_out")
DEFAULT_LOCATOR = "IM98IB"
DEFAULT_TZ = "Europe/Madrid"

TLE_SOURCES = [
    ("Celestrak amateur", "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"),
    ("Celestrak stations", "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle"),
    ("AMSAT nasabare", "https://www.amsat.org/tle/current/nasabare.txt"),
]

C_KMS = 299792.458
EARTH_ROT = 7.29211514670698e-5
WGS84_A = 6378.137
WGS84_F = 1.0 / 298.257223563

# --- must match include/app/sat_db.h ----------------------------------------
SAT_DB_MAGIC = 0x44544153
SAT_DB_VERSION = 1
SAT_DB_FLASH_ADDR = 0x0C0000
SAT_DB_FLASH_SIZE = 0x8000
SAT_DB_MAX_SATS = 48
SAT_DB_MAX_PASSES = 1024
HDR_FMT = "<IHHHHHHIiihbBII8sI8sI"
SAT_FMT = "<12sI8dIIHHHBBI24s4s"
PASS_FMT = "<IHHBBHHH"
MODES = {"FM": 0, "FM_DATA": 1, "LIN_INV": 2, "LIN": 3, "RX_ONLY": 4}
FLAG_ENABLED, FLAG_FAVORITE, FLAG_DEEP, FLAG_SCHED, FLAG_SUNLIT = 1, 2, 4, 8, 16
assert struct.calcsize(HDR_FMT) == 64
assert struct.calcsize(SAT_FMT) == 128
assert struct.calcsize(PASS_FMT) == 16

# Radio transmit bands (RT-950 Pro, amateur 2 m / 70 cm)
TX_BANDS_MHZ = [(144.0, 148.0), (420.0, 450.0)]


# ============================================================================
#  Geography
# ============================================================================

def locator_to_latlon(loc: str) -> Tuple[float, float]:
    """Maidenhead locator (4, 6 or 8 chars) -> centre lat, lon (degrees)."""
    loc = loc.strip()
    if len(loc) not in (4, 6, 8):
        raise ValueError("locator must have 4, 6 or 8 characters: %r" % loc)
    loc = loc[:2].upper() + loc[2:4] + loc[4:6].lower() + loc[6:8]
    lon = (ord(loc[0]) - ord("A")) * 20.0 - 180.0
    lat = (ord(loc[1]) - ord("A")) * 10.0 - 90.0
    lon += int(loc[2]) * 2.0
    lat += int(loc[3]) * 1.0
    dlon, dlat = 2.0, 1.0
    if len(loc) >= 6:
        lon += (ord(loc[4]) - ord("a")) * 5.0 / 60.0
        lat += (ord(loc[5]) - ord("a")) * 2.5 / 60.0
        dlon, dlat = 5.0 / 60.0, 2.5 / 60.0
    if len(loc) == 8:
        lon += int(loc[6]) * 0.5 / 60.0
        lat += int(loc[7]) * 0.25 / 60.0
        dlon, dlat = 0.5 / 60.0, 0.25 / 60.0
    return lat + dlat / 2.0, lon + dlon / 2.0


def latlon_to_locator(lat: float, lon: float) -> str:
    lon += 180.0
    lat += 90.0
    a = chr(ord("A") + int(lon / 20)); b = chr(ord("A") + int(lat / 10))
    lon %= 20; lat %= 10
    c = str(int(lon / 2)); d = str(int(lat))
    lon %= 2; lat %= 1
    e = chr(ord("a") + int(lon * 12)); f = chr(ord("a") + int(lat * 24))
    return a + b + c + d + e + f


class Observer:
    def __init__(self, lat: float, lon: float, alt_m: float = 0.0):
        self.lat, self.lon, self.alt_m = lat, lon, alt_m
        la, lo = math.radians(lat), math.radians(lon)
        self.sla, self.cla, self.slo, self.clo = math.sin(la), math.cos(la), math.sin(lo), math.cos(lo)
        e2 = WGS84_F * (2 - WGS84_F)
        n = WGS84_A / math.sqrt(1 - e2 * self.sla ** 2)
        h = alt_m / 1000.0
        self.x = (n + h) * self.cla * self.clo
        self.y = (n + h) * self.cla * self.slo
        self.z = (n * (1 - e2) + h) * self.sla

    @property
    def locator(self) -> str:
        return latlon_to_locator(self.lat, self.lon)


def gmst(jd: float) -> float:
    t = (jd - 2451545.0) / 36525.0
    s = (-6.2e-6 * t ** 3 + 0.093104 * t ** 2 + (876600.0 * 3600 + 8640184.812866) * t + 67310.54841)
    return math.fmod(math.radians(s / 240.0), 2 * math.pi) % (2 * math.pi)


def unix_to_jd(t: float) -> float:
    return 2440587.5 + t / 86400.0


# ============================================================================
#  TLE handling
# ============================================================================

def tle_checksum_ok(line: str) -> bool:
    if len(line) < 69 or not line[68].isdigit():
        return False
    s = sum(int(c) if c.isdigit() else (1 if c == "-" else 0) for c in line[:68])
    return s % 10 == int(line[68])


def alpha5_to_int(s: str) -> int:
    s = s.strip()
    if s and s[0].isalpha():
        letters = "ABCDEFGHJKLMNPQRSTUVWXYZ"   # I and O are skipped
        return (letters.index(s[0].upper()) + 10) * 10000 + int(s[1:])
    return int(s)


@dataclass
class Tle:
    name: str
    line1: str
    line2: str

    @property
    def norad(self) -> int:
        return alpha5_to_int(self.line1[2:7])

    def satrec(self) -> Satrec:
        return Satrec.twoline2rv(self.line1, self.line2, WGS72)

    def epoch_unix(self) -> float:
        s = self.satrec()
        return (s.jdsatepoch + s.jdsatepochF - 2440587.5) * 86400.0


def parse_tle_text(text: str) -> Dict[int, Tle]:
    """Parse 3-line (name + 2 lines) or bare 2-line element sets."""
    out: Dict[int, Tle] = {}
    lines = [l.rstrip() for l in text.replace("\r", "").split("\n")]
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith("1 ") and i + 1 < len(lines) and lines[i + 1].startswith("2 "):
            name = lines[i - 1].strip() if i > 0 and not lines[i - 1].startswith(("1 ", "2 ")) else ""
            l1, l2 = l[:69], lines[i + 1][:69]
            if tle_checksum_ok(l1) and tle_checksum_ok(l2):
                t = Tle(name.lstrip("0 ").strip() or ("NORAD %s" % l1[2:7].strip()), l1, l2)
                prev = out.get(t.norad)
                if prev is None or t.epoch_unix() > prev.epoch_unix():
                    out[t.norad] = t
            i += 2
        else:
            i += 1
    return out


def http_get(url: str, timeout: float = 20.0) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": "rt950_sat/%s (amateur radio)" % __version__})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def fetch_tles(cache_file: str, sources=None, log=print) -> Dict[int, Tle]:
    sources = sources or TLE_SOURCES
    merged: Dict[int, Tle] = {}
    texts = []
    for name, url in sources:
        try:
            txt = http_get(url)
            got = parse_tle_text(txt)
            log("  %-20s %4d element sets" % (name, len(got)))
            texts.append("# %s %s\n%s" % (name, url, txt))
            for k, v in got.items():
                if k not in merged or v.epoch_unix() > merged[k].epoch_unix():
                    merged[k] = v
        except Exception as e:  # network errors are not fatal
            log("  %-20s FAILED (%s)" % (name, e))
    if merged:
        os.makedirs(os.path.dirname(os.path.abspath(cache_file)), exist_ok=True)
        with open(cache_file, "w", encoding="utf-8") as f:
            for t in merged.values():
                f.write("%s\n%s\n%s\n" % (t.name, t.line1, t.line2))
    return merged


def load_tles(path: str) -> Dict[int, Tle]:
    with open(path, encoding="utf-8", errors="replace") as f:
        return parse_tle_text(f.read())


# ============================================================================
#  Satellite records (TLE + frequencies)
# ============================================================================

@dataclass
class SatEntry:
    name: str
    norad: int
    tle: Tle
    mode: str
    downlink_mhz: float
    uplink_mhz: float
    ctcss_up: float = 0.0
    ctcss_down: float = 0.0
    arm_tone: float = 0.0
    passband_khz: float = 0.0
    flags: List[str] = field(default_factory=list)
    info: str = ""
    enabled: bool = True
    favorite: bool = False
    short: str = ""

    @property
    def label(self) -> str:
        """Short tag used in 12-character channel names."""
        return (self.short or self.name.replace("-", "").replace(" ", ""))[:7]

    @property
    def deep_space(self) -> bool:
        s = self.tle.satrec()
        return (2 * math.pi / s.no_kozai) >= 225.0

    @property
    def can_tx(self) -> bool:
        return (self.mode in ("FM", "FM_DATA") and self.uplink_mhz > 0 and
                any(lo <= self.uplink_mhz <= hi for lo, hi in TX_BANDS_MHZ))

    @property
    def tle_age_days(self) -> float:
        return (time.time() - self.tle.epoch_unix()) / 86400.0


def load_freqdb(path: str) -> List[dict]:
    with open(path, encoding="utf-8") as f:
        return json.load(f)["satellites"]


def build_entries(freqdb: List[dict], tles: Dict[int, Tle], max_age_days: float = 45.0,
                  log=print) -> List[SatEntry]:
    out: List[SatEntry] = []
    by_name = {t.name.upper(): t for t in tles.values()}
    for f in freqdb:
        matches: List[Tle] = []
        if f.get("norad") and f["norad"] in tles:
            matches = [tles[f["norad"]]]
        elif f.get("expand"):
            pats = [m.upper() for m in f.get("match", [])]
            matches = sorted((t for n, t in by_name.items() if any(p in n for p in pats)),
                             key=lambda t: t.name)
        else:
            for p in f.get("match", []):
                cand = [t for n, t in by_name.items() if p.upper() == n or ("(" + p.upper() + ")") in n]
                if cand:
                    matches = cand[:1]
                    break
        if not matches:
            log("  - %-10s no TLE found (norad %s) - skipped" % (f["name"], f.get("norad")))
            continue
        for k, t in enumerate(matches):
            name = f["name"] if not f.get("expand") else t.name.split("(")[0].strip()[:12]
            e = SatEntry(name=name[:12], norad=t.norad, tle=t, mode=f.get("mode", "FM"),
                         downlink_mhz=float(f.get("downlink", 0)), uplink_mhz=float(f.get("uplink", 0)),
                         ctcss_up=float(f.get("ctcss_up", 0)), ctcss_down=float(f.get("ctcss_down", 0)),
                         arm_tone=float(f.get("arm_tone", 0)), passband_khz=float(f.get("passband", 0)),
                         flags=list(f.get("flags", [])), info=f.get("info", ""),
                         enabled=bool(f.get("enabled", True)), favorite=bool(f.get("favorite", False)),
                         short="" if f.get("expand") else f.get("short", ""))
            age = e.tle_age_days
            if age > max_age_days:
                log("  - %-10s TLE is %.0f days old (decayed / inactive?) - skipped" % (e.name, age))
                continue
            if age > 14:
                log("  ! %-10s TLE is %.0f days old" % (e.name, age))
            out.append(e)
    return out[:SAT_DB_MAX_SATS]


# ============================================================================
#  Prediction
# ============================================================================

@dataclass
class Look:
    az: float
    el: float
    rng: float
    rr: float  # km/s, + receding


def look(sat: Satrec, obs: Observer, t_unix: float) -> Optional[Look]:
    jd = unix_to_jd(t_unix)
    jd_i = math.floor(jd)
    e, r, v = sat.sgp4(jd_i, jd - jd_i)
    if e != 0:
        return None
    g = gmst(jd)
    sg, cg = math.sin(g), math.cos(g)
    rx, ry, rz = cg * r[0] + sg * r[1], -sg * r[0] + cg * r[1], r[2]
    vx = cg * v[0] + sg * v[1] + EARTH_ROT * ry
    vy = -sg * v[0] + cg * v[1] - EARTH_ROT * rx
    vz = v[2]
    dx, dy, dz = rx - obs.x, ry - obs.y, rz - obs.z
    rng = math.sqrt(dx * dx + dy * dy + dz * dz)
    s = obs.sla * obs.clo * dx + obs.sla * obs.slo * dy - obs.cla * dz
    east = -obs.slo * dx + obs.clo * dy
    zen = obs.cla * obs.clo * dx + obs.cla * obs.slo * dy + obs.sla * dz
    az = math.degrees(math.atan2(east, -s)) % 360.0
    el = math.degrees(math.asin(max(-1.0, min(1.0, zen / rng))))
    return Look(az, el, rng, (dx * vx + dy * vy + dz * vz) / rng)


@dataclass
class Pass:
    sat: SatEntry
    index: int          # satellite index in the DB
    aos: float
    tca: float
    los: float
    max_el: float
    aos_az: float
    tca_az: float
    los_az: float
    track: List[Tuple[float, float, float, float]] = field(default_factory=list)  # t, az, el, rr

    @property
    def duration(self) -> float:
        return self.los - self.aos


def _el(sat, obs, t):
    lk = look(sat, obs, t)
    return lk.el if lk else -90.0


def _bisect(sat, obs, mask, a, b):
    above_a = _el(sat, obs, a) >= mask
    while b - a > 0.5:
        m = 0.5 * (a + b)
        if (_el(sat, obs, m) >= mask) == above_a:
            a = m
        else:
            b = m
    return a if above_a else b


def predict_passes(entry: SatEntry, index: int, obs: Observer, start: float, end: float,
                   mask: float = 0.0, track_step: float = 10.0) -> List[Pass]:
    """Same strategy as the firmware (src/app/sat_pred.c)."""
    sat = entry.tle.satrec()
    passes: List[Pass] = []
    t = start
    el = _el(sat, obs, t)
    if el >= mask:                       # in progress: walk back to AOS
        while el >= mask and start - t < 2400:
            t -= 20
            el = _el(sat, obs, t)
        t = _bisect(sat, obs, mask, t, t + 20) - 1
        el = _el(sat, obs, t)
    while t < end:
        dt_ = 240 if el < -40 else 120 if el < -20 else 45 if el < -8 else 20
        t2 = t + dt_
        el2 = _el(sat, obs, t2)
        if el2 >= mask:
            aos = _bisect(sat, obs, mask, t, t2)
            tt, best_t, best_el = aos, aos, mask
            while True:
                tt += 10
                e = _el(sat, obs, tt)
                if e > best_el:
                    best_el, best_t = e, tt
                if e < mask or tt - aos > 2700:
                    break
            los = _bisect(sat, obs, mask, tt - 10, tt) if e < mask else tt
            a, b = max(aos, best_t - 10), min(los, best_t + 10)
            for _ in range(20):
                m1, m2 = a + (b - a) / 3, b - (b - a) / 3
                if _el(sat, obs, m1) < _el(sat, obs, m2):
                    a = m1
                else:
                    b = m2
            tca = 0.5 * (a + b)
            lk_a, lk_t, lk_l = look(sat, obs, aos), look(sat, obs, tca), look(sat, obs, los)
            p = Pass(entry, index, aos, tca, los, max(lk_t.el, best_el), lk_a.az, lk_t.az, lk_l.az)
            x = aos
            while x <= los:
                lk = look(sat, obs, x)
                p.track.append((x, lk.az, lk.el, lk.rr))
                x += track_step
            lk = look(sat, obs, los)
            p.track.append((los, lk.az, lk.el, lk.rr))
            passes.append(p)
            t = los + 60
            el = _el(sat, obs, t)
        else:
            t, el = t2, el2
    return passes


def predict_all(entries: List[SatEntry], obs: Observer, start: float, hours: float,
                mask: float = 0.0, only_enabled: bool = True) -> List[Pass]:
    out: List[Pass] = []
    for i, e in enumerate(entries):
        if (only_enabled and not e.enabled) or e.deep_space:
            continue
        out.extend(predict_passes(e, i, obs, start, start + hours * 3600.0, mask))
    out.sort(key=lambda p: p.aos)
    return out


def doppler_hz(f_mhz: float, rr_kms: float) -> float:
    return -f_mhz * 1e6 * rr_kms / C_KMS


# ============================================================================
#  Firmware database (satdb.bin)
# ============================================================================

def _pad(s: str, n: int) -> bytes:
    b = s.encode("ascii", "replace")[:n]
    return b + b"\0" * (n - len(b))


def pack_db(entries: List[SatEntry], passes: List[Pass], obs: Optional[Observer],
            mask: float, created: Optional[float] = None) -> bytes:
    created = int(created if created is not None else time.time())
    sats = b""
    for e in entries[:SAT_DB_MAX_SATS]:
        s = e.tle.satrec()
        flags = (FLAG_ENABLED if e.enabled else 0) | (FLAG_FAVORITE if e.favorite else 0)
        flags |= FLAG_DEEP if e.deep_space else 0
        flags |= FLAG_SCHED if "scheduled" in e.flags else 0
        flags |= FLAG_SUNLIT if "sunlit_only" in e.flags else 0
        sats += struct.pack(
            SAT_FMT, _pad(e.name.upper(), 12), e.norad,
            s.jdsatepoch + s.jdsatepochF, s.bstar, math.degrees(s.inclo), math.degrees(s.nodeo),
            s.ecco, math.degrees(s.argpo), math.degrees(s.mo), s.no_kozai * 1440.0 / (2 * math.pi),
            int(round(e.downlink_mhz * 1e6)), int(round(e.uplink_mhz * 1e6)) if e.mode != "RX_ONLY" else 0,
            int(round(e.ctcss_up * 10)), int(round(e.ctcss_down * 10)), int(round(e.arm_tone * 10)),
            MODES.get(e.mode, 0), flags, int(e.passband_khz * 1000), _pad(e.info, 24), b"\0" * 4)
    max_passes = min(SAT_DB_MAX_PASSES,
                     (SAT_DB_FLASH_SIZE - 64 - len(sats)) // 16)
    plist = [p for p in passes if p.index < len(entries)][:max_passes]
    pas = b"".join(struct.pack(PASS_FMT, int(round(p.aos)), int(round(p.los - p.aos)),
                               int(round(p.tca - p.aos)), p.index, int(round(max(0, min(90, p.max_el)))),
                               int(round(p.aos_az)) % 360, int(round(p.los_az)) % 360,
                               int(round(p.tca_az)) % 360) for p in plist)
    payload = sats + pas
    p_start = int(plist[0].aos) if plist else 0
    p_end = int(max(p.los for p in plist)) if plist else 0
    hdr_wo = struct.pack(HDR_FMT, SAT_DB_MAGIC, SAT_DB_VERSION, 64, len(sats) // 128, 128,
                         len(plist), 16, created,
                         int(round(obs.lat * 1e6)) if obs else 0, int(round(obs.lon * 1e6)) if obs else 0,
                         int(round(obs.alt_m)) if obs else 0, int(round(mask)), 1 if obs else 0,
                         p_start, p_end, _pad(obs.locator if obs else "", 8),
                         zlib.crc32(payload) & 0xFFFFFFFF, b"\0" * 8, 0)
    hdr = hdr_wo[:60] + struct.pack("<I", zlib.crc32(hdr_wo[:60]) & 0xFFFFFFFF)
    blob = hdr + payload
    assert len(blob) <= SAT_DB_FLASH_SIZE
    return blob


def unpack_db(blob: bytes) -> dict:
    h = struct.unpack(HDR_FMT, blob[:64])
    keys = ["magic", "version", "header_size", "sat_count", "sat_rec_size", "pass_count",
            "pass_rec_size", "created", "lat_e6", "lon_e6", "alt", "min_el", "flags",
            "pass_start", "pass_end", "locator", "payload_crc", "reserved", "header_crc"]
    hd = dict(zip(keys, h))
    assert hd["magic"] == SAT_DB_MAGIC, "bad magic"
    assert zlib.crc32(blob[:60]) & 0xFFFFFFFF == hd["header_crc"], "bad header CRC"
    n, m = hd["sat_count"], hd["pass_count"]
    payload = blob[64:64 + n * 128 + m * 16]
    assert zlib.crc32(payload) & 0xFFFFFFFF == hd["payload_crc"], "bad payload CRC"
    sats = [struct.unpack(SAT_FMT, payload[i * 128:(i + 1) * 128]) for i in range(n)]
    passes = [struct.unpack(PASS_FMT, payload[n * 128 + i * 16:n * 128 + (i + 1) * 16]) for i in range(m)]
    return {"header": hd, "sats": sats, "passes": passes}


# ============================================================================
#  Serial upload (custom firmware CPS extension)
# ============================================================================

def crc16_ccitt(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(cmd: int, payload: bytes = b"") -> bytes:
    f = bytes([0xA5, 0xFF, 0xFF, 0xFF, cmd, len(payload)]) + payload
    c = crc16_ccitt(f)
    return f + bytes([c >> 8, c & 0xFF])


class RadioLink:
    """A5-framed protocol spoken by the custom firmware (src/app/cps.c)."""

    def __init__(self, port, baud=115200, timeout=2.0, log=print, ser=None):
        self.log = log
        if ser is None:
            import serial  # pyserial
            ser = serial.Serial(port, baud, timeout=0.05)
        self.ser = ser
        self.timeout = timeout

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _read_exact(self, n, deadline):
        buf = b""
        while len(buf) < n:
            if time.time() > deadline:
                raise TimeoutError("radio did not answer")
            chunk = self.ser.read(n - len(buf))
            if chunk:
                buf += chunk
        return buf

    def read_frame(self, timeout=None):
        deadline = time.time() + (timeout or self.timeout)
        while True:
            b = self._read_exact(1, deadline)
            if b[0] != 0xA5:
                continue
            hdr = b + self._read_exact(5, deadline)
            n = hdr[5]
            rest = self._read_exact(n + 2, deadline)
            body = hdr + rest[:n]
            if crc16_ccitt(body) == (rest[n] << 8 | rest[n + 1]):
                return hdr[4], rest[:n]

    def cmd(self, c, payload=b"", expect=None, timeout=None):
        self.ser.write(frame(c, payload))
        rc, data = self.read_frame(timeout)
        if expect is not None and rc != expect:
            raise IOError("unexpected reply 0x%02X to 0x%02X" % (rc, c))
        return data

    def handshake(self):
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass
        self.ser.write(b"PROGRAMBT9000U")
        rc, data = self.read_frame(timeout=3.0)
        model = data.decode("ascii", "replace").strip()
        self.log("  radio model: %r" % model)
        time.sleep(0.1)
        info = self.cmd(ord("I"), expect=ord("I"), timeout=2.0)
        if info[:3] != b"SAT":
            raise IOError("firmware without satellite support")
        addr = (info[4] << 16) | (info[5] << 8) | info[6]
        self.log("  satellite API v%d, DB @0x%06X (%d KB), %d sats loaded" %
                 (info[3], addr, info[7], info[8]))
        return addr, info[7] * 1024

    def write_block(self, addr: int, data: bytes, progress=None):
        total = len(data)
        for sec in range(0, total, 4096):
            a = addr + sec
            self.cmd(ord("E"), bytes([a >> 16 & 0xFF, a >> 8 & 0xFF, a & 0xFF]), expect=ord("E"), timeout=3.0)
            for off in range(sec, min(sec + 4096, total), 128):
                chunk = data[off:off + 128]
                a2 = addr + off
                self.cmd(ord("W"), bytes([a2 >> 16 & 0xFF, a2 >> 8 & 0xFF, a2 & 0xFF]) + chunk,
                         expect=ord("W"))
                if progress:
                    progress(off + len(chunk), total)

    def read_block(self, addr: int, length: int) -> bytes:
        out = b""
        while len(out) < length:
            n = min(128, length - len(out))
            a = addr + len(out)
            out += self.cmd(ord("R"), bytes([a >> 16 & 0xFF, a >> 8 & 0xFF, a & 0xFF, n]), expect=ord("R"))
        return out

    def set_time(self, unix_s: Optional[int] = None):
        unix_s = int(unix_s if unix_s is not None else time.time())
        self.cmd(ord("K"), struct.pack("<I", unix_s), expect=ord("K"))

    def reload(self) -> int:
        r = self.cmd(ord("L"), expect=ord("L"))
        return -1 if r[0] == 0xFF else r[0]

    def finish(self):
        self.ser.write(frame(ord("X")))


def upload_db(port: str, blob: bytes, log=print, ser=None, verify=True, set_time=True) -> int:
    link = RadioLink(port, log=log, ser=ser)
    try:
        addr, size = link.handshake()
        if addr != SAT_DB_FLASH_ADDR or len(blob) > size:
            raise IOError("unexpected DB window 0x%06X/%d" % (addr, size))
        padded = blob + b"\xFF" * ((-len(blob)) % 128)
        last = [0]

        def prog(done, total):
            pct = done * 100 // total
            if pct >= last[0] + 10 or done == total:
                last[0] = pct
                log("  writing %3d%%" % pct)
        link.write_block(addr, padded, prog)
        if verify:
            back = link.read_block(addr, len(padded))
            if back != padded:
                raise IOError("verify failed: flash content differs")
            log("  verify OK (%d bytes)" % len(padded))
        if set_time:
            link.set_time()
            log("  radio clock set to %s UTC" % dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S"))
        n = link.reload()
        log("  radio reloaded: %d satellites" % n)
        link.finish()
        return n
    finally:
        link.close()


# ============================================================================
#  Stock firmware: Doppler-stepped split channels (CHIRP CSV)
# ============================================================================

CHIRP_HEADER = ["Location", "Name", "Frequency", "Duplex", "Offset", "Tone", "rToneFreq",
                "cToneFreq", "DtcsCode", "DtcsPolarity", "RxDtcsCode", "CrossMode", "Mode",
                "TStep", "Skip", "Power", "Comment", "URCALL", "RPT1CALL", "RPT2CALL", "DVCODE"]


def max_doppler_hz(entry: SatEntry, f_mhz: float) -> float:
    """Doppler at 0 deg elevation for a circular orbit at the satellite's height."""
    s = entry.tle.satrec()
    n = s.no_kozai / 60.0                         # rad/s
    a = (398600.8 / n ** 2) ** (1 / 3)            # km
    v = math.sqrt(398600.8 / a)
    rr = v * 6378.0 / a                           # radial component at the horizon
    return f_mhz * 1e6 * rr / C_KMS


def doppler_steps(entry: SatEntry, n: int = 5) -> List[Tuple[str, int, int]]:
    """Return [(label, rx_offset_hz, tx_offset_hz)] from AOS to LOS."""
    dmax = max_doppler_hz(entry, entry.downlink_mhz)
    step_rx = 1000 * round(dmax * 2 / (n - 1) / 1000.0 * 2) / 2   # 0.5 kHz grid
    out = []
    labels = ["AOS", "A2", "TCA", "L2", "LOS"] if n == 5 else ["S%d" % (i + 1) for i in range(n)]
    for i in range(n):
        k = (n - 1) / 2.0 - i                     # +2..-2  (approaching -> receding)
        rx = int(round(k * step_rx / 100.0) * 100)
        ratio = entry.uplink_mhz / entry.downlink_mhz if entry.downlink_mhz else 0.0
        tx = int(round(-rx * ratio / 100.0) * 100)
        out.append((labels[i], rx, tx))
    return out


def chirp_rows(entries: List[SatEntry], start_loc: int, steps: int = 5) -> List[List[str]]:
    rows = []
    loc = start_loc
    for e in entries:
        if not e.enabled or e.mode in ("LIN_INV", "LIN") or not e.downlink_mhz:
            continue
        short = e.label
        tone_mode = "Tone" if e.ctcss_up and e.can_tx else ""
        r_tone = "%.1f" % (e.ctcss_up or 88.5)
        c_tone = "%.1f" % (e.ctcss_down or 88.5)
        if e.ctcss_down:
            tone_mode = "TSQL" if e.ctcss_up == e.ctcss_down else ("Cross" if e.ctcss_up else "TSQL")
        for label, rxo, txo in doppler_steps(e, steps):
            rx = e.downlink_mhz + rxo / 1e6
            tx = e.uplink_mhz + txo / 1e6
            duplex, offset = ("split", "%.6f" % tx) if e.can_tx else ("off", "0.000000")
            rows.append([str(loc), ("%s %s" % (short, label))[:12], "%.6f" % rx, duplex, offset,
                         tone_mode, r_tone, c_tone, "023", "NN", "023", "Tone->Tone", "FM", "5.00", "",
                         "High", "%s Doppler %s (RX %+.1fk TX %+.1fk)" % (e.name, label, rxo / 1e3, txo / 1e3),
                         "", "", "", ""])
            loc += 1
        if e.arm_tone and e.can_tx:
            rows.append([str(loc), ("%s ARM" % short)[:12], "%.6f" % e.downlink_mhz, "split",
                         "%.6f" % e.uplink_mhz, "Tone", "%.1f" % e.arm_tone, "88.5", "023", "NN", "023",
                         "Tone->Tone", "FM", "5.00", "", "High",
                         "%s arming tone %.1f Hz: key 2 s, then use the Doppler channels" % (e.name, e.arm_tone),
                         "", "", "", ""])
            loc += 1
    return rows


def write_chirp_csv(path: str, rows: List[List[str]]):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(CHIRP_HEADER)
        w.writerows(rows)


def channel_schedule(p: Pass, steps: int = 5) -> List[Tuple[float, str, float]]:
    """(time, channel label, rx doppler kHz) whenever the best channel changes."""
    table = doppler_steps(p.sat, steps)
    sched = []
    cur = None
    for t, az, el, rr in p.track:
        d = doppler_hz(p.sat.downlink_mhz, rr)
        best = min(table, key=lambda x: abs(x[1] - d))
        if best[0] != cur:
            sched.append((t, best[0], d / 1000.0))
            cur = best[0]
    return sched


# ============================================================================
#  Reports
# ============================================================================

def tzinfo_for(name: str):
    try:
        from zoneinfo import ZoneInfo
        return ZoneInfo(name)
    except Exception:
        off = -time.timezone if time.localtime().tm_isdst == 0 else -time.altzone
        return dt.timezone(dt.timedelta(seconds=off))


def fmt_t(t: float, tz, f="%H:%M:%S") -> str:
    return dt.datetime.fromtimestamp(t, tz).strftime(f)


_DIAS = ["lun", "mar", "mié", "jue", "vie", "sáb", "dom"]
_MESES = ["ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"]


def fmt_day_es(t: float, tz) -> str:
    """'vie 02 oct' independent of the OS locale."""
    d = dt.datetime.fromtimestamp(t, tz)
    return "%s %02d %s" % (_DIAS[d.weekday()], d.day, _MESES[d.month - 1])


def write_passes_csv(path: str, passes: List[Pass], tz):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["satellite", "norad", "aos_utc", "tca_utc", "los_utc", "aos_local", "duration_s",
                    "max_el_deg", "aos_az", "tca_az", "los_az", "downlink_mhz", "uplink_mhz",
                    "ctcss_up", "rx_doppler_aos_khz", "rx_doppler_los_khz"])
        for p in passes:
            w.writerow([p.sat.name, p.sat.norad, fmt_t(p.aos, dt.timezone.utc, "%Y-%m-%d %H:%M:%S"),
                        fmt_t(p.tca, dt.timezone.utc, "%H:%M:%S"), fmt_t(p.los, dt.timezone.utc, "%H:%M:%S"),
                        fmt_t(p.aos, tz, "%Y-%m-%d %H:%M:%S"), int(p.duration), "%.1f" % p.max_el,
                        int(p.aos_az), int(p.tca_az), int(p.los_az), "%.4f" % p.sat.downlink_mhz,
                        "%.4f" % p.sat.uplink_mhz, p.sat.ctcss_up or "",
                        "%.1f" % (doppler_hz(p.sat.downlink_mhz, p.track[0][3]) / 1e3),
                        "%.1f" % (doppler_hz(p.sat.downlink_mhz, p.track[-1][3]) / 1e3)])


def write_ics(path: str, passes: List[Pass], min_el: float = 15.0):
    now = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    lines = ["BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//rt950_sat//EN", "CALSCALE:GREGORIAN"]
    for p in passes:
        if p.max_el < min_el:
            continue
        uid = "%s-%d@rt950sat" % (p.sat.norad, int(p.aos))
        desc = ("Max %.0f deg, AZ %03d>%03d. RX %.3f MHz TX %.3f MHz%s" %
                (p.max_el, p.aos_az, p.los_az, p.sat.downlink_mhz, p.sat.uplink_mhz,
                 (" CTCSS %.1f" % p.sat.ctcss_up) if p.sat.ctcss_up else ""))
        lines += ["BEGIN:VEVENT", "UID:" + uid, "DTSTAMP:" + now,
                  "DTSTART:" + fmt_t(p.aos, dt.timezone.utc, "%Y%m%dT%H%M%SZ"),
                  "DTEND:" + fmt_t(p.los, dt.timezone.utc, "%Y%m%dT%H%M%SZ"),
                  "SUMMARY:%s pass %.0f deg" % (p.sat.name, p.max_el), "DESCRIPTION:" + desc,
                  "BEGIN:VALARM", "TRIGGER:-PT5M", "ACTION:DISPLAY", "DESCRIPTION:%s AOS" % p.sat.name,
                  "END:VALARM", "END:VEVENT"]
    lines.append("END:VCALENDAR")
    with open(path, "w", newline="\r\n", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def polar_svg(p: Pass, size: int = 120) -> str:
    c = size / 2
    r = c - 10

    def xy(az, el):
        rr = r * (90 - max(0, el)) / 90
        return c + rr * math.sin(math.radians(az)), c - rr * math.cos(math.radians(az))
    pts = " ".join("%.1f,%.1f" % xy(az, el) for _, az, el, _ in p.track)
    ax, ay = xy(p.aos_az, 0)
    lx, ly = xy(p.los_az, 0)
    tx, ty = xy(p.tca_az, p.max_el)
    return ('<svg viewBox="0 0 {s} {s}" width="{s}" height="{s}" role="img" aria-label="sky track">'
            '<circle cx="{c}" cy="{c}" r="{r}" class="g"/><circle cx="{c}" cy="{c}" r="{r2:.1f}" class="g2"/>'
            '<circle cx="{c}" cy="{c}" r="{r3:.1f}" class="g2"/>'
            '<line x1="{c}" y1="{t}" x2="{c}" y2="{b}" class="g2"/><line x1="{t}" y1="{c}" x2="{b}" y2="{c}" class="g2"/>'
            '<text x="{c}" y="8" class="lbl">N</text>'
            '<polyline points="{pts}" class="trk"/>'
            '<circle cx="{ax:.1f}" cy="{ay:.1f}" r="3" class="aos"/><circle cx="{lx:.1f}" cy="{ly:.1f}" r="3" class="los"/>'
            '<circle cx="{tx:.1f}" cy="{ty:.1f}" r="2.5" class="tca"/></svg>').format(
        s=size, c=c, r=r, r2=r * 2 / 3, r3=r / 3, t=c - r, b=c + r, pts=pts,
        ax=ax, ay=ay, lx=lx, ly=ly, tx=tx, ty=ty)


def write_html(path: str, passes: List[Pass], entries: List[SatEntry], obs: Observer, tz, tzname: str,
               hours: float, mask: float, steps: int = 5):
    gen = dt.datetime.now(tz).strftime("%Y-%m-%d %H:%M %Z")
    rows = []
    for p in passes:
        e = p.sat
        sched = ""
        if e.mode in ("FM", "FM_DATA") and e.downlink_mhz:
            short = e.label
            sched = "".join("<li><b>%s</b> %s <span class=m>(%+.1f kHz)</span></li>" %
                            (fmt_t(t, tz, "%H:%M:%S"), html.escape("%s %s" % (short, lab)), d)
                            for t, lab, d in channel_schedule(p, steps))
            sched = "<ul class=sch>%s</ul>" % sched
        qual = "hi" if p.max_el >= 45 else "mid" if p.max_el >= 20 else "lo"
        rows.append(
            "<article class='pass {q}'><div class=plot>{svg}</div><div class=info>"
            "<h3>{name} <span class=tag>{mode}</span> <span class=el>{el:.0f}&deg;</span></h3>"
            "<p class=t><b>{d}</b> &nbsp; AOS {aos} &middot; TCA {tca} &middot; LOS {los} "
            "<span class=m>({dur} min, {tzn})</span></p>"
            "<p>AZ {aaz:03.0f}&deg; &rarr; {taz:03.0f}&deg; &rarr; {laz:03.0f}&deg; &nbsp; "
            "RX <b>{dn:.3f}</b> &nbsp; TX <b>{up}</b>{tone}{arm}</p>"
            "<p class=m>Doppler RX {da:+.1f} &rarr; {dl:+.1f} kHz &middot; UTC {aosu}&ndash;{losu}</p>"
            "{sched}</div></article>".format(
                q=qual, svg=polar_svg(p), name=html.escape(e.name), mode=e.mode, el=p.max_el,
                d=fmt_day_es(p.aos, tz), aos=fmt_t(p.aos, tz), tca=fmt_t(p.tca, tz),
                los=fmt_t(p.los, tz), dur=int(round(p.duration / 60)), tzn=html.escape(tzname),
                aaz=p.aos_az, taz=p.tca_az, laz=p.los_az, dn=e.downlink_mhz,
                up=("%.3f" % e.uplink_mhz) if e.can_tx else "&mdash;",
                tone=(" &nbsp; CTCSS %.1f" % e.ctcss_up) if e.ctcss_up else "",
                arm=(" &nbsp; <span class=warn>arm %.1f Hz</span>" % e.arm_tone) if e.arm_tone else "",
                da=doppler_hz(e.downlink_mhz, p.track[0][3]) / 1e3,
                dl=doppler_hz(e.downlink_mhz, p.track[-1][3]) / 1e3,
                aosu=fmt_t(p.aos, dt.timezone.utc, "%H:%M"), losu=fmt_t(p.los, dt.timezone.utc, "%H:%M"),
                sched=sched))
    sats_tbl = "".join(
        "<tr><td>{n}</td><td>{no}</td><td>{m}</td><td>{dn:.3f}</td><td>{up}</td><td>{t}</td>"
        "<td>{age:.1f} d</td><td>{inf}</td></tr>".format(
            n=html.escape(e.name), no=e.norad, m=e.mode, dn=e.downlink_mhz,
            up=("%.3f" % e.uplink_mhz) if e.uplink_mhz else "&mdash;",
            t=("%.1f" % e.ctcss_up) if e.ctcss_up else "&mdash;", age=e.tle_age_days,
            inf=html.escape(e.info)) for e in entries)
    doc = """<!doctype html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Pases de satélites</title>
<style>
:root{{--bg:#f6f7f9;--card:#fff;--fg:#1c2330;--mut:#5c6675;--line:#d9dee6;--acc:#0b6bcb;--ok:#1a7f37;--bad:#c9302c;--warn:#b26a00}}
@media (prefers-color-scheme:dark){{:root{{--bg:#0f1318;--card:#171c23;--fg:#e6e9ee;--mut:#97a1b0;--line:#2a313b;--acc:#58a6ff;--ok:#3fb950;--bad:#f85149;--warn:#d29922}}}}
body{{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}}
main{{max-width:980px;margin:auto;padding:16px}} h1{{font-size:1.5rem;margin:.2em 0}} h2{{font-size:1.1rem;margin-top:1.6em}}
.m{{color:var(--mut)}} .warn{{color:var(--warn)}}
.pass{{display:flex;gap:14px;background:var(--card);border:1px solid var(--line);border-left:4px solid var(--mut);border-radius:8px;padding:10px 12px;margin:10px 0}}
.pass.hi{{border-left-color:var(--ok)}} .pass.mid{{border-left-color:var(--acc)}}
.pass h3{{margin:0 0 4px;font-size:1.05rem}} .pass p{{margin:2px 0}} .tag{{font-size:.75rem;border:1px solid var(--line);border-radius:4px;padding:0 4px;color:var(--mut)}}
.el{{float:right;font-weight:700}} .info{{flex:1;min-width:0}}
.sch{{list-style:none;padding:0;margin:6px 0 0;display:flex;flex-wrap:wrap;gap:4px 14px;font-size:.88rem}}
svg .g{{fill:none;stroke:var(--mut)}} svg .g2{{fill:none;stroke:var(--line)}} svg .trk{{fill:none;stroke:var(--acc);stroke-width:2}}
svg .aos{{fill:var(--ok)}} svg .los{{fill:var(--bad)}} svg .tca{{fill:var(--warn)}} svg .lbl{{fill:var(--mut);font-size:9px;text-anchor:middle}}
table{{border-collapse:collapse;width:100%;font-size:.9rem;background:var(--card)}} td,th{{border-bottom:1px solid var(--line);padding:4px 6px;text-align:left}}
.wrap{{overflow-x:auto}}
@media (max-width:560px){{.pass{{flex-direction:column}} .plot svg{{width:100px;height:100px}}}}
</style></head><body><main>
<h1>Pases de satélites &middot; {loc}</h1>
<p class=m>QTH {lat:.4f}, {lon:.4f} &middot; máscara {mask:.0f}&deg; &middot; próximas {hours:.0f} h &middot; generado {gen} &middot; {n} pases</p>
<p class=m>Canales Doppler (firmware original): usa el canal indicado a cada hora (los nombres coinciden con el CSV de canales). Verde = AOS, rojo = LOS.</p>
{rows}
<h2>Satélites incluidos</h2><div class=wrap><table><tr><th>Nombre</th><th>NORAD</th><th>Modo</th><th>Bajada</th><th>Subida</th><th>CTCSS</th><th>Edad TLE</th><th>Notas</th></tr>{sats}</table></div>
<p class=m>Generado por rt950_sat.py {ver}. Comprueba el estado de cada satélite en AMSAT antes de transmitir.</p>
</main></body></html>""".format(loc=obs.locator, lat=obs.lat, lon=obs.lon, mask=mask, hours=hours, gen=gen,
                                n=len(passes), rows="\n".join(rows) or "<p>No hay pases.</p>",
                                sats=sats_tbl, ver=__version__)
    with open(path, "w", encoding="utf-8") as f:
        f.write(doc)


# ============================================================================
#  CLI
# ============================================================================

def make_observer(args) -> Observer:
    if args.lat is not None and args.lon is not None:
        return Observer(args.lat, args.lon, args.alt)
    lat, lon = locator_to_latlon(args.locator)
    return Observer(lat, lon, args.alt)


def get_tles(args, log=print) -> Dict[int, Tle]:
    cache = os.path.join(args.out, "tle_cache.txt")
    if args.tle_file:
        t = load_tles(args.tle_file)
        log("TLE file %s: %d sets" % (args.tle_file, len(t)))
        return t
    if not args.offline:
        log("Downloading TLEs...")
        t = fetch_tles(cache, log=log)
        if t:
            return t
    if os.path.exists(cache):
        t = load_tles(cache)
        log("Using cached TLEs %s (%d sets)" % (cache, len(t)))
        return t
    raise SystemExit("No TLE data: check the internet connection or pass --tle-file")


def run_pipeline(args, log=print) -> dict:
    os.makedirs(args.out, exist_ok=True)
    tles = get_tles(args, log)
    entries = build_entries(load_freqdb(args.freqs), tles, log=log)
    log("%d satellites with frequencies + fresh TLE" % len(entries))
    obs = make_observer(args)
    tz = tzinfo_for(args.tz)
    start = time.time()
    log("Predicting passes for %s (%.4f, %.4f), %d h, mask %.0f deg..." %
        (obs.locator, obs.lat, obs.lon, args.hours, args.min_el))
    passes = predict_all(entries, obs, start, args.hours, args.min_el)
    res = {"entries": entries, "passes": passes, "observer": obs, "files": []}

    def out(name):
        p = os.path.join(args.out, name)
        res["files"].append(p)
        return p
    write_html(out("pases_satelites.html"), passes, entries, obs, tz, args.tz, args.hours, args.min_el)
    write_passes_csv(out("pases_satelites.csv"), passes, tz)
    write_ics(out("pases_satelites.ics"), passes, args.ics_min_el)
    rows = chirp_rows(entries, args.start_channel)
    write_chirp_csv(out("canales_satelite_chirp.csv"), rows)
    blob = pack_db(entries, [p for p in passes if p.aos < start + args.db_hours * 3600], obs, args.min_el)
    with open(out("satdb.bin"), "wb") as f:
        f.write(blob)
    res["blob"] = blob
    log("Wrote %d passes, %d Doppler channels, satdb.bin %d bytes -> %s" %
        (len(passes), len(rows), len(blob), args.out))
    return res


def print_passes(passes: List[Pass], tz, limit: int = 40):
    print("%-10s %-10s %-8s %-8s %5s %4s %-9s" % ("SAT", "DATE", "AOS", "LOS", "MIN", "MAX", "AZ"))
    for p in passes[:limit]:
        print("%-10s %-10s %-8s %-8s %5.1f %3.0f° %03.0f>%03.0f" % (
            p.sat.name, fmt_t(p.aos, tz, "%Y-%m-%d"), fmt_t(p.aos, tz), fmt_t(p.los, tz),
            p.duration / 60, p.max_el, p.aos_az, p.los_az))


def add_common(sp):
    sp.add_argument("--locator", default=DEFAULT_LOCATOR, help="Maidenhead locator (default %(default)s)")
    sp.add_argument("--lat", type=float, help="latitude (overrides locator)")
    sp.add_argument("--lon", type=float, help="longitude, east positive")
    sp.add_argument("--alt", type=float, default=0.0, help="altitude in metres")
    sp.add_argument("--hours", type=float, default=48.0, help="prediction window (h)")
    sp.add_argument("--min-el", type=float, default=0.0, help="horizon mask (deg)")
    sp.add_argument("--tz", default=DEFAULT_TZ, help="time zone for reports (%(default)s)")
    sp.add_argument("--freqs", default=DEFAULT_FREQS, help="frequency database JSON")
    sp.add_argument("--tle-file", help="use this TLE file instead of downloading")
    sp.add_argument("--offline", action="store_true", help="do not download, use cache")
    sp.add_argument("--out", default=DEFAULT_OUT, help="output folder (%(default)s)")
    sp.add_argument("--start-channel", type=int, default=900, help="first CHIRP memory number")
    sp.add_argument("--db-hours", type=float, default=72.0, help="hours of passes stored in satdb.bin")
    sp.add_argument("--ics-min-el", type=float, default=15.0, help="calendar: only passes above (deg)")


def main(argv=None):
    ap = argparse.ArgumentParser(description="RT-950 / RT-950 Pro satellite tool v" + __version__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, hlp in [("all", "download, predict, write reports/CSV/satdb.bin (and upload with --port)"),
                      ("passes", "print and write the pass report"),
                      ("chirp", "write Doppler channels CSV for the stock firmware"),
                      ("build", "write satdb.bin for the custom firmware")]:
        sp = sub.add_parser(name, help=hlp)
        add_common(sp)
        if name == "all":
            sp.add_argument("--port", help="serial port: also upload to the radio (custom firmware)")
    sp = sub.add_parser("fetch", help="download TLEs into the cache")
    sp.add_argument("--out", default=DEFAULT_OUT)
    sp = sub.add_parser("upload", help="upload satdb.bin to a radio running the custom firmware")
    sp.add_argument("--port", required=True)
    sp.add_argument("--db", default=os.path.join(DEFAULT_OUT, "satdb.bin"))
    sp.add_argument("--no-verify", action="store_true")
    sp = sub.add_parser("settime", help="set the radio UTC clock from this PC")
    sp.add_argument("--port", required=True)
    sp = sub.add_parser("dump", help="decode a satdb.bin")
    sp.add_argument("db")
    sub.add_parser("gui", help="graphical interface (tkinter)")
    args = ap.parse_args(argv)

    if args.cmd == "fetch":
        n = fetch_tles(os.path.join(args.out, "tle_cache.txt"))
        print("%d element sets cached" % len(n))
    elif args.cmd in ("all", "passes", "chirp", "build"):
        res = run_pipeline(args)
        if args.cmd in ("all", "passes"):
            print_passes(res["passes"], tzinfo_for(args.tz))
        if args.cmd == "all" and args.port:
            upload_db(args.port, res["blob"])
        for f in res["files"]:
            print("  ->", f)
    elif args.cmd == "upload":
        with open(args.db, "rb") as f:
            blob = f.read()
        unpack_db(blob)  # validates CRCs
        upload_db(args.port, blob, verify=not args.no_verify)
    elif args.cmd == "settime":
        link = RadioLink(args.port)
        try:
            link.handshake()
            link.set_time()
            link.finish()
            print("clock set")
        finally:
            link.close()
    elif args.cmd == "dump":
        with open(args.db, "rb") as f:
            d = unpack_db(f.read())
        h = d["header"]
        print("satdb v%d: %d sats, %d passes, QTH %s (%.4f, %.4f), built %s UTC" % (
            h["version"], h["sat_count"], h["pass_count"], h["locator"].rstrip(b"\0").decode(),
            h["lat_e6"] / 1e6, h["lon_e6"] / 1e6, dt.datetime.fromtimestamp(h["created"], dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")))
        for s in d["sats"]:
            print("  %-12s %6d  dn %9.4f up %9.4f  ctcss %5.1f arm %4.1f mode %d flags 0x%02x" % (
                s[0].rstrip(b"\0").decode(), s[1], s[10] / 1e6, s[11] / 1e6, s[12] / 10, s[14] / 10, s[15], s[16]))
    elif args.cmd == "gui":
        from .gui import run_gui
        run_gui(start_tab="satellites")
    return 0


if __name__ == "__main__":
    sys.exit(main())
