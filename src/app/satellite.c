/*
 * satellite.c - Satellite tracking engine for the RT-950 Pro
 *
 * Responsibilities:
 *   - load / validate the satellite database written by tools/rt950_sat.py
 *   - keep a UTC clock (GPS RMC, PC over CPS, or manual)
 *   - choose the observer position (GPS fix, DB QTH, manual)
 *   - predict the next pass of every satellite in the background
 *   - while engaged: propagate the selected satellite once per second and
 *     drive VFO A (downlink RX) and VFO B (uplink TX) with Doppler
 *   - PTT hooks so radio.c transmits on the corrected uplink
 *
 * CPU budget: one SGP4 propagation costs ~1 ms on the AT32F403A (soft
 * double).  sat_poll() runs every 100 ms and spends at most PRED_BUDGET
 * propagations on background prediction, so the UI stays responsive.
 */

#include "app/satellite.h"
#include "app/sat_math.h"
#include "app/vfo.h"
#include "app/gps.h"
#include "app/audio.h"
#include "drivers/spi.h"
#include "drivers/bk4829.h"
#include "kernel/scheduler.h"
#include "app/cps.h"

#include <stddef.h>
#include <string.h>

extern uint32_t get_tick(void);

#define SPEED_OF_LIGHT_KMS   299792.458
#define PRED_BUDGET          12          /* propagations per sat_poll()   */
#define PRED_HORIZON_S       (36U * 3600U)
#define TRACK_PERIOD_MS      1000U
#define QTH_MATCH_KM         25.0        /* DB pass table usable within   */
#define RX_QUANT_HZ          100
#define TX_QUANT_HZ          100

/* ---- state ----------------------------------------------------------- */

static sat_db_header_t hdr;
static sat_db_sat_t    sats[SAT_DB_MAX_SATS];
static uint8_t         n_sats;
static uint8_t         db_ok;

static sat_cfg_t       cfg;

/* clock */
static uint32_t        clk_base_unix;
static uint32_t        clk_base_tick;
static sat_time_src_t  clk_src = SAT_TIME_NONE;
static uint8_t         last_rmc_seq;

/* observer */
static sat_observer_t  obs;
static sat_pos_src_t   obs_src = SAT_POS_NONE;
static double          obs_lat = 1000.0, obs_lon, obs_alt;

/* background prediction */
static sat_pass_t      next_pass[SAT_DB_MAX_SATS];
static uint8_t         pass_valid[SAT_DB_MAX_SATS];
static sgp4_sat_t      pred_sgp;
static sat_pred_job_t  pred_job;
static uint8_t         pred_idx = 0xFF;      /* job in progress for idx */
static uint8_t         pred_scan;            /* round-robin cursor      */

/* pass list job (pass screen) */
static sgp4_sat_t      pl_sgp;
static sat_pred_job_t  pl_job;
static sat_pass_t      pl_res[SAT_PASSLIST_MAX];
static uint8_t         pl_count, pl_busy, pl_idx;

/* tracking */
static uint8_t         engaged;
static sat_track_t     trk;
static sgp4_sat_t      trk_sgp;
static uint32_t        trk_last_tick;
static uint32_t        applied_rx, applied_tx;
static uint8_t         prev_active_vfo;
static vfo_state_t     saved_a, saved_b;
static uint8_t         dw_was_on;
static uint8_t         tx_active;            /* sat TX in progress */

/* alert */
static uint32_t        alert_done_aos[SAT_DB_MAX_SATS];
static uint32_t        alert_blink_until;

/* ===================================================================== */
/*  CRC-32                                                                */
/* ===================================================================== */

uint32_t sat_crc32(uint32_t crc, const uint8_t *p, uint32_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
    return ~crc;
}

/* ===================================================================== */
/*  Configuration                                                         */
/* ===================================================================== */

static void cfg_defaults(void)
{
    memset(&cfg, 0, sizeof cfg);
    cfg.magic      = SAT_CFG_MAGIC;
    cfg.version    = SAT_CFG_VERSION;
    cfg.min_el_deg = 0;
    cfg.doppler_on = 1;
    cfg.tx_mode    = SAT_TX_SAME_VFO;
    cfg.loc_src    = SAT_LOC_AUTO;
    cfg.alert_min  = 2;
    cfg.rx_wide    = 1;
}

