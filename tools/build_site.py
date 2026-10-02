#!/usr/bin/env python3
"""
build_site.py - Assemble the GitHub Pages site and the release extras

    python tools/build_site.py [--firmware build/rt950-custom.BTF] [--out site]

* site/help/        the application's HTML manual (pc/rt950_toolkit/help)
* site/app/         the web / Android programmer (mobile/www)
* site/firmware/    firmware images + manifest.json for the web flasher
                    (custom firmware from the build, Radtel stock V0.27 from
                    binary/ so testers can go back to the original)
* site/codeplugs/   preloaded country codeplugs (.rt950, CHIRP and RT-950
                    Editor CSV), also attached to every release
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "pc"))

from rt950_toolkit import __version__, country, csvio  # noqa: E402

STOCK = os.path.join(ROOT, "binary", "RT_950Pro_V0.27_260203", "RT_950Pro_V0.27_260203.BTF")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def copytree(src, dst):
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))


def codeplugs(out):
    os.makedirs(out, exist_ok=True)
    made = []
    for code in ("ES", "GB"):
        cp, rep = country.build(code)
        base = os.path.join(out, "codeplug-%s-%s" % (code, __version__))
        cp.save(base + ".rt950")
        csvio.export_chirp(cp, base + "-chirp.csv")
        csvio.export_editor(cp, base + "-rt950editor.csv")
        csvio.export_zones(cp, base + "-zones.csv")
        with open(base + "-report.txt", "w", encoding="utf-8") as f:
            f.write(rep.text() + "\n")
        made += [base + s for s in (".rt950", "-chirp.csv", "-rt950editor.csv", "-zones.csv", "-report.txt")]
    return made


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=os.path.join(ROOT, "build", "rt950-custom.BTF"))
    ap.add_argument("--out", default=os.path.join(ROOT, "site"))
    a = ap.parse_args()
    site = a.out
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "export_web_meta.py")])
    copytree(os.path.join(ROOT, "pc", "rt950_toolkit", "help"), os.path.join(site, "help"))
    copytree(os.path.join(ROOT, "mobile", "www"), os.path.join(site, "app"))
    fw = os.path.join(site, "firmware")
    os.makedirs(fw, exist_ok=True)
    manifest = []
    if os.path.exists(a.firmware):
        name = "rt950-bricohams-%s.BTF" % __version__
        shutil.copy(a.firmware, os.path.join(fw, name))
        manifest.append({"kind": "custom", "version": __version__, "file": name,
                         "size": os.path.getsize(a.firmware), "sha256": sha256(a.firmware)})
    if os.path.exists(STOCK):
        shutil.copy(STOCK, os.path.join(fw, os.path.basename(STOCK)))
        manifest.append({"kind": "stock", "version": "V0.27 (260203)", "file": os.path.basename(STOCK),
                         "size": os.path.getsize(STOCK), "sha256": sha256(STOCK)})
    with open(os.path.join(fw, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    made = codeplugs(os.path.join(site, "codeplugs"))
    shot = os.path.join(ROOT, "docs", "toolkit", "img", "es-channels.png")
    os.makedirs(os.path.join(site, "assets"), exist_ok=True)
    if os.path.exists(shot) and not os.path.exists(os.path.join(site, "assets", "screenshot.png")):
        shutil.copy(shot, os.path.join(site, "assets", "screenshot.png"))
    print("site ready:", site, "| firmware:", [m["file"] for m in manifest], "| codeplugs:", len(made))


if __name__ == "__main__":
    main()
