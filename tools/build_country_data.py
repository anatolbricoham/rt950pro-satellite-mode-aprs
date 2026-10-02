#!/usr/bin/env python3
"""
build_country_data.py - Build the preloaded country codeplug data
(pc/rt950_toolkit/data/countries/*.json) from the source lists kept in
tools/country_sources/.

Sources (see tools/country_sources/README.md):
  * OurAirports (public domain): airports and their radio frequencies
  * URE (Unión de Radioaficionados Españoles) repeater lists, 144 / 432 MHz
  * ukrepeater.net (RSGB ETCC) voice repeater list

    python tools/build_country_data.py
"""

from __future__ import annotations

import csv
import datetime
import json
import math
import os
import re
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "country_sources")
OUT = os.path.join(HERE, "..", "pc", "rt950_toolkit", "data", "countries")

# --------------------------------------------------------------- helpers

def locator_to_latlon(loc: str):
    loc = loc.strip()
    loc = loc[:2].upper() + loc[2:4] + loc[4:6].lower()
    lon = (ord(loc[0]) - 65) * 20.0 - 180.0 + int(loc[2]) * 2.0
    lat = (ord(loc[1]) - 65) * 10.0 - 90.0 + int(loc[3]) * 1.0
    if len(loc) >= 6:
        lon += (ord(loc[4]) - 97) * 5.0 / 60.0 + 2.5 / 60.0
        lat += (ord(loc[5]) - 97) * 2.5 / 60.0 + 1.25 / 60.0
    else:
        lon += 1.0
        lat += 0.5
    return round(lat, 4), round(lon, 4)


