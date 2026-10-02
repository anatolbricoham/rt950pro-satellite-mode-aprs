"""
country.py - Preloaded country codeplugs

Each country file (data/countries/<cc>.json, built by
tools/build_country_data.py) describes the 10 zones of the radio:

  Spain (ES)   zones EA1..EA9 = amateur FM repeaters of the call district
               (URE list) + air band frequencies of the airports in it
               (RX only, AM); zone 10 = PMR446 + CB-27 (RX only).
  UK (GB)      zones = the 8 ETCC repeater regions with their FM repeaters
               (ukrepeater.net) and airports; zone 9 = simplex, APRS, ISS;
               zone 10 = PMR446 + UK CB 27/81 + CEPT CB (RX only).

apply() loads a country into a codeplug either replacing every channel
("overwrite", for a new radio) or adding to the channels already there
("append", for a configured radio: existing channels are kept, duplicates
are skipped and new ones go into the free slots of the same zone).

A zone holds 99 channels. When a zone has more candidates than room, the
ones closest to the operator's locator are kept (or to the middle of the
zone when no locator is given); main airports are favoured over airfields.
"""

from __future__ import annotations

import json
import math
import os
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple

from .codeplug import Channel, Codeplug, CH_PER_ZONE, ZONES

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data", "countries")
KINDS = ("repeater", "airport", "simplex", "pmr", "cb")
ORDER = {"repeater": 0, "simplex": 1, "airport": 2, "pmr": 3, "cb": 4}
DEFAULT_ZONE_NAMES = {"", "ZONEONE", "ZONETWO", "ZONETHREE", "ZONEFOUR", "ZONEFIVE", "ZONESIX",
                      "ZONESEVEN", "ZONEEIGHT", "ZONENINE", "ZONETEN"}


def available() -> List[Tuple[str, Dict[str, str]]]:
    out = []
    for f in sorted(os.listdir(DATA)):
        if f.endswith(".json"):
            d = load(f[:-5])
            out.append((d["country"], d["title"]))
    return out


def load(code: str) -> dict:
    with open(os.path.join(DATA, code.lower() + ".json"), encoding="utf-8") as f:
        return json.load(f)


def locator_to_latlon(loc: str) -> Tuple[float, float]:
    loc = loc.strip()
    if len(loc) < 4:
        raise ValueError("locator needs at least 4 characters")
    loc = loc[:2].upper() + loc[2:4] + loc[4:6].lower()
    lon = (ord(loc[0]) - 65) * 20.0 - 180.0 + int(loc[2]) * 2.0
    lat = (ord(loc[1]) - 65) * 10.0 - 90.0 + int(loc[3]) * 1.0
    if len(loc) >= 6:
        return lat + (ord(loc[5]) - 97) * 2.5 / 60 + 1.25 / 60, lon + (ord(loc[4]) - 97) * 5 / 60 + 2.5 / 60
    return lat + 0.5, lon + 1.0


def km(a, b) -> float:
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 6371.0 * 2 * math.asin(math.sqrt(h))


@dataclass
class ZoneReport:
    zone: int
    name: str
    added: int = 0
    duplicates: int = 0
    dropped: List[str] = field(default_factory=list)


@dataclass
class Report:
    country: str
    mode: str
    zones: List[ZoneReport] = field(default_factory=list)

    @property
    def added(self) -> int:
        return sum(z.added for z in self.zones)

    def text(self) -> str:
        lines = ["%s (%s): %d channels" % (self.country, self.mode, self.added)]
        for z in self.zones:
            s = "  %2d %-16s +%d" % (z.zone + 1, z.name, z.added)
            if z.duplicates:
                s += "  (%d already there)" % z.duplicates
            if z.dropped:
                s += "  (%d did not fit: %s%s)" % (len(z.dropped), ", ".join(z.dropped[:4]),
                                                    "..." if len(z.dropped) > 4 else "")
            lines.append(s)
        return "\n".join(lines)


def _score(ch: dict, ref) -> float:
    if ch.get("lat") is None or ref is None:
        return 0.0 if ch["kind"] not in ("repeater", "airport") else 5000.0
    d = km(ref, (ch["lat"], ch["lon"]))
    if ch["kind"] == "airport":
        d -= {"large_airport": 80, "medium_airport": 30}.get(ch.get("airport_type"), 0)
    return d


