/*
 * sat_sgp4.c - SGP4 near-Earth propagator and topocentric geometry
 *
 * Port of the near-Earth branch of D. Vallado's public-domain sgp4unit
 * ("Revisiting Spacetrack Report #3", 2006, rev. 2020), WGS-72 constants,
 * operation mode 'i' (improved).  Verified against python-sgp4 in
 * tests/sat_host_test.c (see tools/README_SATELLITE.md).
 */

#include "app/sat_sgp4.h"
#include "app/sat_math.h"

/* WGS-72 gravitational constants (as used to generate TLEs) */
#define RE_KM      6378.135
#define MU         398600.8
#define XKE        0.0743669161331734132      /* 60/sqrt(RE^3/MU) */
#define J2         0.001082616
#define J3        -0.00000253881
#define J4        -0.00000165597
#define J3OJ2      (J3 / J2)
#define X2O3       (2.0 / 3.0)

/* WGS-84 ellipsoid for the observer */
#define WGS84_A    6378.137
#define WGS84_F    (1.0 / 298.257223563)
#define EARTH_ROT  7.29211514670698e-5        /* rad/s */

/* ===================================================================== */

int sgp4_init(sgp4_sat_t *s, double epoch_jd, double bstar,
              double incl_deg, double raan_deg, double ecc,
              double argp_deg, double mean_anom_deg, double mean_motion_rpd)
{
    s->epoch_jd = epoch_jd;
    s->bstar    = bstar;
    s->ecco     = ecc;
    s->inclo    = incl_deg * SM_DEG2RAD;
    s->nodeo    = raan_deg * SM_DEG2RAD;
    s->argpo    = argp_deg * SM_DEG2RAD;
    s->mo       = mean_anom_deg * SM_DEG2RAD;
    s->no_kozai = mean_motion_rpd * SM_TWOPI / 1440.0;   /* rad/min */
    s->error    = SGP4_OK;

    if (s->no_kozai <= 0.0) { s->error = SGP4_ERR_MEANMOT; return s->error; }
    if (ecc < 0.0 || ecc >= 1.0) { s->error = SGP4_ERR_ECC; return s->error; }

    /* ---- initl ---- */
    double eccsq  = s->ecco * s->ecco;
    double omeosq = 1.0 - eccsq;
    double rteosq = sm_sqrt(omeosq);
    double cosio, sinio;
    sm_sincos(s->inclo, &sinio, &cosio);
    double cosio2 = cosio * cosio;

    double ak   = sm_cbrt(XKE / s->no_kozai); ak *= ak;      /* (xke/n)^(2/3) */
    double d1   = 0.75 * J2 * (3.0 * cosio2 - 1.0) / (rteosq * omeosq);
    double del  = d1 / (ak * ak);
    double adel = ak * (1.0 - del * del - del * (1.0 / 3.0 + 134.0 * del * del / 81.0));
    del = d1 / (adel * adel);
    s->no_unkozai = s->no_kozai / (1.0 + del);

    double ao = sm_cbrt(XKE / s->no_unkozai); ao *= ao;
    s->ao = ao;
    double po    = ao * omeosq;
    double con42 = 1.0 - 5.0 * cosio2;
    s->con41     = -con42 - cosio2 - cosio2;
    double posq  = po * po;
    double rp    = ao * (1.0 - s->ecco);

    if (SM_TWOPI / s->no_unkozai >= 225.0) {
        s->error = SGP4_ERR_DEEPSPACE;
        return s->error;
    }

    /* ---- sgp4init ---- */
    double ss     = 78.0 / RE_KM + 1.0;
    double qzms2t = (120.0 - 78.0) / RE_KM;
    qzms2t = qzms2t * qzms2t * qzms2t * qzms2t;

    s->isimp = (rp < (220.0 / RE_KM + 1.0)) ? 1 : 0;

    double sfour  = ss;
    double qzms24 = qzms2t;
    double perige = (rp - 1.0) * RE_KM;
    if (perige < 156.0) {
        sfour = perige - 78.0;
        if (perige < 98.0) sfour = 20.0;
        double q = (120.0 - sfour) / RE_KM;
        qzms24 = q * q * q * q;
        sfour = sfour / RE_KM + 1.0;
    }

    double pinvsq = 1.0 / posq;
    double tsi    = 1.0 / (ao - sfour);
    s->eta        = ao * s->ecco * tsi;
    double etasq  = s->eta * s->eta;
    double eeta   = s->ecco * s->eta;
    double psisq  = sm_fabs(1.0 - etasq);
    double tsi2   = tsi * tsi;
    double coef   = qzms24 * tsi2 * tsi2;
    double psisq35 = psisq * psisq * psisq * sm_sqrt(psisq);       /* ^3.5 */
    double coef1  = coef / psisq35;
    double cc2 = coef1 * s->no_unkozai *
                 (ao * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
                  0.375 * J2 * tsi / psisq * s->con41 *
                  (8.0 + 3.0 * etasq * (8.0 + etasq)));
    s->cc1 = s->bstar * cc2;
    double cc3 = 0.0;
    if (s->ecco > 1.0e-4)
        cc3 = -2.0 * coef * tsi * J3OJ2 * s->no_unkozai * sinio / s->ecco;
    s->x1mth2 = 1.0 - cosio2;
    s->cc4 = 2.0 * s->no_unkozai * coef1 * ao * omeosq *
             (s->eta * (2.0 + 0.5 * etasq) + s->ecco *
              (0.5 + 2.0 * etasq) - J2 * tsi / (ao * psisq) *
              (-3.0 * s->con41 * (1.0 - 2.0 * eeta + etasq *
              (1.5 - 0.5 * eeta)) + 0.75 * s->x1mth2 *
              (2.0 * etasq - eeta * (1.0 + etasq)) * sm_cos(2.0 * s->argpo)));
    s->cc5 = 2.0 * coef1 * ao * omeosq * (1.0 + 2.75 *
             (etasq + eeta) + eeta * etasq);

    double cosio4 = cosio2 * cosio2;
    double temp1  = 1.5 * J2 * pinvsq * s->no_unkozai;
    double temp2  = 0.5 * temp1 * J2 * pinvsq;
    double temp3  = -0.46875 * J4 * pinvsq * pinvsq * s->no_unkozai;
    s->mdot    = s->no_unkozai + 0.5 * temp1 * rteosq * s->con41 + 0.0625 *
                 temp2 * rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
    s->argpdot = -0.5 * temp1 * con42 + 0.0625 * temp2 *
                 (7.0 - 114.0 * cosio2 + 395.0 * cosio4) +
                 temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
    double xhdot1 = -temp1 * cosio;
    s->nodedot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) +
                 2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;
    s->omgcof  = s->bstar * cc3 * sm_cos(s->argpo);
    s->xmcof   = 0.0;
    if (s->ecco > 1.0e-4)
        s->xmcof = -X2O3 * coef * s->bstar / eeta;
    s->nodecf = 3.5 * omeosq * xhdot1 * s->cc1;
    s->t2cof  = 1.5 * s->cc1;
    if (sm_fabs(cosio + 1.0) > 1.5e-12)
        s->xlcof = -0.25 * J3OJ2 * sinio * (3.0 + 5.0 * cosio) / (1.0 + cosio);
    else
        s->xlcof = -0.25 * J3OJ2 * sinio * (3.0 + 5.0 * cosio) / 1.5e-12;
    s->aycof  = -0.5 * J3OJ2 * sinio;
    double dm = 1.0 + s->eta * sm_cos(s->mo);
    s->delmo  = dm * dm * dm;
    s->sinmao = sm_sin(s->mo);
    s->x7thm1 = 7.0 * cosio2 - 1.0;

    s->d2 = s->d3 = s->d4 = 0.0;
    s->t3cof = s->t4cof = s->t5cof = 0.0;
    if (s->isimp != 1) {
        double cc1sq = s->cc1 * s->cc1;
        s->d2 = 4.0 * ao * tsi * cc1sq;
        double temp = s->d2 * tsi * s->cc1 / 3.0;
        s->d3 = (17.0 * ao + sfour) * temp;
        s->d4 = 0.5 * temp * ao * tsi * (221.0 * ao + 31.0 * sfour) * s->cc1;
        s->t3cof = s->d2 + 2.0 * cc1sq;
        s->t4cof = 0.25 * (3.0 * s->d3 + s->cc1 * (12.0 * s->d2 + 10.0 * cc1sq));
        s->t5cof = 0.2 * (3.0 * s->d4 + 12.0 * s->cc1 * s->d3 +
                   6.0 * s->d2 * s->d2 + 15.0 * cc1sq * (2.0 * s->d2 + cc1sq));
    }
    return SGP4_OK;
}

