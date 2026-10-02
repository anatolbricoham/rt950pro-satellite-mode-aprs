#!/usr/bin/env python3
"""
test_e2e.py - End-to-end check of the satellite feature on the host.

  PC tool (pc/rt950_toolkit/sat.py)  --A5 frames over a pty-->  firmware cps.c
  (build_host/sim, real firmware sources) -> SPI flash image -> sat_db_reload
  -> background predictor -> satellite screens rendered to PNG.
"""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "pc"))
from rt950_toolkit import sat as core  # noqa: E402

B = os.path.join(ROOT, "build_host")
OUT = os.path.join(B, "e2e")
SIM = os.path.join(B, "sim")


def main():
    os.makedirs(OUT, exist_ok=True)
    fails = 0

    # 1) PC side: TLE -> passes -> satdb.bin (offline sample TLEs)
    class A:  # argparse-like namespace
        locator = "IM98IB"; lat = None; lon = None; alt = 100.0; hours = 48.0; min_el = 0.0
        tz = "Europe/Madrid"; freqs = core.DEFAULT_FREQS; offline = True
        tle_file = os.path.join(HERE, "data", "sample_tle.txt"); out = OUT
        start_channel = 900; db_hours = 72.0; ics_min_el = 15.0
    res = core.run_pipeline(A, log=lambda *a: None)
    blob = res["blob"]
    print("PC tool: %d sats, %d passes, satdb %d bytes" % (len(res["entries"]), len(res["passes"]), len(blob)))
    core.unpack_db(blob)

    # 2) upload through the real firmware CPS handler
    flash_out = os.path.join(OUT, "flash_dump.bin")
    sim = subprocess.Popen([SIM, "cps", flash_out], stdout=subprocess.PIPE, text=True)
    line = sim.stdout.readline().strip()
    pty = line.split(" ", 1)[1]
    import serial
    ser = serial.Serial(pty, 115200, timeout=0.05)
    t0 = time.time()
    logs = []
    n = core.upload_db(pty, blob, log=logs.append, ser=ser)
    print("Upload over pty: %.1f s, radio reports %d satellites" % (time.time() - t0, n))
    for l in logs:
        print("   ", l)
    rest, _ = sim.communicate(timeout=30)
    print("   sim:", rest.strip().replace("\n", " | "))
    dump = open(flash_out, "rb").read()
    if dump[:len(blob)] != blob:
        print("FAIL: flash content differs from satdb.bin")
        fails += 1
    if n != len(res["entries"]):
        print("FAIL: satellite count after reload")
        fails += 1
    if "time source=2" not in rest:
        print("FAIL: radio clock was not set by the PC (source PC=2)")
        fails += 1

    # 3) boot the engine from the flash image and render the screens
    now = int(time.time())
    want = "SO-50"
    r = subprocess.run([SIM, "render", flash_out, str(now), os.path.join(OUT, "screen"), want],
                       capture_output=True, text=True, timeout=600)
    print(r.stdout)
    if r.returncode != 0:
        print("FAIL: render exit", r.returncode, r.stderr)
        fails += 1

    # firmware next-pass vs PC prediction (both from the same TLE)
    firm = {}
    for m in re.finditer(r"^\s+(\S+(?: \S+)?)\s+AOS (\S+ \S+) dur\s+(\d+)s max\s+(\d+)", r.stdout, re.M):
        firm[m.group(1).strip()] = (m.group(2), int(m.group(3)), int(m.group(4)))
    import datetime as dt
    for e in res["entries"]:
        nxt = [p for p in res["passes"] if p.sat is e and p.los > now]
        if not nxt or e.name.upper() not in firm:
            continue
        p = nxt[0]
        f_aos = dt.datetime.strptime(firm[e.name.upper()][0], "%Y-%m-%d %H:%M:%S").replace(
            tzinfo=dt.timezone.utc).timestamp()
        d = abs(f_aos - p.aos)
        ok = d <= 3
        print("  next pass %-10s PC %s  radio %s  |dAOS| %.0f s %s" % (
            e.name, dt.datetime.fromtimestamp(p.aos, dt.timezone.utc).strftime("%H:%M:%S"),
            firm[e.name.upper()][0][11:], d, "ok" if ok else "MISMATCH"))
        fails += 0 if ok else 1

    m = re.search(r"PTT hook: ret=(-?\d+) tx_vfo=(\w) tx=(\d+) Hz tone_idx=(-?\d+)", r.stdout)
    if not m or m.group(1) != "1" or m.group(4) != "3":     # idx 3 = 74.4 Hz arm tone
        print("FAIL: PTT hook / arm tone", m.groups() if m else None)
        fails += 1
    if "after exit: VFO A=145500000 VFO B=433500000 active=A" not in r.stdout:
        print("FAIL: VFOs not restored after leaving satellite mode")
        fails += 1

    # PPM -> PNG (2x) for the documentation
    try:
        from PIL import Image
        for f in sorted(os.listdir(OUT)):
            if f.endswith(".ppm"):
                im = Image.open(os.path.join(OUT, f))
                im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
                im.save(os.path.join(OUT, f[:-4] + ".png"))
                print("  png:", f[:-4] + ".png")
    except ImportError:
        print("  (pillow not installed: PNGs skipped)")

    print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