static void cfg_load(void)
{
    sat_cfg_t c;
    spi_flash_read(SAT_CFG_FLASH_ADDR, (uint8_t *)&c, sizeof c);
    if (c.magic == SAT_CFG_MAGIC && c.version == SAT_CFG_VERSION &&
        c.crc32 == sat_crc32(0, (const uint8_t *)&c, offsetof(sat_cfg_t, crc32))) {
        cfg = c;
    } else {
        cfg_defaults();
    }
}

static void cfg_save(void)
{
    cfg.crc32 = sat_crc32(0, (const uint8_t *)&cfg, offsetof(sat_cfg_t, crc32));
    spi_flash_erase_4k(SAT_CFG_FLASH_ADDR);
    spi_flash_write_page(SAT_CFG_FLASH_ADDR, (const uint8_t *)&cfg, sizeof cfg);
}

const sat_cfg_t *sat_cfg(void) { return &cfg; }

static void invalidate_predictions(void)
{
    memset(pass_valid, 0, sizeof pass_valid);
    pred_idx = 0xFF;
    pred_scan = 0;
    pl_busy = 0;
    pl_count = 0;
}

void sat_cfg_set_min_el(uint8_t v)  { if (v > 45) v = 45; cfg.min_el_deg = (int8_t)v; cfg_save(); invalidate_predictions(); }
void sat_cfg_set_doppler(uint8_t v) { cfg.doppler_on = v ? 1 : 0; cfg_save(); trk_last_tick = 0; }
void sat_cfg_set_tx_mode(uint8_t v) { cfg.tx_mode = v ? SAT_TX_VFO_B : SAT_TX_SAME_VFO; cfg_save(); }
void sat_cfg_set_loc_src(uint8_t v) { cfg.loc_src = (v <= SAT_LOC_MANUAL) ? v : SAT_LOC_AUTO; cfg_save(); obs_lat = 1000.0; }
void sat_cfg_set_alert(uint8_t v)   { cfg.alert_min = (v > 15) ? 15 : v; cfg_save(); }
void sat_cfg_set_rx_wide(uint8_t v)
{
    cfg.rx_wide = v ? 1 : 0;
    cfg_save();
    if (engaged) {
        vfo_set_bandwidth(RADIO_VFO_A, cfg.rx_wide);
        vfo_apply(RADIO_VFO_A);
    }
}

/* ===================================================================== */
/*  Database                                                              */
/* ===================================================================== */

int sat_db_reload(void)
{
    uint8_t buf[128];

    db_ok = 0;
    n_sats = 0;
    invalidate_predictions();
    if (engaged) sat_disengage();

    spi_flash_read(SAT_DB_FLASH_ADDR, (uint8_t *)&hdr, sizeof hdr);
    if (hdr.magic != SAT_DB_MAGIC || hdr.version != SAT_DB_VERSION ||
        hdr.header_size != sizeof(sat_db_header_t) ||
        hdr.sat_rec_size != sizeof(sat_db_sat_t) ||
        hdr.pass_rec_size != sizeof(sat_db_pass_t) ||
        hdr.sat_count > SAT_DB_MAX_SATS || hdr.pass_count > SAT_DB_MAX_PASSES)
        return -1;
    if (hdr.header_crc32 != sat_crc32(0, (const uint8_t *)&hdr, 60))
        return -1;

    uint32_t payload = (uint32_t)hdr.sat_count * sizeof(sat_db_sat_t) +
                       (uint32_t)hdr.pass_count * sizeof(sat_db_pass_t);
    if (sizeof(sat_db_header_t) + payload > SAT_DB_FLASH_SIZE)
        return -1;

    uint32_t crc = 0;
    for (uint32_t off = 0; off < payload; off += sizeof buf) {
        uint16_t n = (uint16_t)((payload - off) > sizeof buf ? sizeof buf : (payload - off));
        spi_flash_read(SAT_DB_FLASH_ADDR + sizeof(sat_db_header_t) + off, buf, n);
        crc = sat_crc32(crc, buf, n);
    }
    if (crc != hdr.payload_crc32)
        return -1;

    for (uint8_t i = 0; i < hdr.sat_count; i++) {
        spi_flash_read(SAT_DB_FLASH_ADDR + sizeof(sat_db_header_t) +
                       (uint32_t)i * sizeof(sat_db_sat_t),
                       (uint8_t *)&sats[i], sizeof(sat_db_sat_t));
    }
    n_sats = (uint8_t)hdr.sat_count;
    db_ok = 1;
    if (cfg.selected >= n_sats) cfg.selected = 0;
    return n_sats;
}

