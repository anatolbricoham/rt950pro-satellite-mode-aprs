/*
 * sat_pred.c - Incremental satellite pass predictor
 *
 * Coarse search with an elevation-dependent step (240 s far below the
 * horizon, 20 s close to it), bisection to 1 s for AOS/LOS and a ternary
 * search for the time of closest approach.  A whole 24 h search for a LEO
 * typically costs 400-900 propagations (~0.5 s on the AT32 with soft double),
 * so the radio runs it in small slices from the scheduler.
 */

#include "app/sat_pred.h"
#include "app/sat_math.h"

enum { PH_INIT = 0, PH_BACK, PH_SEARCH, PH_TCA, PH_DONE };

#define BACK_STEP_S      20.0
#define BACK_LIMIT_S     (40.0 * 60.0)
#define TCA_STEP_S       10.0
#define MAX_PASS_S       (45.0 * 60.0)

double sat_pred_elev(sgp4_sat_t *sgp, const sat_observer_t *obs, double t_unix,
                     sat_look_t *lk_out)
{
    sat_look_t lk;
    double jd = 2440587.5 + t_unix / 86400.0;
    if (sat_look(sgp, obs, jd, &lk) != SGP4_OK)
        return -90.0;
    if (lk_out) *lk_out = lk;
    return lk.el_deg;
}

static double elev(sat_pred_job_t *j, double t)
{
    j->props++;
    return sat_pred_elev(j->sgp, j->obs, t, 0);
}

/* el(t_lo) and el(t_hi) are on opposite sides of the mask. Returns the
 * crossing time to 1 s. */
static double bisect(sat_pred_job_t *j, double t_lo, double t_hi)
{
    double e_lo = elev(j, t_lo);
    int lo_above = (e_lo >= j->mask_deg);
    while (t_hi - t_lo > 1.0) {
        double tm = 0.5 * (t_lo + t_hi);
        int above = (elev(j, tm) >= j->mask_deg);
        if (above == lo_above) t_lo = tm; else t_hi = tm;
    }
    return lo_above ? t_lo : t_hi;    /* first/last second above the mask */
}

static uint16_t az_at(sat_pred_job_t *j, double t)
{
    sat_look_t lk;
    j->props++;
    if (sat_pred_elev(j->sgp, j->obs, t, &lk) <= -90.0) return 0;
    int a = (int)(lk.az_deg + 0.5);
    return (uint16_t)(a >= 360 ? a - 360 : a);
}

static double coarse_step(double el)
{
    if (el < -40.0) return 240.0;
    if (el < -20.0) return 120.0;
    if (el < -8.0)  return 45.0;
    return 20.0;
}

void sat_pred_start(sat_pred_job_t *j, sgp4_sat_t *sgp, const sat_observer_t *obs,
                    double mask_deg, uint32_t start_unix, uint32_t horizon_s)
{
    j->sgp = sgp;
    j->obs = obs;
    j->mask_deg = mask_deg;
    j->t_start = (double)start_unix;
    j->t = (double)start_unix;
    j->t_end = (double)start_unix + (double)horizon_s;
    j->phase = PH_INIT;
    j->done = 0;
    j->props = 0;
    j->result.aos = j->result.tca = j->result.los = 0;
    j->result.aos_az = j->result.tca_az = j->result.los_az = 0;
    j->result.max_el = 0;
    j->result.from_db = 0;
    if (sgp->error != SGP4_OK) {
        j->phase = PH_DONE;
        j->done = 1;
    }
}

