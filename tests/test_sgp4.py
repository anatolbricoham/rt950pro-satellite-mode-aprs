#!/usr/bin/env python3
"""
test_sgp4.py - Compare the firmware C SGP4/geometry code with python-sgp4
and Skyfield.  Run via tests/run_tests.sh (builds build_host/sgp4_driver).
"""
import math
from datetime import datetime, timedelta, timezone
import os
import random
import subprocess
import sys

from sgp4.api import Satrec, WGS72
from sgp4 import exporter  # noqa: F401  (ensures full package present)

HERE = os.path.dirname(os.path.abspath(__file__))
DRIVER = os.path.join(HERE, "..", "build_host", "sgp4_driver")

# Real element sets (Oct 2026) + classic Spacetrack Report #3 test vector
TLES = [
    ("ISS",
     "1 25544U 98067A   26273.85230731  .00003748  00000-0  76931-4 0  9991",
     "2 25544  51.6316 137.1559 0007005 207.6272 152.4345 15.48699564588139"),
    ("SO-50",
     "1 27607U 02058C   26274.42157801  .00000393  00000-0  64436-4 0  9998",
     "2 27607  64.5523 160.2499 0070977 242.8813 116.5042 14.83236109280217"),
    ("AO-123",
     "1 61781U 24199AY  26274.91400601  .00005570  00000-0  15562-3 0  9999",
     "2 61781  97.2810 142.6256 0011540 288.0068  71.9919 15.37048869150318"),
    ("STR3-88888",  # Spacetrack Report #3 SGP4 test case
     "1 88888U          80275.98708465  .00073094  13844-3  66816-4 0    87",
     "2 88888  72.8435 115.9689 0086731  52.6988 110.5714 16.05824518  1058"),
    ("HIGH-ECC-LEO",  # Vallado verification set object 06251 (near-earth, e~0.003)
     "1 06251U 62025E   06176.82412014  .00008885  00000-0  12808-3 0  3985",
     "2 06251  58.0579  54.0425 0030035 139.1568 221.1854 15.56387291  6774"),
]


def elements(sat):
    epoch = sat.jdsatepoch + sat.jdsatepochF
    return [epoch, sat.bstar, math.degrees(sat.inclo), math.degrees(sat.nodeo),
            sat.ecco, math.degrees(sat.argpo), math.degrees(sat.mo),
            sat.no_kozai * 1440.0 / (2 * math.pi)]


def main():
    proc = subprocess.Popen([DRIVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            text=True, bufsize=1)

    def ask(s):
        proc.stdin.write(s + "\n")
        proc.stdin.flush()
        return proc.stdout.readline().split()

    fails = 0
    worst_r = worst_v = 0.0
    for name, l1, l2 in TLES:
        sat = Satrec.twoline2rv(l1, l2, WGS72)
        el = elements(sat)
        for t in [0, 1, 10, 60, 360, 720, 1440, 2880, 4320, 7 * 1440, -1440]:
            e, r, v = sat.sgp4_tsince(t)
            out = ask("P " + " ".join("%.15g" % x for x in el + [t]))
            err = int(out[0])
            if e != 0 or err != 0:
                if (e != 0) != (err != 0):
                    print("  MISMATCH err", name, t, e, err)
                    fails += 1
                continue
            cr = list(map(float, out[1:4]))
            cv = list(map(float, out[4:7]))
            dr = math.dist(r, cr)
            dv = math.dist(v, cv)
            worst_r = max(worst_r, dr)
            worst_v = max(worst_v, dv)
            if dr > 1e-3 or dv > 1e-6:      # 1 metre, 1 mm/s
                print("  FAIL %-12s t=%7.0f  dr=%.6f km dv=%.9f km/s" % (name, t, dr, dv))
                fails += 1
    print("SGP4 TEME vs python-sgp4: worst |dr| = %.3e km, |dv| = %.3e km/s" % (worst_r, worst_v))

    # ---- topocentric look angles vs Skyfield --------------------------
    from skyfield.api import EarthSatellite, load, wgs84
    ts = load.timescale(builtin=True)
    lat, lon, alt = 38.0625, -1.2917, 100.0      # IM98IB
    obs = wgs84.latlon(lat, lon, alt)
    worst_az = worst_el = worst_rng = worst_rr = 0.0
    rnd = random.Random(1)
    for name, l1, l2 in TLES[:3]:
        sat = Satrec.twoline2rv(l1, l2, WGS72)
        es = EarthSatellite(l1, l2, name, ts)
        el = elements(sat)
        for _ in range(200):
            jd = el[0] + rnd.uniform(0.0, 2.0)
            # TLE epochs/firmware clock are UTC; GMST uses UTC ~ UT1 (|dUT1| < 0.9 s)
            dt = datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(days=jd - 2440587.5)
            t = ts.from_datetime(dt)
            topo = (es - obs).at(t)
            alt_, az_, dist = topo.altaz()
            _, _, _, _, _, rr = topo.frame_latlon_and_rates(obs)
            out = ask("L " + " ".join("%.15g" % x for x in el + [jd, lat, lon, alt]))
            caz, cel, crng, crr = map(float, out[1:5])
            daz = abs((caz - az_.degrees + 180) % 360 - 180) * math.cos(math.radians(alt_.degrees))
            worst_az = max(worst_az, daz)
            worst_el = max(worst_el, abs(cel - alt_.degrees))
            worst_rng = max(worst_rng, abs(crng - dist.km))
            worst_rr = max(worst_rr, abs(crr - rr.km_per_s))
    print("Look angles vs Skyfield (600 samples): worst az*cos(el)=%.4f deg, "
          "el=%.4f deg, range=%.3f km, range-rate=%.5f km/s"
          % (worst_az, worst_el, worst_rng, worst_rr))
    # Skyfield applies polar motion/precession frames (TEME->ITRS);
    # the firmware uses GMST only: a few hundredths of a degree is expected.
    if worst_az > 0.1 or worst_el > 0.1 or worst_rng > 2.0 or worst_rr > 0.002:
        print("  FAIL look-angle tolerance")
        fails += 1
    # Doppler error implied at 437 MHz:
    print("  -> Doppler error at 437 MHz <= %.1f Hz" % (worst_rr / 299792.458 * 437e6))

    # ---- math kernels -------------------------------------------------
    worst_m = 0.0
    for fn, f, lo, hi in [("sin", math.sin, -50, 50), ("cos", math.cos, -50, 50),
                          ("atan", math.atan, -100, 100), ("asin", math.asin, -1, 1),
                          ("acos", math.acos, -1, 1), ("sqrt", math.sqrt, 1e-6, 1e6),
                          ("cbrt", lambda x: x ** (1 / 3), 1e-6, 1e6)]:
        for _ in range(500):
            x = rnd.uniform(lo, hi)
            got = float(ask("M %s %.17g" % (fn, x))[0])
            ref = f(x)
            worst_m = max(worst_m, abs(got - ref) / max(1.0, abs(ref)))
    print("Math kernels: worst relative error %.2e" % worst_m)
    if worst_m > 1e-13:
        print("  FAIL math tolerance")
        fails += 1

    proc.stdin.close()
    proc.wait()
    print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