uint8_t sat_count(void) { return db_ok ? n_sats : 0; }
const sat_db_sat_t *sat_get(uint8_t idx) { return (db_ok && idx < n_sats) ? &sats[idx] : 0; }
const sat_db_header_t *sat_db_header(void) { return db_ok ? &hdr : 0; }

const char *sat_mode_name(uint8_t mode)
{
    static const char * const names[SAT_MODE_COUNT] = { "FM", "DATA", "LIN-I", "LIN", "RX" };
    return (mode < SAT_MODE_COUNT) ? names[mode] : "?";
}

static int sat_init_sgp(sgp4_sat_t *s, const sat_db_sat_t *d)
{
    return sgp4_init(s, d->epoch_jd, d->bstar, d->incl_deg, d->raan_deg, d->ecc,
                     d->argp_deg, d->mean_anom_deg, d->mean_motion_rpd);
}

/* ===================================================================== */
/*  Clock                                                                 */
/* ===================================================================== */

void sat_time_set(uint32_t unix_s, sat_time_src_t src)
{
    uint32_t prev = sat_time_now(0);
    clk_base_unix = unix_s;
    clk_base_tick = get_tick();
    sat_time_src_t old = clk_src;
    clk_src = src;
    /* a jump of more than a minute invalidates cached predictions */
    if (old == SAT_TIME_NONE || (prev > unix_s ? prev - unix_s : unix_s - prev) > 60)
        invalidate_predictions();
}

uint32_t sat_time_now(uint16_t *ms_out)
{
    uint32_t dt = get_tick() - clk_base_tick;
    if (ms_out) *ms_out = (uint16_t)(dt % 1000U);
    return clk_base_unix + dt / 1000U;
}

uint8_t sat_time_valid(void) { return clk_src != SAT_TIME_NONE; }
sat_time_src_t sat_time_source(void) { return clk_src; }

static void clock_sync_gps(void)
{
    const gps_data_t *g = gps_get_data();
    if (!g || !g->rmc_valid || g->year < 2024 || g->rmc_seq == last_rmc_seq)
        return;
    last_rmc_seq = g->rmc_seq;
    uint32_t t = sat_ymdhms_to_unix(g->year, g->month, g->day,
                                    g->hour, g->minute, g->second);
    uint32_t now = sat_time_now(0);
    uint32_t diff = (now > t) ? now - t : t - now;
    if (clk_src != SAT_TIME_GPS || diff > 1)
        sat_time_set(t, SAT_TIME_GPS);
}

/* ===================================================================== */
/*  Observer position                                                     */
/* ===================================================================== */

static double approx_dist_km(double la1, double lo1, double la2, double lo2)
{
    double dlat = (la2 - la1) * SM_DEG2RAD;
    double dlon = (lo2 - lo1) * SM_DEG2RAD * sm_cos(0.5 * (la1 + la2) * SM_DEG2RAD);
    return 6371.0 * sm_sqrt(dlat * dlat + dlon * dlon);
}

