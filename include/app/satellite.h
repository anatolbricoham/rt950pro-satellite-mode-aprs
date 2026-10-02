/*
 * satellite.h - Amateur satellite tracking & Doppler-corrected operation
 *
 * Feature set modelled on OpenGD77's satellite screen and AnyTone's
 * satellite mode:
 *
 *   - Up to 48 satellites (TLE + transponder data) uploaded from the PC
 *     with tools/rt950_sat.py into SPI flash (see sat_db.h).
 *   - On-radio SGP4 propagation: azimuth, elevation, range, range-rate.
 *   - Next-pass prediction for every satellite (background, incremental),
 *     or the pass table pre-computed by the PC tool when the QTH matches.
 *   - Split operation: VFO A receives the downlink, VFO B shows/transmits
 *     the uplink.  Both are Doppler corrected once per second.
 *     TX can run on the same chip (default, like OpenGD77) or on the VFO B
 *     chip (dual-VFO, AnyTone-like).
 *   - Arming tone (SO-50 74.4 Hz) for the next transmission.
 *   - UTC clock from GPS ($xxRMC), from the PC upload tool, or manual.
 *   - Observer from GPS fix, or the QTH stored in the database (locator).
 *   - AOS alert beep and status bar satellite icon.
 */

#ifndef APP_SATELLITE_H
#define APP_SATELLITE_H

#include <stdint.h>
#include "app/sat_db.h"
#include "app/sat_sgp4.h"
#include "app/sat_pred.h"
#include "app/radio.h"

#define SAT_TRACK_POINTS   40          /* polar plot samples per pass */
#define SAT_PASSLIST_MAX   8           /* passes shown on the pass screen */

typedef enum {
    SAT_TIME_NONE = 0,
    SAT_TIME_MANUAL,
    SAT_TIME_PC,                       /* set over the CPS cable */
    SAT_TIME_GPS,
} sat_time_src_t;

typedef enum {
    SAT_POS_NONE = 0,
    SAT_POS_GPS,
    SAT_POS_DB,                        /* QTH stored by the PC tool */
    SAT_POS_MANUAL,
} sat_pos_src_t;

typedef sat_pred_pass_t sat_pass_t;     /* aos/tca/los unix UTC, los==0 => none */

typedef struct {
    uint8_t    idx;                    /* satellite index in DB            */
    uint8_t    valid;                  /* look angles are valid            */
    uint8_t    in_view;                /* el >= mask                       */
    uint8_t    arm_pending;            /* next TX uses arm tone            */
    sat_look_t look;
    uint32_t   rx_hz;                  /* corrected downlink (incl. trim)  */
    uint32_t   tx_hz;                  /* corrected uplink (0 = no TX)     */
    int32_t    rx_shift_hz;            /* Doppler on downlink              */
    int32_t    tx_shift_hz;            /* Doppler pre-correction on uplink */
    int32_t    trim_hz;                /* user fine tune on RX             */
    sat_pass_t pass;                   /* current (in view) or next pass   */
    uint8_t    track_n;                /* polar plot points                */
    uint8_t    track_az[SAT_TRACK_POINTS];  /* az/2 (0..179)              */
    uint8_t    track_el[SAT_TRACK_POINTS];  /* el (0..90)                 */
} sat_track_t;

/* ---- lifecycle ------------------------------------------------------ */
void sat_init(void);                   /* load config + DB from SPI flash  */
void sat_poll(void);                   /* call every 100 ms (scheduler)    */
int  sat_db_reload(void);              /* returns sat count or -1          */

/* ---- database ------------------------------------------------------- */
uint8_t              sat_count(void);
const sat_db_sat_t  *sat_get(uint8_t idx);
const sat_db_header_t *sat_db_header(void);
const char          *sat_mode_name(uint8_t mode);

/* ---- time & position ------------------------------------------------ */
uint32_t       sat_time_now(uint16_t *ms_out);
uint8_t        sat_time_valid(void);
sat_time_src_t sat_time_source(void);
void           sat_time_set(uint32_t unix_s, sat_time_src_t src);
sat_pos_src_t  sat_position(double *lat, double *lon, double *alt_m);
void           sat_locator(char out[7]);   /* current observer as locator */

/* ---- prediction ----------------------------------------------------- */
const sat_pass_t *sat_next_pass(uint8_t idx);  /* NULL if not computed   */
uint8_t           sat_pred_done(void);          /* 1 when all computed    */
/* Pass list job for the pass screen (incremental) */
void              sat_passlist_start(uint8_t idx);
uint8_t           sat_passlist_count(void);     /* results ready so far   */
uint8_t           sat_passlist_busy(void);
const sat_pass_t *sat_passlist_get(uint8_t i);
uint8_t           sat_passlist_sat(void);       /* satellite of the job   */

/* ---- tracking / radio control -------------------------------------- */
int   sat_engage(uint8_t idx);         /* take over VFO A/B (0 = ok)       */
void  sat_disengage(void);             /* restore VFOs                     */
uint8_t sat_is_engaged(void);
void  sat_select(uint8_t idx);         /* change satellite while engaged   */
const sat_track_t *sat_track(void);
void  sat_trim(int32_t delta_hz);      /* RX fine tune                     */
void  sat_trim_reset(void);
void  sat_arm_toggle(void);            /* arm tone for next TX             */
void  sat_doppler_toggle(void);

/*
 * Radio hooks (called from radio.c).
 * sat_ptt_prepare(): returns 0 if satellite mode is not engaged (normal TX),
 *   1 if TX must use *tx_vfo / *tx_hz (sub-tone already programmed),
 *  -1 if TX must be refused (receive-only satellite, no uplink).
 */
int   sat_ptt_prepare(radio_vfo_t *tx_vfo, uint32_t *tx_hz);
void  sat_ptt_release(void);

/* ---- UI helpers ----------------------------------------------------- */
/* Status icon: 0 = hidden, 1 = DB loaded, 2 = engaged, 3 = sat in view /
 * AOS alert (blinks) */
uint8_t sat_icon_state(void);

/* ---- configuration (menu) ------------------------------------------ */
const sat_cfg_t *sat_cfg(void);
void  sat_cfg_set_min_el(uint8_t v);
void  sat_cfg_set_doppler(uint8_t v);
void  sat_cfg_set_tx_mode(uint8_t v);
void  sat_cfg_set_loc_src(uint8_t v);
void  sat_cfg_set_alert(uint8_t v);
void  sat_cfg_set_rx_wide(uint8_t v);

/* ---- satellite screen (sat_ui.c) ----------------------------------- */
void    sat_ui_enter(void);
void    sat_ui_exit(void);
uint8_t sat_ui_is_active(void);
void    sat_ui_handle_key(uint8_t key);
void    sat_ui_handle_encoder(int8_t dir);
void    sat_ui_draw(void);             /* from display_update()            */
void    sat_draw_icon(uint16_t x, uint16_t y);  /* status bar icon 16x12   */

/* CRC-32 (zlib/ISO-HDLC), shared with the CPS upload path */
uint32_t sat_crc32(uint32_t crc, const uint8_t *p, uint32_t len);

#endif /* APP_SATELLITE_H */