def _to_channel(idx: int, ch: dict) -> Channel:
    return Channel(idx, ch["rx"], ch["tx"], "OFF", ch.get("tone_tx", "OFF"),
                   power="High" if ch["kind"] in ("repeater", "simplex") else "Low",
                   bandwidth=ch.get("bw", "Wide"), scan_add="ON",
                   tx_enable="OFF" if ch.get("tx_enable") is False else "ON",
                   rx_am="AM" if ch.get("mode") == "AM" else "FM", name=ch["name"][:12])


def select(chans: List[dict], room: int, ref) -> Tuple[List[dict], List[dict]]:
    """Keep at most `room` channels (non-geographic kinds first, then the
    closest), returned in display order."""
    fixed = [c for c in chans if c["kind"] not in ("repeater", "airport")]
    geo = sorted((c for c in chans if c["kind"] in ("repeater", "airport")), key=lambda c: _score(c, ref))
    keep_geo = geo[:max(0, room - len(fixed))]
    kept = fixed[:room] + keep_geo
    dropped = [c for c in chans if c not in kept]

    def order(c):
        if c["kind"] == "repeater":
            return (0, 0 if c.get("band") == "2m" else 1, _score(c, ref) if ref else 0, c["name"])
        if c["kind"] == "airport":
            return (2, _score(c, ref) if ref else 0, c.get("airport", ""), c["name"])
        return (ORDER[c["kind"]], 0, 0, "")
    kept.sort(key=order)
    return kept, dropped


def apply(cp: Codeplug, code: str, mode: str = "overwrite", locator: Optional[str] = None,
          kinds: Iterable[str] = KINDS, rename_zones: Optional[bool] = None) -> Report:
    if mode not in ("overwrite", "append"):
        raise ValueError("mode must be 'overwrite' or 'append'")
    data = load(code)
    kinds = set(kinds)
    ref = locator_to_latlon(locator) if locator else None
    rep = Report(data["country"], mode)
    if rename_zones is None:
        rename_zones = mode == "overwrite"
    if mode == "overwrite":
        for i in range(ZONES * CH_PER_ZONE):
            if not cp.channel(i).empty:
                cp.clear_channel(i)
    for z, zone in enumerate(data["zones"][:ZONES]):
        zr = ZoneReport(z, zone["name"])
        rep.zones.append(zr)
        chans = [c for c in zone["channels"] if c["kind"] in kinds]
        base = z * CH_PER_ZONE
        existing = {(ch.rx_hz, ch.tx_hz, ch.tx_tone) for ch in
                    (cp.channel(i) for i in range(base, base + CH_PER_ZONE)) if not ch.empty}
        fresh = [c for c in chans if (c["rx"], c["tx"], c.get("tone_tx", "OFF")) not in existing]
        zr.duplicates = len(chans) - len(fresh)
        free = [i for i in range(base, base + CH_PER_ZONE) if cp.channel(i).empty]
        zone_ref = ref
        if zone_ref is None:
            pts = [(c["lat"], c["lon"]) for c in fresh if c.get("lat") is not None]
            zone_ref = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)) if pts else None
        kept, dropped = select(fresh, len(free), zone_ref)
        zr.dropped = [c["name"] for c in dropped]
        for slot, c in zip(free, kept):
            cp.set_channel(_to_channel(slot, c))
            zr.added += 1
        current = cp.zone_name(z).replace(" ", "").upper()
        if rename_zones or current in DEFAULT_ZONE_NAMES:
            cp.set_zone_name(z, zone["name"][:16])
    cp.meta.setdefault("presets", []).append({"country": data["country"], "mode": mode,
                                              "locator": locator or "", "data": data.get("generated", "")})
    return rep


def build(code: str, locator: Optional[str] = None, kinds: Iterable[str] = KINDS) -> Tuple[Codeplug, Report]:
    """A fresh codeplug holding only the country channels and zone names
    (settings are never written from it, see protocol.region_data)."""
    cp = Codeplug()
    cp.meta.update({"source": "preset", "country": code.upper()})
    rep = apply(cp, code, "overwrite", locator, kinds)
    return cp, rep