static void update_observer(void)
{
    double lat = 0, lon = 0, alt = 0;
    sat_pos_src_t src = SAT_POS_NONE;
    const gps_data_t *g = gps_get_data();
    uint8_t gps_ok = g && g->fix_quality > 0 &&
                     (g->latitude != 0.0f || g->longitude != 0.0f);

    if (cfg.loc_src != SAT_LOC_MANUAL && gps_ok) {
        lat = g->latitude; lon = g->longitude; alt = g->altitude;
        src = SAT_POS_GPS;
    } else if (cfg.loc_src != SAT_LOC_GPS) {
        if (cfg.manual_valid) {
            lat = cfg.manual_lat_e6 / 1e6; lon = cfg.manual_lon_e6 / 1e6;
            alt = cfg.manual_alt_m;
            src = SAT_POS_MANUAL;
        } else if (db_ok && (hdr.flags & SAT_HDR_FLAG_HAS_QTH)) {
            lat = hdr.qth_lat_e6 / 1e6; lon = hdr.qth_lon_e6 / 1e6;
            alt = hdr.qth_alt_m;
            src = SAT_POS_DB;
        }
    }

    if (src == SAT_POS_NONE) { obs_src = SAT_POS_NONE; return; }

    /* only re-seat the observer (and redo predictions) on real movement */
    if (obs_lat > 900.0 || approx_dist_km(obs_lat, obs_lon, lat, lon) > 1.0 ||
        src != obs_src) {
        uint8_t big = (obs_lat > 900.0) ||
                      approx_dist_km(obs_lat, obs_lon, lat, lon) > 10.0;
        obs_lat = lat; obs_lon = lon; obs_alt = alt;
        sat_observer_set(&obs, lat, lon, alt);
        obs_src = src;
        if (big) invalidate_predictions();
    }
}

sat_pos_src_t sat_position(double *lat, double *lon, double *alt_m)
{
    if (obs_src != SAT_POS_NONE) {
        if (lat) *lat = obs_lat;
        if (lon) *lon = obs_lon;
        if (alt_m) *alt_m = obs_alt;
    }
    return obs_src;
}

void sat_locator(char out[7])
{
    if (obs_src == SAT_POS_NONE) { strcpy(out, "------"); return; }
    double lon = obs_lon + 180.0, lat = obs_lat + 90.0;
    if (lon < 0) lon = 0;
    if (lon >= 360.0) lon = 359.9999;
    if (lat < 0) lat = 0;
    if (lat >= 180.0) lat = 179.9999;
    int f_lon = (int)(lon / 20.0), f_lat = (int)(lat / 10.0);
    lon -= f_lon * 20.0; lat -= f_lat * 10.0;
    int s_lon = (int)(lon / 2.0), s_lat = (int)lat;
    lon -= s_lon * 2.0; lat -= s_lat;
    int ss_lon = (int)(lon * 12.0), ss_lat = (int)(lat * 24.0);
    out[0] = (char)('A' + f_lon); out[1] = (char)('A' + f_lat);
    out[2] = (char)('0' + s_lon); out[3] = (char)('0' + s_lat);
    out[4] = (char)('a' + ss_lon); out[5] = (char)('a' + ss_lat);
    out[6] = '\0';
}

/* ===================================================================== */
/*  Prediction                                                            */
/* ===================================================================== */

static uint8_t db_qth_matches(void)
{
    if (!db_ok || !(hdr.flags & SAT_HDR_FLAG_HAS_QTH) || obs_src == SAT_POS_NONE)
        return 0;
    if (hdr.min_el_deg != cfg.min_el_deg)
        return 0;
    return approx_dist_km(obs_lat, obs_lon, hdr.qth_lat_e6 / 1e6,
                          hdr.qth_lon_e6 / 1e6) <= QTH_MATCH_KM;
}

/* Look up the first PC-computed pass of sat idx with LOS after `after`. */
static uint8_t db_find_pass(uint8_t idx, uint32_t after, sat_pass_t *out)
{
    sat_db_pass_t rec[8];
    uint32_t base = SAT_DB_FLASH_ADDR + sizeof(sat_db_header_t) +
                    (uint32_t)n_sats * sizeof(sat_db_sat_t);
    if (after >= hdr.pass_end_unix) return 0;
    for (uint16_t i = 0; i < hdr.pass_count; i += 8) {
        uint16_t n = (uint16_t)((hdr.pass_count - i) > 8 ? 8 : (hdr.pass_count - i));
        spi_flash_read(base + (uint32_t)i * sizeof(sat_db_pass_t), (uint8_t *)rec,
                       (uint16_t)(n * sizeof(sat_db_pass_t)));
        for (uint16_t k = 0; k < n; k++) {
            if (rec[k].sat_index != idx) continue;
            uint32_t los = rec[k].aos_unix + rec[k].duration_s;
            if (los <= after) continue;
            out->aos = rec[k].aos_unix;
            out->tca = rec[k].aos_unix + rec[k].tca_offset_s;
            out->los = los;
            out->aos_az = rec[k].aos_az_deg;
            out->tca_az = rec[k].tca_az_deg;
            out->los_az = rec[k].los_az_deg;
            out->max_el = rec[k].max_el_deg;
            out->from_db = 1;
            return 1;   /* table is sorted by AOS */
        }
    }
    return 0;
}