/* ===================================================================== */

int sgp4_propagate(sgp4_sat_t *s, double t, vec3_t *r, vec3_t *v)
{
    if (s->error == SGP4_ERR_DEEPSPACE) return s->error;

    double xmdf   = s->mo + s->mdot * t;
    double argpdf = s->argpo + s->argpdot * t;
    double nodedf = s->nodeo + s->nodedot * t;
    double argpm  = argpdf;
    double mm     = xmdf;
    double t2     = t * t;
    double nodem  = nodedf + s->nodecf * t2;
    double tempa  = 1.0 - s->cc1 * t;
    double tempe  = s->bstar * s->cc4 * t;
    double templ  = s->t2cof * t2;

    if (s->isimp != 1) {
        double delomg = s->omgcof * t;
        double dm = 1.0 + s->eta * sm_cos(xmdf);
        double delm = s->xmcof * (dm * dm * dm - s->delmo);
        double temp = delomg + delm;
        mm    = xmdf + temp;
        argpm = argpdf - temp;
        double t3 = t2 * t;
        double t4 = t3 * t;
        tempa = tempa - s->d2 * t2 - s->d3 * t3 - s->d4 * t4;
        tempe = tempe + s->bstar * s->cc5 * (sm_sin(mm) - s->sinmao);
        templ = templ + s->t3cof * t3 + t4 * (s->t4cof + t * s->t5cof);
    }

    double nm    = s->no_unkozai;
    double em    = s->ecco;
    double inclm = s->inclo;
    if (nm <= 0.0) return SGP4_ERR_MEANMOT;

    double am = sm_cbrt(XKE / nm); am = am * am * tempa * tempa;
    nm = XKE / (am * sm_sqrt(am));
    em = em - tempe;
    if (em >= 1.0 || em < -0.001) return SGP4_ERR_ECC;
    if (em < 1.0e-6) em = 1.0e-6;
    mm = mm + s->no_unkozai * templ;
    double xlm = mm + argpm + nodem;

    nodem = sm_fmod(nodem, SM_TWOPI);
    argpm = sm_fmod(argpm, SM_TWOPI);
    xlm   = sm_fmod(xlm, SM_TWOPI);
    mm    = sm_fmod(xlm - argpm - nodem, SM_TWOPI);

    double sinip, cosip;
    sm_sincos(inclm, &sinip, &cosip);

    /* long period periodics */
    double sinargp, cosargp;
    sm_sincos(argpm, &sinargp, &cosargp);
    double axnl = em * cosargp;
    double temp = 1.0 / (am * (1.0 - em * em));
    double aynl = em * sinargp + temp * s->aycof;
    double xl   = mm + argpm + nodem + temp * s->xlcof * axnl;

    /* Kepler's equation */
    double u   = sm_fmod(xl - nodem, SM_TWOPI);
    double eo1 = u;
    double tem5 = 9999.9;
    double sineo1 = 0.0, coseo1 = 1.0;
    int ktr = 1;
    while (sm_fabs(tem5) >= 1.0e-12 && ktr <= 10) {
        sm_sincos(eo1, &sineo1, &coseo1);
        tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
        tem5 = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
        if (sm_fabs(tem5) >= 0.95)
            tem5 = (tem5 > 0.0) ? 0.95 : -0.95;
        eo1 = eo1 + tem5;
        ktr++;
    }

    /* short period preliminary quantities */
    double ecose = axnl * coseo1 + aynl * sineo1;
    double esine = axnl * sineo1 - aynl * coseo1;
    double el2   = axnl * axnl + aynl * aynl;
    double pl    = am * (1.0 - el2);
    if (pl < 0.0) return SGP4_ERR_SEMILATUS;

    double rl     = am * (1.0 - ecose);
    double rdotl  = sm_sqrt(am) * esine / rl;
    double rvdotl = sm_sqrt(pl) / rl;
    double betal  = sm_sqrt(1.0 - el2);
    temp = esine / (1.0 + betal);
    double sinu = am / rl * (sineo1 - aynl - axnl * temp);
    double cosu = am / rl * (coseo1 - axnl + aynl * temp);
    double su   = sm_atan2(sinu, cosu);
    double sin2u = (cosu + cosu) * sinu;
    double cos2u = 1.0 - 2.0 * sinu * sinu;
    temp = 1.0 / pl;
    double temp1 = 0.5 * J2 * temp;
    double temp2 = temp1 * temp;

    /* update for short period periodics */
    double mrt   = rl * (1.0 - 1.5 * temp2 * betal * s->con41) +
                   0.5 * temp1 * s->x1mth2 * cos2u;
    su           = su - 0.25 * temp2 * s->x7thm1 * sin2u;
    double xnode = nodem + 1.5 * temp2 * cosip * sin2u;
    double xinc  = inclm + 1.5 * temp2 * cosip * sinip * cos2u;
    double mvt   = rdotl - nm * temp1 * s->x1mth2 * sin2u / XKE;
    double rvdot = rvdotl + nm * temp1 * (s->x1mth2 * cos2u + 1.5 * s->con41) / XKE;

    /* orientation vectors */
    double sinsu, cossu, snod, cnod, sini, cosi;
    sm_sincos(su, &sinsu, &cossu);
    sm_sincos(xnode, &snod, &cnod);
    sm_sincos(xinc, &sini, &cosi);
    double xmx = -snod * cosi;
    double xmy =  cnod * cosi;
    double ux  =  xmx * sinsu + cnod * cossu;
    double uy  =  xmy * sinsu + snod * cossu;
    double uz  =  sini * sinsu;
    double vx  =  xmx * cossu - cnod * sinsu;
    double vy  =  xmy * cossu - snod * sinsu;
    double vz  =  sini * cossu;

    const double vkmpersec = RE_KM * XKE / 60.0;
    r->x = mrt * ux * RE_KM;
    r->y = mrt * uy * RE_KM;
    r->z = mrt * uz * RE_KM;
    v->x = (mvt * ux + rvdot * vx) * vkmpersec;
    v->y = (mvt * uy + rvdot * vy) * vkmpersec;
    v->z = (mvt * uz + rvdot * vz) * vkmpersec;

    if (mrt < 1.0) return SGP4_ERR_DECAYED;
    return SGP4_OK;
}