static void finish(sat_pred_job_t *j, double los)
{
    /* refine TCA with a ternary search around the best coarse sample */
    double a = j->best_t - TCA_STEP_S, b = j->best_t + TCA_STEP_S;
    if (a < j->aos) a = j->aos;
    if (b > los) b = los;
    for (int i = 0; i < 8 && b - a > 1.0; i++) {
        double m1 = a + (b - a) / 3.0, m2 = b - (b - a) / 3.0;
        if (elev(j, m1) < elev(j, m2)) a = m1; else b = m2;
    }
    double tca = 0.5 * (a + b);
    double max_el = elev(j, tca);
    if (max_el < j->best_el) { max_el = j->best_el; tca = j->best_t; }

    j->result.aos = (uint32_t)(j->aos + 0.5);
    j->result.los = (uint32_t)(los + 0.5);
    j->result.tca = (uint32_t)(tca + 0.5);
    j->result.max_el = (uint8_t)(max_el < 0.0 ? 0 : (max_el > 90.0 ? 90 : (int)(max_el + 0.5)));
    j->result.aos_az = az_at(j, j->aos);
    j->result.tca_az = az_at(j, tca);
    j->result.los_az = az_at(j, los);
    j->phase = PH_DONE;
    j->done = 1;
}

uint8_t sat_pred_step(sat_pred_job_t *j, int budget)
{
    uint16_t start_props = j->props;
    while (!j->done && (int)(j->props - start_props) < budget) {
        switch (j->phase) {
        case PH_INIT: {
            double e = elev(j, j->t);
            if (e >= j->mask_deg) {
                j->phase = PH_BACK;
            } else {
                j->best_el = e;          /* reuse as "previous elevation" */
                j->phase = PH_SEARCH;
            }
            break;
        }
        case PH_BACK: {
            double t2 = j->t - BACK_STEP_S;
            if (j->t_start - t2 > BACK_LIMIT_S) {
                j->aos = t2;
            } else if (elev(j, t2) < j->mask_deg) {
                j->aos = bisect(j, t2, j->t);
            } else {
                j->t = t2;
                break;
            }
            j->t = j->aos;
            j->best_t = j->aos;
            j->best_el = j->mask_deg;
            j->phase = PH_TCA;
            break;
        }
        case PH_SEARCH: {
            double dt = coarse_step(j->best_el);
            double t2 = j->t + dt;
            double e = elev(j, t2);
            if (e >= j->mask_deg) {
                j->aos = bisect(j, j->t, t2);
                j->t = j->aos;
                j->best_t = j->aos;
                j->best_el = j->mask_deg;
                j->phase = PH_TCA;
            } else {
                j->t = t2;
                j->best_el = e;
                if (j->t > j->t_end) {   /* nothing within the horizon */
                    j->phase = PH_DONE;
                    j->done = 1;
                }
            }
            break;
        }
        case PH_TCA: {
            double t2 = j->t + TCA_STEP_S;
            double e = elev(j, t2);
            if (e > j->best_el) { j->best_el = e; j->best_t = t2; }
            if (e < j->mask_deg) {
                finish(j, bisect(j, j->t, t2));
            } else if (t2 - j->aos > MAX_PASS_S) {
                finish(j, t2);
            } else {
                j->t = t2;
            }
            break;
        }
        default:
            j->done = 1;
            break;
        }
    }
    return j->done;
}

uint8_t sat_pred_track(sgp4_sat_t *sgp, const sat_observer_t *obs,
                       const sat_pred_pass_t *p, uint8_t n,
                       uint8_t *az_half, uint8_t *el)
{
    if (!p->los || p->los <= p->aos || n < 2) return 0;
    double span = (double)(p->los - p->aos);
    uint8_t k = 0;
    for (uint8_t i = 0; i < n; i++) {
        sat_look_t lk;
        double t = (double)p->aos + span * (double)i / (double)(n - 1);
        if (sat_pred_elev(sgp, obs, t, &lk) <= -90.0) continue;
        double e = lk.el_deg < 0.0 ? 0.0 : lk.el_deg;
        int a = (int)(lk.az_deg * 0.5 + 0.5);
        az_half[k] = (uint8_t)(a >= 180 ? a - 180 : a);
        el[k] = (uint8_t)(e > 90.0 ? 90 : (int)(e + 0.5));
        k++;
    }
    return k;
}