static uint8_t sat_predictable(uint8_t i)
{
    return (sats[i].flags & SAT_FLAG_ENABLED) && !(sats[i].flags & SAT_FLAG_DEEP_SPACE);
}

static void prediction_poll(void)
{
    if (!db_ok || !sat_time_valid() || obs_src == SAT_POS_NONE || n_sats == 0)
        return;
    uint32_t now = sat_time_now(0);

    if (pred_idx == 0xFF) {
        /* find the next satellite needing a (new) prediction */
        for (uint8_t k = 0; k < n_sats; k++) {
            uint8_t i = (uint8_t)((pred_scan + k) % n_sats);
            if (!sat_predictable(i)) continue;
            if (pass_valid[i] && next_pass[i].los >= now) continue;
            if (pass_valid[i] && next_pass[i].los == 0 &&
                now < next_pass[i].aos)            /* "none" retry time */
                continue;
            pred_scan = (uint8_t)(i + 1);
            if (db_qth_matches() && db_find_pass(i, now, &next_pass[i])) {
                pass_valid[i] = 1;
                return;
            }
            if (sat_init_sgp(&pred_sgp, &sats[i]) != SGP4_OK) {
                next_pass[i].los = 0;
                next_pass[i].aos = now + 3600U;     /* retry in an hour */
                pass_valid[i] = 1;
                return;
            }
            sat_pred_start(&pred_job, &pred_sgp, &obs, cfg.min_el_deg, now, PRED_HORIZON_S);
            pred_idx = i;
            break;
        }
        if (pred_idx == 0xFF) return;
    }

    if (sat_pred_step(&pred_job, PRED_BUDGET)) {
        next_pass[pred_idx] = pred_job.result;
        if (pred_job.result.los == 0)
            next_pass[pred_idx].aos = now + 3600U;  /* nothing: retry later */
        pass_valid[pred_idx] = 1;
        pred_idx = 0xFF;
    }
}

const sat_pass_t *sat_next_pass(uint8_t idx)
{
    if (!db_ok || idx >= n_sats || !pass_valid[idx]) return 0;
    return &next_pass[idx];
}

uint8_t sat_pred_done(void)
{
    if (!db_ok) return 1;
    for (uint8_t i = 0; i < n_sats; i++)
        if (sat_predictable(i) && !pass_valid[i]) return 0;
    return 1;
}

/* ---- pass list ------------------------------------------------------ */

void sat_passlist_start(uint8_t idx)
{
    pl_count = 0;
    pl_busy = 0;
    if (!db_ok || idx >= n_sats || !sat_time_valid() || obs_src == SAT_POS_NONE)
        return;
    pl_idx = idx;
    if (sat_init_sgp(&pl_sgp, &sats[idx]) != SGP4_OK)
        return;
    sat_pred_start(&pl_job, &pl_sgp, &obs, cfg.min_el_deg, sat_time_now(0), 4U * 86400U);
    pl_busy = 1;
}

static void passlist_poll(void)
{
    if (!pl_busy) return;
    if (!sat_pred_step(&pl_job, PRED_BUDGET * 2)) return;
    if (pl_job.result.los == 0) { pl_busy = 0; return; }
    pl_res[pl_count++] = pl_job.result;
    if (pl_count >= SAT_PASSLIST_MAX) { pl_busy = 0; return; }
    sat_pred_start(&pl_job, &pl_sgp, &obs, cfg.min_el_deg,
                   pl_job.result.los + 60U, 4U * 86400U);
}