/* ===================================================================== */

double sgp4_gmst(double jdut1)
{
    double tut1 = (jdut1 - 2451545.0) / 36525.0;
    double temp = -6.2e-6 * tut1 * tut1 * tut1 + 0.093104 * tut1 * tut1 +
                  (876600.0 * 3600.0 + 8640184.812866) * tut1 + 67310.54841;
    temp = sm_fmod(temp * SM_DEG2RAD / 240.0, SM_TWOPI);
    if (temp < 0.0) temp += SM_TWOPI;
    return temp;
}

void sat_observer_set(sat_observer_t *o, double lat_deg, double lon_deg, double alt_m)
{
    o->lat_rad = lat_deg * SM_DEG2RAD;
    o->lon_rad = lon_deg * SM_DEG2RAD;
    o->alt_km  = alt_m / 1000.0;
    sm_sincos(o->lat_rad, &o->sin_lat, &o->cos_lat);
    sm_sincos(o->lon_rad, &o->sin_lon, &o->cos_lon);
    double e2 = WGS84_F * (2.0 - WGS84_F);
    double n  = WGS84_A / sm_sqrt(1.0 - e2 * o->sin_lat * o->sin_lat);
    o->ecef.x = (n + o->alt_km) * o->cos_lat * o->cos_lon;
    o->ecef.y = (n + o->alt_km) * o->cos_lat * o->sin_lon;
    o->ecef.z = (n * (1.0 - e2) + o->alt_km) * o->sin_lat;
}

