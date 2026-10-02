/*
 * sat_sgp4.h - SGP4 orbit propagator (near-Earth) + observer geometry
 *
 * Implementation follows Vallado et al., "Revisiting Spacetrack Report #3"
 * (AIAA 2006-6753), WGS-72 constants, "improved" operation mode.  Deep-space
 * (SDP4, period >= 225 min) is NOT implemented: every amateur FM/linear LEO
 * satellite is near-Earth.  Records flagged deep-space are rejected.
 *
 * Units: positions km, velocities km/s, angles radians, time Julian date UTC.
 */

#ifndef APP_SAT_SGP4_H
#define APP_SAT_SGP4_H

#include <stdint.h>

typedef enum {
    SGP4_OK = 0,
    SGP4_ERR_ECC       = 1,   /* eccentricity out of range           */
    SGP4_ERR_MEANMOT   = 2,   /* mean motion < 0                      */
    SGP4_ERR_SEMILATUS = 4,   /* semi-latus rectum < 0                */
    SGP4_ERR_DECAYED   = 6,   /* satellite has decayed                */
    SGP4_ERR_DEEPSPACE = 10,  /* period >= 225 min: SDP4 not built in */
} sgp4_err_t;

/* Initialised propagator state for one satellite */
typedef struct {
    double epoch_jd;
    /* mean elements (rad, rad/min) */
    double ecco, inclo, nodeo, argpo, mo, no_kozai, no_unkozai, bstar;
    /* derived constants */
    int    isimp;
    double ao, con41, cc1, cc4, cc5, d2, d3, d4, delmo, eta, argpdot,
           omgcof, sinmao, t2cof, t3cof, t4cof, t5cof, x1mth2, x7thm1,
           mdot, nodedot, xlcof, xmcof, nodecf, aycof;
    int    error;
} sgp4_sat_t;

typedef struct { double x, y, z; } vec3_t;

/*
 * Initialise from mean elements as found in a TLE.
 *   epoch_jd   : Julian date (UTC) of the element set epoch
 *   angles in degrees, mean motion in revolutions/day, bstar as in TLE
 * Returns SGP4_OK or an sgp4_err_t.
 */
int sgp4_init(sgp4_sat_t *s, double epoch_jd, double bstar,
              double incl_deg, double raan_deg, double ecc,
              double argp_deg, double mean_anom_deg, double mean_motion_rpd);

/* Propagate to tsince minutes from epoch. Position/velocity in TEME frame. */
int sgp4_propagate(sgp4_sat_t *s, double tsince_min, vec3_t *r_km, vec3_t *v_kms);

/* Greenwich mean sidereal time (IAU-82), radians in [0, 2pi). */
double sgp4_gmst(double jd_ut1);

/* Observer on the WGS-84 ellipsoid */
typedef struct {
    double lat_rad, lon_rad, alt_km;
    /* cached */
    double sin_lat, cos_lat, sin_lon, cos_lon;
    vec3_t ecef;              /* km */
} sat_observer_t;

void sat_observer_set(sat_observer_t *o, double lat_deg, double lon_deg, double alt_m);

/* Topocentric look angles */
typedef struct {
    double az_deg;            /* 0 = North, 90 = East                  */
    double el_deg;            /* above local horizon                   */
    double range_km;
    double range_rate_kms;    /* + = receding (frequency goes down)    */
    double sub_lat_deg, sub_lon_deg, alt_km;
} sat_look_t;

/*
 * Propagate satellite to Julian date jd and compute look angles from the
 * observer.  Returns SGP4_OK or an error code from the propagator.
 */
int sat_look(sgp4_sat_t *s, const sat_observer_t *o, double jd, sat_look_t *out);

/* Julian date helpers */
double sat_unix_to_jd(uint32_t unix_s, uint16_t ms);
double sat_jd_from_ymdhms(int year, int month, int day, int h, int m, double s);
uint32_t sat_ymdhms_to_unix(int year, int month, int day, int h, int m, int s);
void sat_unix_to_ymdhms(uint32_t t, int *year, int *month, int *day,
                        int *h, int *m, int *s);

#endif /* APP_SAT_SGP4_H */