uint8_t sat_passlist_count(void) { return pl_count; }
uint8_t sat_passlist_busy(void)  { return pl_busy; }
uint8_t sat_passlist_sat(void)   { return pl_idx; }
const sat_pass_t *sat_passlist_get(uint8_t i) { return (i < pl_count) ? &pl_res[i] : 0; }

/* ===================================================================== */
/*  Tracking & radio control                                              */
/* ===================================================================== */

static uint8_t tone_index(uint16_t dhz)
{
    if (dhz == 0) return 0xFF;
    uint8_t best = 0xFF;
    uint16_t best_d = 0xFFFF;
    for (uint8_t i = 0; i < CTCSS_TONE_COUNT; i++) {
        uint16_t t = ctcss_tone_table[i];
        uint16_t d = (t > dhz) ? (uint16_t)(t - dhz) : (uint16_t)(dhz - t);
        if (d < best_d) { best_d = d; best = i; }
    }
    return (best_d <= 5) ? best : 0xFF;    /* within 0.5 Hz */
}

static uint32_t quant(int64_t hz, uint32_t q)
{
    if (hz <= 0) return 0;
    return (uint32_t)(((uint64_t)hz + q / 2U) / q * q);
}

static void compute_track_points(void)
{
    trk.track_n = 0;
    if (!trk.pass.los) return;
    trk.track_n = sat_pred_track(&trk_sgp, &obs, &trk.pass, SAT_TRACK_POINTS,
                                 trk.track_az, trk.track_el);
}

static void refresh_track_pass(uint32_t now)
{
    const sat_pass_t *p = sat_next_pass(trk.idx);
    if (p && p->los && p->los >= now) {
        if (p->aos != trk.pass.aos || trk.track_n == 0) {
            trk.pass = *p;
            compute_track_points();
        }
    } else if (trk.pass.los < now) {
        trk.pass.los = 0;
        trk.track_n = 0;
    }
}

static void track_update(void)
{
    const sat_db_sat_t *d = &sats[trk.idx];
    uint32_t now;
    uint16_t ms;

    trk.valid = 0;
    if (!sat_time_valid() || obs_src == SAT_POS_NONE) return;
    now = sat_time_now(&ms);
    if (sat_look(&trk_sgp, &obs, sat_unix_to_jd(now, ms), &trk.look) != SGP4_OK)
        return;
    trk.valid = 1;
    trk.in_view = (trk.look.el_deg >= (double)cfg.min_el_deg);
    refresh_track_pass(now);

    double beta = trk.look.range_rate_kms / SPEED_OF_LIGHT_KMS;  /* +: receding */
    int32_t rx_shift = 0, tx_shift = 0;
    if (cfg.doppler_on) {
        rx_shift = (int32_t)(-(double)d->downlink_hz * beta);
        tx_shift = (int32_t)( (double)d->uplink_hz * beta);
    }
    trk.rx_shift_hz = rx_shift;
    trk.tx_shift_hz = tx_shift;
    trk.rx_hz = d->downlink_hz ? quant((int64_t)d->downlink_hz + rx_shift + trk.trim_hz, RX_QUANT_HZ) : 0;
    trk.tx_hz = (d->uplink_hz && d->mode <= SAT_MODE_FM_DATA)
                ? quant((int64_t)d->uplink_hz + tx_shift, TX_QUANT_HZ) : 0;

    /* drive the VFOs (never retune while transmitting) */
    if (engaged && !tx_active && !radio_is_transmitting()) {
        if (trk.rx_hz && trk.rx_hz != applied_rx) {
            vfo_set_frequency(RADIO_VFO_A, trk.rx_hz);
            applied_rx = trk.rx_hz;
        }
        if (trk.tx_hz && trk.tx_hz != applied_tx) {
            vfo_set_frequency(RADIO_VFO_B, trk.tx_hz);
            applied_tx = trk.tx_hz;
        }
    }
}

