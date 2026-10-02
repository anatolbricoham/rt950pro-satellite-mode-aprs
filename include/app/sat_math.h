/*
 * sat_math.h - Self-contained double precision math for satellite tracking
 *
 * The firmware links with -nostdlib (no libm), so the SGP4 propagator uses
 * these small, deterministic implementations (fdlibm-derived polynomials).
 * Accuracy is ~1e-15 relative on the ranges used by SGP4, far better than
 * the TLE model itself (~1 km).  All functions are pure and re-entrant.
 */

#ifndef APP_SAT_MATH_H
#define APP_SAT_MATH_H

#define SM_PI       3.14159265358979323846
#define SM_TWOPI    6.28318530717958647692
#define SM_HALFPI   1.57079632679489661923
#define SM_DEG2RAD  (SM_PI / 180.0)
#define SM_RAD2DEG  (180.0 / SM_PI)

double sm_fabs(double x);
double sm_floor(double x);
double sm_fmod(double x, double y);     /* result has the sign of x       */
double sm_wrap_2pi(double x);           /* result in [0, 2*pi)            */
double sm_sqrt(double x);
double sm_cbrt(double x);
double sm_sin(double x);
double sm_cos(double x);
void   sm_sincos(double x, double *s, double *c);
double sm_atan(double x);
double sm_atan2(double y, double x);
double sm_asin(double x);
double sm_acos(double x);

#endif /* APP_SAT_MATH_H */