int sat_look(sgp4_sat_t *s, const sat_observer_t *o, double jd, sat_look_t *out)
{
    vec3_t r, v;
    double tsince = (jd - s->epoch_jd) * 1440.0;
    int err = sgp4_propagate(s, tsince, &r, &v);
    if (err != SGP4_OK) return err;

    /* TEME -> pseudo Earth-fixed (rotate by GMST, polar motion ignored) */
    double g = sgp4_gmst(jd);
    double sg, cg;
    sm_sincos(g, &sg, &cg);
    vec3_t re = {  cg * r.x + sg * r.y, -sg * r.x + cg * r.y, r.z };
    /* Earth-fixed velocity: rotate, then subtract omega x r */
    vec3_t ve = {  cg * v.x + sg * v.y + EARTH_ROT * re.y,
                  -sg * v.x + cg * v.y - EARTH_ROT * re.x,
                   v.z };

    double rx = re.x - o->ecef.x;
    double ry = re.y - o->ecef.y;
    double rz = re.z - o->ecef.z;
    double range = sm_sqrt(rx * rx + ry * ry + rz * rz);

    /* SEZ topocentric */
    double south = o->sin_lat * o->cos_lon * rx + o->sin_lat * o->sin_lon * ry - o->cos_lat * rz;
    double east  = -o->sin_lon * rx + o->cos_lon * ry;
    double zen   = o->cos_lat * o->cos_lon * rx + o->cos_lat * o->sin_lon * ry + o->sin_lat * rz;

    double az = sm_atan2(east, -south) * SM_RAD2DEG;
    if (az < 0.0) az += 360.0;

    out->az_deg   = az;
    out->el_deg   = sm_asin(zen / range) * SM_RAD2DEG;
    out->range_km = range;
    out->range_rate_kms = (rx * ve.x + ry * ve.y + rz * ve.z) / range;

    /* Sub-satellite point (spherical approximation is fine for display) */
    double rxy = sm_sqrt(re.x * re.x + re.y * re.y);
    out->sub_lat_deg = sm_atan2(re.z, rxy) * SM_RAD2DEG;
    out->sub_lon_deg = sm_atan2(re.y, re.x) * SM_RAD2DEG;
    out->alt_km = sm_sqrt(re.x * re.x + re.y * re.y + re.z * re.z) - RE_KM;
    return SGP4_OK;
}