static void program_vfos_for_sat(void)
{
    const sat_db_sat_t *d = &sats[trk.idx];

    /* VFO A: downlink receiver */
    vfo_clear_tone(RADIO_VFO_A);
    vfo_set_offset_dir(RADIO_VFO_A, 0);
    vfo_set_modulation(RADIO_VFO_A, RADIO_MOD_FM);
    vfo_set_bandwidth(RADIO_VFO_A, cfg.rx_wide);
    if (d->ctcss_down_dhz) {
        uint8_t ti = tone_index(d->ctcss_down_dhz);
        if (ti != 0xFF) vfo_set_ctcss_rx(RADIO_VFO_A, ti);
    }
    if (d->downlink_hz) vfo_set_frequency(RADIO_VFO_A, d->downlink_hz);

    /* VFO B: uplink transmitter */
    vfo_clear_tone(RADIO_VFO_B);
    vfo_set_offset_dir(RADIO_VFO_B, 0);
    vfo_set_modulation(RADIO_VFO_B, RADIO_MOD_FM);
    vfo_set_bandwidth(RADIO_VFO_B, 1);
    if (d->ctcss_up_dhz) {
        uint8_t ti = tone_index(d->ctcss_up_dhz);
        if (ti != 0xFF) vfo_set_ctcss_tx(RADIO_VFO_B, ti);
    }
    if (d->uplink_hz) vfo_set_frequency(RADIO_VFO_B, d->uplink_hz);

    vfo_apply(RADIO_VFO_A);
    vfo_apply(RADIO_VFO_B);
    applied_rx = applied_tx = 0;
    trk_last_tick = 0;                  /* force immediate Doppler update */
}

static void restore_vfo(radio_vfo_t v, const vfo_state_t *s)
{
    vfo_clear_tone(v);
    vfo_set_frequency(v, s->freq_hz);
    vfo_set_offset_dir(v, s->offset_dir);
    vfo_set_offset_freq(v, s->offset_freq_hz);
    vfo_set_modulation(v, s->modulation);
    vfo_set_bandwidth(v, s->bandwidth);
    if (s->dcs_code_idx != 0xFF) {
        vfo_set_dcs(v, s->dcs_code_idx, s->dcs_polarity);
    } else {
        if (s->ctcss_tx_idx != 0xFF) vfo_set_ctcss_tx(v, s->ctcss_tx_idx);
        if (s->ctcss_rx_idx != 0xFF) vfo_set_ctcss_rx(v, s->ctcss_rx_idx);
    }
    vfo_apply(v);
}

int sat_engage(uint8_t idx)
{
    if (!db_ok || idx >= n_sats) return -1;
    if (sat_init_sgp(&trk_sgp, &sats[idx]) != SGP4_OK) return -2;

    if (!engaged) {
        saved_a = *vfo_get_state(RADIO_VFO_A);
        saved_b = *vfo_get_state(RADIO_VFO_B);
        prev_active_vfo = (uint8_t)vfo_get_active();
        /* dual watch would retune/steal the receiver: pause it */
        dw_was_on = 1;
        sched_enable("dualwatch", 0);
    }
    memset(&trk, 0, sizeof trk);
    trk.idx = idx;
    engaged = 1;
    if (cfg.selected != idx) { cfg.selected = idx; cfg_save(); }
    vfo_set_active(RADIO_VFO_A);
    program_vfos_for_sat();
    track_update();
    return 0;
}

void sat_select(uint8_t idx)
{
    if (!engaged) { sat_engage(idx); return; }
    if (!db_ok || idx >= n_sats || idx == trk.idx) return;
    if (sat_init_sgp(&trk_sgp, &sats[idx]) != SGP4_OK) return;
    memset(&trk, 0, sizeof trk);
    trk.idx = idx;
    cfg.selected = idx;
    cfg_save();
    program_vfos_for_sat();
    track_update();
}

void sat_disengage(void)
{
    if (!engaged) return;
    engaged = 0;
    restore_vfo(RADIO_VFO_A, &saved_a);
    restore_vfo(RADIO_VFO_B, &saved_b);
    vfo_set_active((radio_vfo_t)prev_active_vfo);
    if (dw_was_on) sched_enable("dualwatch", 1);
}

uint8_t sat_is_engaged(void) { return engaged; }
const sat_track_t *sat_track(void) { return engaged ? &trk : 0; }

