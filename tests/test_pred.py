#!/usr/bin/env python3
"""test_pred.py - Compare the firmware pass predictor with Skyfield find_events."""
import math, os, subprocess, sys
from datetime import datetime, timezone
from sgp4.api import Satrec, WGS72
from skyfield.api import EarthSatellite, load, wgs84
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_sgp4 import TLES, elements, DRIVER  # noqa: E402

LAT, LON, ALT, MASK = 38.0625, -1.2917, 100.0, 0.0


def main():
    ts = load.timescale(builtin=True)
    obs = wgs84.latlon(LAT, LON, ALT)
    proc = subprocess.Popen([DRIVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    fails = 0
    worst = [0, 0, 0, 0]
    total_props, npass = 0, 0
    for name, l1, l2 in TLES[:3]:
        sat = Satrec.twoline2rv(l1, l2, WGS72)
        es = EarthSatellite(l1, l2, name, ts)
        el = elements(sat)
        start = int((el[0] - 2440587.5) * 86400) + 600
        t0 = ts.from_datetime(datetime.fromtimestamp(start, timezone.utc))
        t1 = ts.from_datetime(datetime.fromtimestamp(start + 86400, timezone.utc))
        times, events = es.find_events(obs, t0, t1, altitude_degrees=MASK)
        ref = []
        cur = {}
        for t, ev in zip(times, events):
            u = t.utc_datetime().timestamp()
            if ev == 0:
                cur = {"aos": u}
            elif ev == 1:
                cur["tca"] = u
                cur["max"] = (es - obs).at(t).altaz()[0].degrees
            elif ev == 2 and "tca" in cur:
                cur["los"] = u
                ref.append(cur)
                cur = {}
        cursor = start
        print("%s: %d passes in 24 h (Skyfield)" % (name, len(ref)))
        while True:
            proc.stdin.write("X " + " ".join("%.15g" % x for x in el + [LAT, LON, ALT, cursor, MASK]) + "\n")
            proc.stdin.flush()
            aos, tca, los, mx, aaz, taz, laz, props = map(int, proc.stdout.readline().split())
            if los == 0 or aos > start + 86400 - 600:
                break
            cursor = los + 60
            # match with the Skyfield pass that has the closest TCA
            p = min(ref, key=lambda q: abs(q["tca"] - tca)) if ref else None
            if p is None or abs(p["tca"] - tca) > 900:
                tag = "ok (grazing, max<1 deg)" if mx < 1 else "EXTRA"
                if mx >= 1: fails += 1
                print("  AOS %s  max %2d  %s" % (datetime.fromtimestamp(aos, timezone.utc).strftime("%m-%d %H:%M:%S"), mx, tag))
                continue
            p["seen"] = True
            d = [abs(aos - p["aos"]) if "aos" in p else 0, abs(tca - p["tca"]),
                 abs(los - p["los"]), abs(mx - p["max"])]
            worst = [max(a, b) for a, b in zip(worst, d)]
            total_props += props; npass += 1
            ok = d[0] <= 3 and d[2] <= 3 and d[1] <= 15 and d[3] <= 1.0
            print("  AOS %s  TCA %s  LOS %s  max %2d deg  az %3d->%3d  props %4d  %s%s" % (
                datetime.fromtimestamp(aos, timezone.utc).strftime("%m-%d %H:%M:%S"),
                datetime.fromtimestamp(tca, timezone.utc).strftime("%H:%M:%S"),
                datetime.fromtimestamp(los, timezone.utc).strftime("%H:%M:%S"),
                mx, aaz, laz, props, "ok" if ok else "MISMATCH %s" % d,
                "" if "aos" in p else " (in progress at start)"))
            if not ok: fails += 1
        missed = [q for q in ref if not q.get("seen")]
        for q in missed:
            print("  MISSED pass max %.1f deg at %s" % (q["max"], datetime.fromtimestamp(q["tca"], timezone.utc)))
            if q["max"] >= 1.0: fails += 1
    print("Worst |dAOS|=%ds |dTCA|=%ds |dLOS|=%ds |dMaxEl|=%.2f deg; avg %.0f propagations/pass"
          % (worst[0], worst[1], worst[2], worst[3], total_props / max(1, npass)))
    proc.stdin.close(); proc.wait()
    print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
