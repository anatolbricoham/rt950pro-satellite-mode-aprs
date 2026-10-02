/*
 * sgp4_driver.c - Host-side driver for verifying the firmware SGP4 code.
 *
 * Reads lines from stdin:
 *   P epoch_jd bstar incl raan ecc argp ma n tsince_min
 *       -> "err x y z vx vy vz"   (TEME, km, km/s)
 *   L epoch_jd bstar incl raan ecc argp ma n jd lat lon alt_m
 *       -> "err az el range range_rate"
 *   M fn x            (math kernel check) -> value
 *   X epoch_jd bstar incl raan ecc argp ma n lat lon alt start_unix mask
 *       -> "aos tca los max_el aos_az tca_az los_az props"  (next pass)
 *
 * Built by tests/run_tests.sh with the host compiler.
 */
#include <stdio.h>
#include <string.h>
#include "app/sat_sgp4.h"
#include "app/sat_math.h"
#include "app/sat_pred.h"

int main(void)
{
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        double e, b, i, r, ec, a, m, n, t, jd, lat, lon, alt, x;
        char fn[16];
        if (line[0] == 'P' &&
            sscanf(line + 1, "%lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   &e, &b, &i, &r, &ec, &a, &m, &n, &t) == 9) {
            sgp4_sat_t s; vec3_t rv, vv;
            int err = sgp4_init(&s, e, b, i, r, ec, a, m, n);
            if (!err) err = sgp4_propagate(&s, t, &rv, &vv);
            printf("%d %.9f %.9f %.9f %.12f %.12f %.12f\n", err,
                   rv.x, rv.y, rv.z, vv.x, vv.y, vv.z);
        } else if (line[0] == 'L' &&
            sscanf(line + 1, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   &e, &b, &i, &r, &ec, &a, &m, &n, &jd, &lat, &lon, &alt) == 12) {
            sgp4_sat_t s; sat_observer_t o; sat_look_t lk;
            memset(&lk, 0, sizeof lk);
            int err = sgp4_init(&s, e, b, i, r, ec, a, m, n);
            sat_observer_set(&o, lat, lon, alt);
            if (!err) err = sat_look(&s, &o, jd, &lk);
            printf("%d %.6f %.6f %.6f %.9f\n", err, lk.az_deg, lk.el_deg,
                   lk.range_km, lk.range_rate_kms);
        } else if (line[0] == 'X') {
            double st, mask;
            if (sscanf(line + 1, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                       &e, &b, &i, &r, &ec, &a, &m, &n, &lat, &lon, &alt, &st, &mask) != 13)
                continue;
            sgp4_sat_t s; sat_observer_t o; sat_pred_job_t j;
            sgp4_init(&s, e, b, i, r, ec, a, m, n);
            sat_observer_set(&o, lat, lon, alt);
            sat_pred_start(&j, &s, &o, mask, (uint32_t)st, 2 * 86400U);
            while (!sat_pred_step(&j, 16)) { }
            printf("%u %u %u %u %u %u %u %u\n", j.result.aos, j.result.tca, j.result.los,
                   j.result.max_el, j.result.aos_az, j.result.tca_az, j.result.los_az,
                   (unsigned)j.props);
        } else if (line[0] == 'M' && sscanf(line + 1, "%15s %lf", fn, &x) == 2) {
            double v = 0;
            if (!strcmp(fn, "sin")) v = sm_sin(x);
            else if (!strcmp(fn, "cos")) v = sm_cos(x);
            else if (!strcmp(fn, "atan")) v = sm_atan(x);
            else if (!strcmp(fn, "asin")) v = sm_asin(x);
            else if (!strcmp(fn, "acos")) v = sm_acos(x);
            else if (!strcmp(fn, "sqrt")) v = sm_sqrt(x);
            else if (!strcmp(fn, "cbrt")) v = sm_cbrt(x);
            else if (!strcmp(fn, "floor")) v = sm_floor(x);
            printf("%.17g\n", v);
        }
        fflush(stdout);
    }
    return 0;
}