/* ===================================================================== */

double sat_jd_from_ymdhms(int year, int month, int day, int h, int m, double s)
{
    /* Vallado jday(): valid 1900..2100 */
    double jd = 367.0 * year -
                (double)((7 * (year + ((month + 9) / 12))) / 4) +
                (double)((275 * month) / 9) +
                day + 1721013.5;
    return jd + ((s / 60.0 + m) / 60.0 + h) / 24.0;
}

double sat_unix_to_jd(uint32_t unix_s, uint16_t ms)
{
    return 2440587.5 + ((double)unix_s + (double)ms / 1000.0) / 86400.0;
}

static int days_from_civil(int y, int m, int d)
{
    /* H. Hinnant's algorithm, days since 1970-01-01 */
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint32_t sat_ymdhms_to_unix(int year, int month, int day, int h, int m, int s)
{
    int32_t days = days_from_civil(year, month, day);
    return (uint32_t)days * 86400U + (uint32_t)(h * 3600 + m * 60 + s);
}

void sat_unix_to_ymdhms(uint32_t t, int *year, int *month, int *day,
                        int *h, int *m, int *s)
{
    int32_t z = (int32_t)(t / 86400U);
    uint32_t sod = t % 86400U;
    *h = (int)(sod / 3600U);
    *m = (int)((sod / 60U) % 60U);
    *s = (int)(sod % 60U);
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = yoe + era * 400;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    int32_t d = doy - (153 * mp + 2) / 5 + 1;
    int32_t mo = mp < 10 ? mp + 3 : mp - 9;
    *year = (int)(y + (mo <= 2));
    *month = (int)mo;
    *day = (int)d;
}
