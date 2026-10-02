/*
 * sat_pred.h - Incremental pass predictor (AOS / TCA / LOS)
 *
 * Pure computation on top of sat_sgp4: no hardware access, so it runs both
 * on the radio (spread over scheduler ticks with a propagation budget) and
 * on the host test bench (tests/test_pred.py compares with Skyfield).
 */

#ifndef APP_SAT_PRED_H
#define APP_SAT_PRED_H

#include <stdint.h>
#include "app/sat_sgp4.h"

typedef struct {
    uint32_t aos, tca, los;        /* unix UTC, los == 0 => no pass found */
    uint16_t aos_az, tca_az, los_az;
    uint8_t  max_el;
    uint8_t  from_db;
} sat_pred_pass_t;

typedef struct {
    sgp4_sat_t     *sgp;           /* initialised propagator (caller owned) */
    const sat_observer_t *obs;
    double  mask_deg;
    double  t, t_end, t_start;     /* unix seconds (double)                 */
    double  aos, best_t, best_el;
    uint8_t phase;
    uint8_t done;
    uint16_t props;                /* propagations used (statistics)        */
    sat_pred_pass_t result;
} sat_pred_job_t;

/* Start a search for the first pass whose LOS is after start_unix.
 * If the satellite is already above the mask, the current pass is
 * returned (AOS in the past). */
void sat_pred_start(sat_pred_job_t *j, sgp4_sat_t *sgp, const sat_observer_t *obs,
                    double mask_deg, uint32_t start_unix, uint32_t horizon_s);

/* Advance by at most ~budget propagations. Returns 1 when finished. */
uint8_t sat_pred_step(sat_pred_job_t *j, int budget);

/* Elevation helper (returns -90 on propagation error) */
double sat_pred_elev(sgp4_sat_t *sgp, const sat_observer_t *obs, double t_unix,
                     sat_look_t *lk_out);

/* Sample a pass for the polar plot: n points from AOS to LOS.
 * az_half[i] = azimuth/2 (0..179), el[i] = elevation (0..90). */
uint8_t sat_pred_track(sgp4_sat_t *sgp, const sat_observer_t *obs,
                       const sat_pred_pass_t *p, uint8_t n,
                       uint8_t *az_half, uint8_t *el);

#endif /* APP_SAT_PRED_H */