def km(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 6371.0 * 2 * math.asin(math.sqrt(h))


def airband_hz(mhz: float) -> int:
    """Air band frequency in Hz. 8.33 kHz *channel designators* (the
    published "frequencies" ending in 5/10/15 kHz inside each 25 kHz block)
    are converted to the real carrier frequency."""
    khz = round(mhz * 1000)
    block, rest = khz - khz % 25, khz % 25
    if rest == 5:
        return block * 1000
    if rest == 10:
        return block * 1000 + 8333
    if rest == 15:
        return block * 1000 + 16667
    return khz * 1000


def ctcss(s: str):
    s = (s or "").strip().replace(",", ".")
    try:
        v = float(s)
    except ValueError:
        return "OFF"
    return "%.1f" % v if 60.0 <= v <= 260.0 else "OFF"


# ---------------------------------------------------------------- airports

KIND = {
    "TWR": "TWR", "twr": "TWR",
    "APP": "APP", "Approach": "APP", "DIR": "APP", "RDR": "APP", "RAD/APP": "APP", "APP/RAD": "APP",
    "APP/DEP": "APP", "DEP": "APP", "RADAR": "APP", "RAD": "APP",
    "ATIS": "ATIS", "ATIS DEP": "ATIS",
    "GND": "GND",
    "AFIS": "INFO", "A/G": "INFO", "A/G / AFIS": "INFO", "INFO": "INFO", "Info": "INFO", "info": "INFO",
    "RDO": "INFO", "RADIO": "INFO", "Radio": "INFO", "CTAF": "INFO", "UNIC": "INFO", "AutoInfo": "INFO",
    "auto informacion": "INFO", "VHF": "INFO", "A/A": "INFO", "A/D": "INFO", "Banda Aèria": "INFO",
}
PRIO = {"TWR": 0, "INFO": 1, "APP": 2, "ATIS": 3, "GND": 4}
PER_AIRPORT = {"large_airport": 6, "medium_airport": 4, "small_airport": 2}


def load_airports(countries):
    ap = {r["ident"]: r for r in csv.DictReader(open(os.path.join(SRC, "ourairports_airports.csv"), encoding="utf-8"))
          if r["iso_country"] in countries}
    freqs = defaultdict(list)
    for r in csv.DictReader(open(os.path.join(SRC, "ourairports_frequencies.csv"), encoding="utf-8")):
        if r["airport_ident"] in ap:
            freqs[r["airport_ident"]].append(r)
    out = []
    for ident, a in ap.items():
        if a["type"] not in PER_AIRPORT:
            continue
        chans, seen = [], set()
        for f in sorted(freqs[ident], key=lambda f: PRIO.get(KIND.get(f["type"].strip(), ""), 9)):
            kind = KIND.get(f["type"].strip())
            try:
                mhz = float(f["frequency_mhz"])
            except ValueError:
                continue
            if not kind or not 108.0 <= mhz < 137.0:
                continue
            hz = airband_hz(mhz)
            if hz in seen:
                continue
            seen.add(hz)
            chans.append((kind, hz))
        chans = chans[:PER_AIRPORT[a["type"]]]
        if not chans:
            continue
        code = a["icao_code"] or (a["ident"] if re.fullmatch(r"[A-Z]{4}", a["ident"]) else "")
        if not code:                          # private airfields without ICAO code
            word = re.sub(r"[^A-Za-z]", "", (a["municipality"] or a["name"]).split(" ")[0])
            code = word[:5].upper() or a["ident"][-4:]
        count = defaultdict(int)
        named = []
        for kind, hz in chans:
            count[kind] += 1
            n = "%s %s%s" % (code, kind, count[kind] if count[kind] > 1 else "")
            named.append({"name": n[:12], "rx": hz, "tx": hz, "mode": "AM", "tx_enable": False,
                          "kind": "airport", "bw": "Wide"})
        out.append({"ident": ident, "type": a["type"], "name": a["name"], "region": a["iso_region"],
                    "country": a["iso_country"], "municipality": a["municipality"],
                    "lat": float(a["latitude_deg"]), "lon": float(a["longitude_deg"]), "channels": named})
    return out


# ------------------------------------------------------------------- Spain

ES_REGION_DISTRICT = {
    "ES-GA": 1, "ES-AS": 1, "ES-CB": 1, "ES-CL": 1, "ES-RI": 1,
    "ES-PV": 2, "ES-NC": 2, "ES-AR": 2,
    "ES-CT": 3,
    "ES-MD": 4, "ES-CM": 4, "ES-EX": 4,
    "ES-VC": 5, "ES-MC": 5,
    "ES-IB": 6, "ES-AN": 7, "ES-CN": 8, "ES-CE": 9, "ES-ML": 9,
}
ES_ZONE_LABEL = {1: "EA1", 2: "EA2", 3: "EA3", 4: "EA4", 5: "EA5", 6: "EA6", 7: "EA7", 8: "EA8", 9: "EA9"}

URE_RE = re.compile(r"^(?P<call>\S+)\s+(?P<rest>.*)$")
FREQ_RE = re.compile(r"(\d{3})\.(\d{3})(?:,(\d)|(\d))?\s*(?:MHz|FM)")
SHIFT_RE = re.compile(r"([+-])\s*(\d+(?:[.,]\d+)?)\s*(kHz|MHz)")
TONE_RE = re.compile(r"(?:Subtono|subtono|CTSS)\D{0,6}(\d{2,3}(?:[.,]\d)?)")
LOC_RE = re.compile(r"\b([A-R]{2}\d{2}[A-X]{2})\b")


def parse_ure(path, band):
    reps = []
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        m = URE_RE.match(line)
        if not m or not line.startswith("ED"):
            continue
        call, rest = m.group("call"), m.group("rest")
        if re.search(r"D-Star|DMR|C4FM|\(APRS\)|\(Echolink\)|Fuera de servicio|Digital", rest):
            continue                         # digital, APRS digipeaters, out of service
        f = FREQ_RE.search(rest)
        s = SHIFT_RE.search(rest)
        loc = LOC_RE.search(rest)
        if not (f and s and loc):
            continue
        frac = f.group(2) + (f.group(3) or f.group(4) or "")
        out_hz = int(f.group(1)) * 1_000_000 + int(frac.ljust(4, "0")) * 100
        sh = float(s.group(2).replace(",", ".")) * (1000 if s.group(3) == "kHz" else 1_000_000)
        in_hz = int(round(out_hz + (sh if s.group(1) == "+" else -sh)))
        t = TONE_RE.search(rest)
        lat, lon = locator_to_latlon(loc.group(1))
        if not (27.0 <= lat <= 44.5 and -18.5 <= lon <= 4.6):
            lat = lon = None                 # wrong locator in the source list
        district = int(call[2]) if call[2].isdigit() else 0
        reps.append({"name": call[:10] + (" V" if band == "2m" else " U"), "rx": out_hz, "tx": in_hz, "tone_tx": ctcss(t.group(1)) if t else "OFF",
                     "mode": "FM", "kind": "repeater", "band": band, "lat": lat, "lon": lon,
                     "locator": loc.group(1), "district": district,
                     "bw": "Narrow" if "FM-N" in rest else "Wide"})
    return reps


def pmr446():
    return [{"name": "PMR %d" % (i + 1), "rx": 446_006_250 + 12_500 * i, "tx": 446_006_250 + 12_500 * i,
             "mode": "FM", "bw": "Narrow", "tx_enable": False, "kind": "pmr"} for i in range(16)]


CB_CEPT_KHZ = [26965, 26975, 26985, 27005, 27015, 27025, 27035, 27055, 27065, 27075,
               27085, 27105, 27115, 27125, 27135, 27155, 27165, 27175, 27185, 27205,
               27215, 27225, 27255, 27235, 27245, 27265, 27275, 27285, 27295, 27305,
               27315, 27325, 27335, 27345, 27355, 27365, 27375, 27385, 27395, 27405]


def cb_cept(mode, prefix="CB"):
    return [{"name": "%s %02d" % (prefix, i + 1), "rx": k * 1000, "tx": k * 1000, "mode": mode,
             "bw": "Narrow", "tx_enable": False, "kind": "cb"} for i, k in enumerate(CB_CEPT_KHZ)]


def cb_uk_fm():
    return [{"name": "UK CB %02d" % (i + 1), "rx": 27_601_250 + 10_000 * i, "tx": 27_601_250 + 10_000 * i,
             "mode": "FM", "bw": "Narrow", "tx_enable": False, "kind": "cb"} for i in range(40)]


def build_spain():
    reps = parse_ure(os.path.join(SRC, "ure_144_2026-10-02.txt"), "2m")
    p432 = os.path.join(SRC, "ure_432_2026-10-02.txt")
    if os.path.exists(p432):
        reps += parse_ure(p432, "70cm")
    airports = load_airports({"ES"})
    zones = []
    for d in range(1, 10):
        chans = [r for r in reps if r["district"] == d]
        for a in airports:
            dist = ES_REGION_DISTRICT.get(a["region"], 0)
            if a["region"] == "ES-CM" and "albacete" in (a["municipality"] + a["name"]).lower():
                dist = 5                      # Albacete province belongs to EA5
            if dist == d:
                for c in a["channels"]:
                    chans.append(dict(c, lat=a["lat"], lon=a["lon"], airport=a["ident"],
                                      airport_type=a["type"]))
        zones.append({"name": ES_ZONE_LABEL[d], "channels": chans})
    zones.append({"name": "PMR-CB", "channels": pmr446() + cb_cept("AM")})
    return {"country": "ES", "title": {"es": "España", "en": "Spain"},
            "description": {"es": "Zonas EA1-EA9 con repetidores FM de radioaficionado (URE) y frecuencias "
                                  "de aeropuertos (solo RX, AM); zona 10: PMR446 y CB-27 (solo RX).",
                            "en": "Zones EA1-EA9 with amateur FM repeaters (URE) and airport frequencies "
                                  "(RX only, AM); zone 10: PMR446 and CB-27 (RX only)."},
            "zones": zones}


# --------------------------------------------------------------------- UK

UK_REGIONS = [("SE", "G SE LONDON"), ("SW", "G SW+CI"), ("CEN", "G MIDLANDS"), ("EA", "G EAST ANGLIA"),
              ("NOR", "G NORTH+GD"), ("WM", "GW WALES"), ("SCOT", "GM SCOTLAND"), ("NI", "GI N.IRELAND")]
FORCED = {"GB-SCT": "SCOT", "GB-WLS": "WM", "GB-NIR": "NI", "IM": "NOR", "JE": "SW", "GG": "SW"}


def parse_ukrepeater(path):
    reps, seen = [], set()
    for line in open(path, encoding="utf-8"):
        p = line.rstrip("\n").split("|")
        if len(p) < 9:
            continue
        call, band, out_mhz, in_mhz, tone, region, lat, lon, where = [x.strip() for x in p[:9]]
        if call.endswith("-L") or float(in_mhz) == 0.0:
            continue                         # link transmitters / simplex gateways
        key = (call, out_mhz, in_mhz)
        if key in seen:
            continue
        seen.add(key)
        if region not in dict(UK_REGIONS):
            region = ""                      # fixed below from the position
        name = (call + " " + where.title())[:12].rstrip()
        reps.append({"name": name, "rx": int(round(float(out_mhz) * 1e6)), "tx": int(round(float(in_mhz) * 1e6)),
                     "tone_tx": ctcss(tone), "mode": "FM", "kind": "repeater", "band": "2m" if band == "2" else "70cm",
                     "lat": float(lat), "lon": float(lon), "region": region, "bw": "Wide",
                     "reduced": len(p) > 9 and p[9].strip() == "R"})
    known = [r for r in reps if r["region"]]
    for r in reps:
        if not r["region"]:
            r["region"] = min(known, key=lambda k: km((r["lat"], r["lon"]), (k["lat"], k["lon"])))["region"]
    return reps


def uk_simplex():
    ch = [("2M CALL", 145.500), ("2M S21", 145.525), ("2M S22", 145.550), ("2M S23", 145.575),
          ("2M S16", 145.400), ("2M S17", 145.425), ("2M S18", 145.450), ("2M S19", 145.475),
          ("70CM CALL", 433.500), ("70 SU21", 433.525), ("70 SU22", 433.550), ("70 SU23", 433.575),
          ("70 SU17", 433.425), ("70 SU18", 433.450), ("70 SU19", 433.475)]
    out = [{"name": n, "rx": int(round(f * 1e6)), "tx": int(round(f * 1e6)), "mode": "FM", "bw": "Wide",
            "kind": "simplex"} for n, f in ch]
    out += [{"name": "APRS", "rx": 144_800_000, "tx": 144_800_000, "mode": "FM", "bw": "Wide", "kind": "simplex"},
            {"name": "ISS VOICE", "rx": 145_800_000, "tx": 145_800_000, "mode": "FM", "bw": "Wide",
             "tx_enable": False, "kind": "simplex"},
            {"name": "ISS APRS", "rx": 145_825_000, "tx": 145_825_000, "mode": "FM", "bw": "Wide", "kind": "simplex"}]
    return out


def build_uk():
    reps = parse_ukrepeater(os.path.join(SRC, "ukrepeater_net_2026-10-02.txt"))
    airports = load_airports({"GB", "IM", "JE", "GG"})
    # UK small airfields are very numerous: keep licensed aerodromes only
    airports = [a for a in airports if a["type"] in ("large_airport", "medium_airport")
                or any(c["name"].endswith("TWR") for c in a["channels"])]
    for a in airports:
        forced = FORCED.get(a["region"]) or FORCED.get(a["country"])
        a["zone"] = forced or min(reps, key=lambda r: km((a["lat"], a["lon"]), (r["lat"], r["lon"])))["region"]
    zones = []
    for code, label in UK_REGIONS:
        chans = [r for r in reps if r["region"] == code]
        for a in airports:
            if a["zone"] == code:
                for c in a["channels"]:
                    chans.append(dict(c, lat=a["lat"], lon=a["lon"], airport=a["ident"], airport_type=a["type"]))
        zones.append({"name": label, "channels": chans})
    zones.append({"name": "UK SIMPLEX", "channels": uk_simplex()})
    zones.append({"name": "PMR-CB", "channels": pmr446() + cb_uk_fm() + cb_cept("FM", "EU CB")})
    return {"country": "GB", "title": {"es": "Reino Unido", "en": "United Kingdom"},
            "description": {"es": "Zonas por región ETCC con repetidores FM (ukrepeater.net) y aeropuertos "
                                  "(solo RX, AM); zona 9: símplex, APRS e ISS; zona 10: PMR446 y CB 27/81 y CEPT "
                                  "(solo RX).",
                            "en": "Zones per ETCC region with FM repeaters (ukrepeater.net) and airports "
                                  "(RX only, AM); zone 9: simplex, APRS and ISS; zone 10: PMR446, CB 27/81 and "
                                  "CEPT (RX only)."},
            "zones": zones}


SOURCES = {
    "ES": ["OurAirports (public domain) - airports and frequencies",
           "URE - Repetidores y balizas (https://www.ure.es/repetidores/), 2026-10-02"],
    "GB": ["OurAirports (public domain) - airports and frequencies",
           "ukrepeater.net (RSGB ETCC) voice repeater list, 2026-10-02"],
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for build, fname in ((build_spain, "es.json"), (build_uk, "gb.json")):
        d = build()
        seen = defaultdict(int)
        for z in d["zones"]:
            for ch in z["channels"]:
                seen[ch["name"]] += 1
                if seen[ch["name"]] > 1:          # unique names (12 chars max)
                    suf = str(seen[ch["name"]])
                    ch["name"] = ch["name"][:12 - len(suf)].rstrip() + suf
        d["generated"] = datetime.date.today().isoformat()
        d["sources"] = SOURCES[d["country"]]
        with open(os.path.join(OUT, fname), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=1)
        print(fname, [(z["name"], len(z["channels"])) for z in d["zones"]])


if __name__ == "__main__":
    main()
