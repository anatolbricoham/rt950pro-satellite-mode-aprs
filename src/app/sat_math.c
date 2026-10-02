/*
 * sat_math.c - Double precision math kernels for the satellite module
 *
 * Polynomials and reduction constants from FreeBSD msun / fdlibm
 * (Copyright (C) 1993 by Sun Microsystems, Inc. - "Permission to use, copy,
 * modify, and distribute this software is freely granted, provided that this
 * notice is preserved.").  Simplified: no NaN/Inf handling (inputs from SGP4
 * are always finite), Cody-Waite argument reduction valid for |x| < 1e6.
 */

#include "app/sat_math.h"
#include <stdint.h>

typedef union { double d; uint64_t u; } dbits_t;

double sm_fabs(double x) { return (x < 0.0) ? -x : x; }

double sm_floor(double x)
{
    /* |x| < 2^52 for every caller; larger values are already integral. */
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    int64_t i = (int64_t)x;
    double  f = (double)i;
    if (f > x) f -= 1.0;
    return f;
}

double sm_fmod(double x, double y)
{
    double q = x / y;
    q = (q < 0.0) ? -sm_floor(-q) : sm_floor(q);   /* trunc toward zero */
    return x - q * y;
}

double sm_wrap_2pi(double x)
{
    double r = sm_fmod(x, SM_TWOPI);
    if (r < 0.0) r += SM_TWOPI;
    return r;
}

double sm_sqrt(double x)
{
    if (x <= 0.0) return 0.0;
    dbits_t b; b.d = x;
    b.u = (b.u >> 1) + 0x1FF8000000000000ULL;       /* ~6 % initial error */
    double y = b.d;
    for (int i = 0; i < 5; i++)
        y = 0.5 * (y + x / y);
    return y;
}

double sm_cbrt(double x)
{
    if (x == 0.0) return 0.0;
    int neg = (x < 0.0);
    if (neg) x = -x;
    dbits_t b; b.d = x;
    b.u = b.u / 3 + 0x2A9F7893782DA1CEULL;
    double y = b.d;
    for (int i = 0; i < 6; i++)
        y = y - (y * y * y - x) / (3.0 * y * y);
    return neg ? -y : y;
}

/* ---- sin / cos ------------------------------------------------------- */

static const double S1 = -1.66666666666666324348e-01;
static const double S2 =  8.33333333332248946124e-03;
static const double S3 = -1.98412698298579493134e-04;
static const double S4 =  2.75573137070700676789e-06;
static const double S5 = -2.50507602534068634195e-08;
static const double S6 =  1.58969099521155010221e-10;

static const double C1 =  4.16666666666666019037e-02;
static const double C2 = -1.38888888888741095749e-03;
static const double C3 =  2.48015872894767294178e-05;
static const double C4 = -2.75573143513906633035e-07;
static const double C5 =  2.08757232129817482790e-09;
static const double C6 = -1.13596475577881948265e-11;

static double k_sin(double x)
{
    double z = x * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x + x * z * (S1 + z * r);
}

static double k_cos(double x)
{
    double z = x * x;
    double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    double hz = 0.5 * z;
    double w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + z * r);
}

/* Reduce x to r in [-pi/4, pi/4], return quadrant n (mod 4). */
static int reduce_pio2(double x, double *r)
{
    static const double invpio2 = 6.36619772367581382433e-01;
    static const double pio2_1  = 1.57079632673412561417e+00;
    static const double pio2_2  = 6.07710050630396597660e-11;
    static const double pio2_2t = 2.02226624879595063154e-21;

    double fn = sm_floor(x * invpio2 + 0.5);
    int64_t n = (int64_t)fn;
    /* Two-step Cody-Waite reduction (fdlibm __ieee754_rem_pio2, medium) */
    double t = x - fn * pio2_1;
    double w = fn * pio2_2;
    double y = t - w;
    w = fn * pio2_2t - ((t - y) - w);
    *r = y - w;
    return (int)(n & 3);
}

void sm_sincos(double x, double *s, double *c)
{
    double r;
    int n = reduce_pio2(x, &r);
    double sr = k_sin(r), cr = k_cos(r);
    switch (n) {
    case 0:  *s =  sr; *c =  cr; break;
    case 1:  *s =  cr; *c = -sr; break;
    case 2:  *s = -sr; *c = -cr; break;
    default: *s = -cr; *c =  sr; break;
    }
}

double sm_sin(double x) { double s, c; sm_sincos(x, &s, &c); return s; }
double sm_cos(double x) { double s, c; sm_sincos(x, &s, &c); return c; }

/* ---- atan family ----------------------------------------------------- */

static const double atanhi[4] = {
    4.63647609000806093515e-01, 7.85398163397448278999e-01,
    9.82793723247329054082e-01, 1.57079632679489655800e+00,
};
static const double atanlo[4] = {
    2.26987774529616870924e-17, 3.06161699786838301793e-17,
    1.39033110312309984516e-17, 6.12323399573676603587e-17,
};
static const double aT[11] = {
     3.33333333333329318027e-01, -1.99999999998764832476e-01,
     1.42857142725034663711e-01, -1.11111104054623557880e-01,
     9.09088713343650656196e-02, -7.69187620504482999495e-02,
     6.66107313738753120669e-02, -5.83357013379057348645e-02,
     4.97687799461593236017e-02, -3.65315727442169155270e-02,
     1.62858201153657823623e-02,
};

double sm_atan(double x)
{
    int neg = (x < 0.0);
    double ax = neg ? -x : x;
    int id;

    if (ax > 1.0e17)
        return neg ? -atanhi[3] : atanhi[3];

    if (ax < 0.4375) {
        id = -1;
    } else if (ax < 1.1875) {
        if (ax < 0.6875) { id = 0; ax = (2.0 * ax - 1.0) / (2.0 + ax); }
        else             { id = 1; ax = (ax - 1.0) / (ax + 1.0); }
    } else {
        if (ax < 2.4375) { id = 2; ax = (ax - 1.5) / (1.0 + 1.5 * ax); }
        else             { id = 3; ax = -1.0 / ax; }
    }

    double z = ax * ax;
    double w = z * z;
    double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));

    double res;
    if (id < 0)
        res = ax - ax * (s1 + s2);
    else
        res = atanhi[id] - ((ax * (s1 + s2) - atanlo[id]) - ax);
    return neg ? -res : res;
}

double sm_atan2(double y, double x)
{
    if (x == 0.0) {
        if (y > 0.0) return SM_HALFPI;
        if (y < 0.0) return -SM_HALFPI;
        return 0.0;
    }
    double a = sm_atan(y / x);
    if (x > 0.0) return a;
    return (y >= 0.0) ? a + SM_PI : a - SM_PI;
}

double sm_asin(double x)
{
    if (x >= 1.0)  return SM_HALFPI;
    if (x <= -1.0) return -SM_HALFPI;
    return sm_atan2(x, sm_sqrt((1.0 - x) * (1.0 + x)));
}

double sm_acos(double x)
{
    if (x >= 1.0)  return 0.0;
    if (x <= -1.0) return SM_PI;
    return sm_atan2(sm_sqrt((1.0 - x) * (1.0 + x)), x);
}
