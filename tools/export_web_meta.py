#!/usr/bin/env python3
"""export_web_meta.py - Export the codeplug definitions used by the desktop
toolkit (regions, transfer plan, tones, schema-driven fields) and the country
presets as JSON for the web / Android programmer (mobile/www/data/), so the
two implementations share exactly the same tables."""
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "pc"))
from rt950_toolkit import codeplug as C, __version__  # noqa: E402

OUT = os.path.join(HERE, "..", "mobile", "www", "data")


def main():
    os.makedirs(OUT, exist_ok=True)
    meta = {
        "version": __version__,
        "regions": [{"name": n, "addr": a, "size": s, "transfer": list(C.TRANSFER[n])} for n, a, s in C.REGIONS],
        "tones": C.tone_list(),
        "dcs": C.dcs_codes(),
        "power": C.POWER, "scramble": C.SCRAMBLE, "bandwidth": C.BANDWIDTH, "ptt_id": C.PTT_ID,
        "encryption": C.ENCRYPTION,
        "select": [dict(section=f.section, title=f.title, addr=f.addr, positions=f.positions,
                        options=f.options, group=f.group) for f in C.select_fields()],
        "text": [dict(group=t.group, title=t.title, addr=t.addr, length=t.length, upper=t.upper)
                 for t in C.text_fields()],
        "num": [dict(group=n.group, title=n.title, addr=n.addr, signed=n.signed, scale=n.scale, lo=n.lo, hi=n.hi)
                for n in C.numeric_fields()],
        "dtmf": [dict(group=d.group, title=d.title, addr=d.addr, length=d.length) for d in C.dtmf_fields()],
    }
    with open(os.path.join(OUT, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, separators=(",", ":"))
    from rt950_toolkit import legal
    with open(os.path.join(OUT, "legal.json"), "w", encoding="utf-8") as f:
        json.dump({"version": legal.DISCLAIMER_VERSION, "disclaimer": legal.DISCLAIMER,
                   "backup": legal.BACKUP_GUIDE}, f, ensure_ascii=False, indent=1)
    src = os.path.join(HERE, "..", "pc", "rt950_toolkit", "data", "countries")
    for fn in os.listdir(src):
        if fn.endswith(".json"):
            shutil.copy(os.path.join(src, fn), os.path.join(OUT, "country-" + fn))
    print("meta.json:", len(meta["select"]), "select,", len(meta["text"]), "text fields")


if __name__ == "__main__":
    main()