void sat_trim(int32_t delta_hz)
{
    trk.trim_hz += delta_hz;
    if (trk.trim_hz > 20000) trk.trim_hz = 20000;
    if (trk.trim_hz < -20000) trk.trim_hz = -20000;
    trk_last_tick = 0;
}

void sat_trim_reset(void) { trk.trim_hz = 0; trk_last_tick = 0; }

void sat_arm_toggle(void)
{
    if (!engaged || !sats[trk.idx].arm_tone_dhz) return;
    trk.arm_pending ^= 1;
}

void sat_doppler_toggle(void) { sat_cfg_set_doppler(!cfg.doppler_on); }

/* ---- PTT hooks ------------------------------------------------------ */

int sat_ptt_prepare(radio_vfo_t *tx_vfo, uint32_t *tx_hz)
{
    if (!engaged) return 0;
    const sat_db_sat_t *d = &sats[trk.idx];

    track_update();                       /* freshest uplink correction */
    if (!trk.tx_hz) return -1;            /* receive-only: never TX on downlink */

    radio_vfo_t v = (cfg.tx_mode == SAT_TX_VFO_B) ? RADIO_VFO_B : RADIO_VFO_A;
    uint8_t chip = vfo_get_state(v)->chip;
    uint16_t tone = (trk.arm_pending && d->arm_tone_dhz) ? d->arm_tone_dhz : d->ctcss_up_dhz;
    uint8_t ti = tone_index(tone);
    bk4829_disable_dcs(chip);
    if (ti != 0xFF) bk4829_set_ctcss_tx(chip, ti);
    else            bk4829_disable_ctcss(chip);

    *tx_vfo = v;
    *tx_hz = trk.tx_hz;
    tx_active = 1;
    return 1;
}

void sat_ptt_release(void)
{
    if (!tx_active) return;
    tx_active = 0;
    trk.arm_pending = 0;                  /* arm tone is one-shot */
    /* re-program both chips: frequency, tones, bandwidth */
    vfo_apply(RADIO_VFO_A);
    vfo_apply(RADIO_VFO_B);
    applied_rx = applied_tx = 0;
    trk_last_tick = 0;
}

/* ===================================================================== */
/*  Alerts & icon                                                         */
/* ===================================================================== */

static void alert_poll(void)
{
    if (!cfg.alert_min || !sat_time_valid()) return;
    uint32_t now = sat_time_now(0);
    uint32_t win = (uint32_t)cfg.alert_min * 60U;
    for (uint8_t i = 0; i < n_sats; i++) {
        if (!pass_valid[i] || !next_pass[i].los) continue;
        if (!(sats[i].flags & SAT_FLAG_FAVORITE) && !(engaged && i == trk.idx))
            continue;
        uint32_t aos = next_pass[i].aos;
        if (aos > now && aos - now <= win && alert_done_aos[i] != aos) {
            alert_done_aos[i] = aos;
            audio_beep_freq(15000, 120);      /* 1.5 kHz, 120 ms */
            alert_blink_until = now + win;
        }
    }
}

uint8_t sat_icon_state(void)
{
    if (!db_ok) return 0;
    uint32_t now = sat_time_now(0);
    if ((engaged && trk.valid && trk.in_view) || now < alert_blink_until)
        return 3;
    if (engaged) return 2;
    return 1;
}

/* ===================================================================== */
/*  Lifecycle                                                             */
/* ===================================================================== */

void sat_init(void)
{
    cfg_load();
    sat_db_reload();
    /* PC tool writes the build time into the DB: a coarse clock until GPS
     * or the PC sets the real time (marked NONE so predictions wait). */
    clk_src = SAT_TIME_NONE;
    clk_base_tick = get_tick();
    clk_base_unix = db_ok ? hdr.created_unix : 0;
}

void sat_poll(void)
{
    /* While the PC is rewriting the database, do not read it. */
    if (cps_is_active())
        return;

    clock_sync_gps();
    update_observer();

    if (engaged) {
        uint32_t t = get_tick();
        if (trk_last_tick == 0 || t - trk_last_tick >= TRACK_PERIOD_MS) {
            trk_last_tick = t ? t : 1;
            track_update();
        }
    }
    passlist_poll();
    prediction_poll();
    alert_poll();
}
