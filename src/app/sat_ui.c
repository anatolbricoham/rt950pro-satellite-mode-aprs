/*
 * sat_ui.c - Satellite screens for the RT-950 Pro (240 x 320 portrait)
 *
 *   LIST    all satellites sorted by next AOS (in-view first)
 *   TRACK   polar plot + AZ/EL/range + Doppler-corrected RX (VFO A) and
 *           TX (VFO B) frequencies, OpenGD77 / AnyTone style
 *   PASSES  next passes of the selected satellite
 *
 * Keys (all screens):  encoder = scroll / RX trim,  MENU = select,
 *                      # = back,  PTT works normally on the TRACK screen.
 * TRACK:  A = previous sat, B = next sat, * = arm tone (SO-50),
 *         0 = Doppler on/off, 1 = pass list, 5 = reset trim,
 *         MENU = satellite settings.
 *
 * Drawing: direct LCD writes (no frame buffer).  Static parts are drawn
 * once per screen change; dynamic parts are refreshed once per second.
 */

#include "app/satellite.h"
#include "app/sat_math.h"
#include "app/display.h"
#include "app/keypad.h"
#include "app/menu.h"
#include "app/radio.h"
#include "drivers/lcd.h"

#include <string.h>

extern uint32_t get_tick(void);

typedef enum { SCR_LIST = 0, SCR_TRACK, SCR_PASSES } sat_screen_t;

static uint8_t      ui_active;
static sat_screen_t screen;
static uint8_t      dirty;               /* full redraw requested */
static uint32_t     last_draw_tick;
static uint32_t     last_dyn_sec = 0xFFFFFFFFUL;
static uint8_t      sel;                 /* list cursor (index into order) */
static uint8_t      list_top;
static uint8_t      order[SAT_DB_MAX_SATS];
static uint8_t      pass_scroll;
static uint8_t      last_tx_state;

/* Colours (RGB565) */
#define C_BG      COLOR_BLACK
#define C_TXT     COLOR_WHITE
#define C_DIM     COLOR_GRAY
#define C_DARK    COLOR_DARK_GRAY
#define C_HDR     COLOR_YELLOW
#define C_OK      COLOR_GREEN
#define C_INFO    COLOR_CYAN
#define C_TX      COLOR_RED
#define C_WARN    COLOR_ORANGE
#define C_GRID    0x2945               /* dark blue-grey */

/* Polar plot geometry */
#define PP_CX     70
#define PP_CY     114
#define PP_R      58

/* ===================================================================== */
/*  Small formatting helpers (no printf float support in this firmware)   */
/* ===================================================================== */

static char *put_uint(char *p, uint32_t v, uint8_t min_digits)
{
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10U); v /= 10U; } while (v && n < 10);
    while (n < min_digits && n < 10) tmp[n++] = '0';
    while (n) *p++ = tmp[--n];
    *p = '\0';
    return p;
}

static char *put_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = '\0';
    return p;
}

/* fixed point: value / 10^dec, e.g. put_fix(p, 1234, 1) -> "123.4" */
static char *put_fix(char *p, int32_t v, uint8_t dec, uint8_t show_plus)
{
    uint32_t div = 1;
    for (uint8_t i = 0; i < dec; i++) div *= 10U;
    if (v < 0) { *p++ = '-'; v = -v; }
    else if (show_plus) *p++ = '+';
    p = put_uint(p, (uint32_t)v / div, 1);
    if (dec) { *p++ = '.'; p = put_uint(p, (uint32_t)v % div, dec); }
    return p;
}

static char *put_hhmmss(char *p, uint32_t t, uint8_t secs)
{
    uint32_t sod = t % 86400U;
    p = put_uint(p, sod / 3600U, 2); *p++ = ':';
    p = put_uint(p, (sod / 60U) % 60U, 2);
    if (secs) { *p++ = ':'; p = put_uint(p, sod % 60U, 2); }
    return p;
}

/* compact relative time: "45s", "19m", "1h07m" */
static char *put_in(char *p, uint32_t s)
{
    if (s < 60U) { p = put_uint(p, s, 1); *p++ = 's'; }
    else if (s < 3600U) { p = put_uint(p, s / 60U, 1); *p++ = 'm'; }
    else {
        p = put_uint(p, s / 3600U, 1); *p++ = 'h';
        p = put_uint(p, (s / 60U) % 60U, 2); *p++ = 'm';
    }
    *p = '\0';
    return p;
}

/* pass length: "10m34" */
static char *put_len(char *p, uint32_t s)
{
    p = put_uint(p, s / 60U, 1); *p++ = 'm';
    return put_uint(p, s % 60U, 2);
}

static char *put_duration(char *p, uint32_t s)
{
    if (s >= 3600U) {
        p = put_uint(p, s / 3600U, 1); *p++ = 'h';
        p = put_uint(p, (s / 60U) % 60U, 2); *p++ = 'm';
    } else {
        p = put_uint(p, s / 60U, 2); *p++ = ':';
        p = put_uint(p, s % 60U, 2);
    }
    *p = '\0';
    return p;
}

/* "436.7985" (MHz with 4 decimals = 100 Hz resolution) */
static char *put_freq(char *p, uint32_t hz)
{
    p = put_uint(p, hz / 1000000U, 3); *p++ = '.';
    return put_uint(p, (hz % 1000000U) / 100U, 4);
}

static char *put_khz_shift(char *p, int32_t hz)
{
    /* +3.5k with 0.1 kHz resolution */
    int32_t v = (hz >= 0) ? (hz + 50) / 100 : (hz - 50) / 100;
    p = put_fix(p, v, 1, 1);
    *p++ = 'k'; *p = '\0';
    return p;
}

static char *put_tone(char *p, uint16_t dhz)
{
    if (!dhz) return put_str(p, "off");
    return put_fix(p, dhz, 1, 0);
}

static void text_s(uint16_t x, uint16_t y, const char *s, uint16_t fg)
{
    display_draw_text(x, y, s, fg, C_BG);
}

static void text_m(uint16_t x, uint16_t y, const char *s, uint16_t fg)
{
    lcd_draw_string(x, y, s, fg, C_BG);
}

static void text_l(uint16_t x, uint16_t y, const char *s, uint16_t fg)
{
    lcd_draw_string_2x(x, y, s, fg, C_BG);
}

static void pad_to(char *buf, uint8_t width)
{
    uint8_t n = (uint8_t)strlen(buf);
    while (n < width) buf[n++] = ' ';
    buf[n] = '\0';
}

static void name_of(uint8_t idx, char out[SAT_DB_NAME_LEN + 1])
{
    const sat_db_sat_t *d = sat_get(idx);
    uint8_t n = 0;
    if (d) {
        for (; n < SAT_DB_NAME_LEN && d->name[n] && d->name[n] != ' '; n++)
            out[n] = d->name[n];
        /* keep inner spaces (e.g. "ISS VOICE") */
        for (uint8_t k = n; k < SAT_DB_NAME_LEN && d->name[k]; k++) {
            out[k] = d->name[k];
            if (d->name[k] != ' ') n = (uint8_t)(k + 1);
        }
    }
    out[n] = '\0';
}

/* ===================================================================== */
/*  Status bar icon (16 x 12): two solar panels + body + antenna          */
/* ===================================================================== */

void sat_draw_icon(uint16_t x, uint16_t y)
{
    uint8_t st = sat_icon_state();
    lcd_fill_rect(x, y, 16, 12, C_BG);
    if (st == 0) return;

    uint16_t c = C_DIM;
    if (st == 2) c = C_INFO;
    if (st == 3) c = ((get_tick() / 500U) & 1U) ? C_OK : C_BG;

    /* panels */
    lcd_fill_rect(x + 0,  y + 3, 5, 6, c);
    lcd_fill_rect(x + 11, y + 3, 5, 6, c);
    /* struts */
    lcd_fill_rect(x + 5,  y + 5, 1, 2, c);
    lcd_fill_rect(x + 10, y + 5, 1, 2, c);
    /* body */
    lcd_fill_rect(x + 6,  y + 4, 4, 4, c);
    /* antenna */
    lcd_fill_rect(x + 7,  y + 8, 2, 2, c);
    lcd_fill_rect(x + 8,  y + 10, 1, 2, c);
    if (st >= 2) {
        /* "signal" dots */
        lcd_fill_rect(x + 7, y + 1, 2, 1, c);
        lcd_fill_rect(x + 6, y + 0, 4, 1, c);
    }
}

/* ===================================================================== */
/*  Sorting for the list screen                                           */
/* ===================================================================== */

static uint32_t sort_key(uint8_t idx, uint32_t now)
{
    const sat_db_sat_t *d = sat_get(idx);
    if (!d || !(d->flags & SAT_FLAG_ENABLED)) return 0xFFFFFFFFUL;
    const sat_pass_t *p = sat_next_pass(idx);
    if (!p) return 0xFFFFFFF0UL;              /* not computed yet */
    if (!p->los) return 0xFFFFFFF8UL;         /* no pass in window */
    if (p->aos <= now) return 0;              /* in view: on top */
    return p->aos;
}

static void build_order(void)
{
    uint8_t n = sat_count();
    uint32_t now = sat_time_now(0);
    uint32_t keys[SAT_DB_MAX_SATS];
    for (uint8_t i = 0; i < n; i++) { order[i] = i; keys[i] = sort_key(i, now); }
    for (uint8_t i = 1; i < n; i++) {       /* insertion sort, stable */
        uint8_t o = order[i]; uint32_t k = keys[i]; int8_t j = (int8_t)(i - 1);
        while (j >= 0 && keys[j] > k) { order[j + 1] = order[j]; keys[j + 1] = keys[j]; j--; }
        order[j + 1] = o; keys[j + 1] = k;
    }
}

/* ===================================================================== */
/*  Common header / footer                                                */
/* ===================================================================== */

static void draw_title(const char *title)
{
    char buf[16];
    lcd_fill_rect(0, LAYOUT_STATUS_H, LCD_WIDTH, 14, C_BG);
    text_m(4, 23, title, C_HDR);
    char *p = buf;
    if (sat_time_valid()) { p = put_hhmmss(p, sat_time_now(0), 1); *p++ = 'Z'; *p = 0; }
    else put_str(buf, "--:--:--Z");
    text_s(LCD_WIDTH - 6 * 9 - 2, 24, buf, sat_time_valid() ? C_TXT : C_WARN);
    display_draw_hline(0, 35, LCD_WIDTH, C_DARK);
}

static void draw_qth_line(uint16_t y)
{
    char buf[44], loc[7];
    static const char * const psrc[] = { "none", "GPS", "DB", "MAN" };
    static const char * const tsrc[] = { "none", "MAN", "PC", "GPS" };
    sat_locator(loc);
    char *p = put_str(buf, "QTH ");
    p = put_str(p, loc);
    p = put_str(p, " (");
    p = put_str(p, psrc[sat_position(0, 0, 0)]);
    p = put_str(p, ")  UTC:");
    p = put_str(p, tsrc[sat_time_source()]);
    pad_to(buf, 39);
    text_s(4, y, buf, (sat_position(0, 0, 0) && sat_time_valid()) ? C_DIM : C_WARN);
}

static void draw_keys(uint16_t y, const char *l1, const char *l2)
{
    lcd_fill_rect(0, y - 4, LCD_WIDTH, LCD_HEIGHT - (y - 4), C_BG);
    display_draw_hline(0, y - 4, LCD_WIDTH, C_DARK);
    text_s(4, y, l1, C_DARK);
    if (l2) text_s(4, y + 10, l2, C_DARK);
}

/* ===================================================================== */
/*  LIST screen                                                           */
/* ===================================================================== */

#define LIST_Y0     40
#define LIST_ROW_H  18
#define LIST_ROWS   12

static void list_row(uint8_t row, uint8_t idx, uint8_t selected, uint32_t now)
{
    uint16_t y = (uint16_t)(LIST_Y0 + row * LIST_ROW_H);
    char name[SAT_DB_NAME_LEN + 1], buf[24];
    const sat_db_sat_t *d = sat_get(idx);
    const sat_pass_t *p = sat_next_pass(idx);

    lcd_fill_rect(0, y, LCD_WIDTH, LIST_ROW_H, selected ? C_GRID : C_BG);
    uint16_t bg = selected ? C_GRID : C_BG;
    name_of(idx, name);
    uint16_t nc = (d->flags & SAT_FLAG_ENABLED) ? (selected ? C_HDR : C_TXT) : C_DARK;
    lcd_draw_string(4, (uint16_t)(y + 5), name, nc, bg);

    uint16_t sc = C_DIM;
    char *q = buf;
    if (!(d->flags & SAT_FLAG_ENABLED)) {
        q = put_str(q, "disabled");
    } else if (d->flags & SAT_FLAG_DEEP_SPACE) {
        q = put_str(q, "deep-space n/a");
    } else if (!sat_time_valid() || !sat_position(0, 0, 0)) {
        q = put_str(q, "need time/QTH");
        sc = C_WARN;
    } else if (!p) {
        q = put_str(q, "calculating..");
    } else if (!p->los) {
        q = put_str(q, "no pass 36h");
        sc = C_DARK;
    } else if (p->aos <= now) {
        q = put_str(q, "LIVE LOS ");
        q = put_duration(q, p->los > now ? p->los - now : 0);
        sc = C_OK;
    } else {
        uint32_t dt = p->aos - now;
        q = put_hhmmss(q, p->aos, 0);
        q = put_str(q, " in ");
        q = put_in(q, dt);
        sc = (dt < 600U) ? C_INFO : C_DIM;
    }
    display_draw_text(104, (uint16_t)(y + 2), buf, sc, bg);

    /* second small line: max elevation + mode */
    q = buf;
    if (p && p->los) {
        q = put_str(q, "max ");
        q = put_uint(q, p->max_el, 1);
        q = put_str(q, "deg ");
        q = put_uint(q, p->aos_az, 3);
        *q++ = '>';
        q = put_uint(q, p->los_az, 3);
    }
    *q = '\0';
    display_draw_text(104, (uint16_t)(y + 10), buf, C_DARK, bg);
    display_draw_text(LCD_WIDTH - 6 * 5 - 2, (uint16_t)(y + 10),
                      sat_mode_name(d->mode), C_DARK, bg);
}

static void draw_list(uint8_t full)
{
    uint8_t n = sat_count();
    uint32_t now = sat_time_now(0);
    if (full) {
        lcd_fill_rect(0, LAYOUT_STATUS_H, LCD_WIDTH, LCD_HEIGHT - LAYOUT_STATUS_H, C_BG);
        draw_keys(300, "Enc:select MENU:track 1:passes", "D:settings #:exit sat mode");
    }
    draw_title("SATELLITES");
    if (n == 0) {
        text_m(8, 70, "No satellite data", C_WARN);
        text_s(8, 90, "Load it from the PC with:", C_DIM);
        text_s(8, 102, "rt950_sat.py upload COMx", C_INFO);
        text_s(8, 120, "(TLE + frequencies + passes)", C_DIM);
        draw_qth_line(284);
        return;
    }
    build_order();
    if (sel >= n) sel = (uint8_t)(n - 1);
    if (sel < list_top) list_top = sel;
    if (sel >= list_top + LIST_ROWS) list_top = (uint8_t)(sel - LIST_ROWS + 1);
    for (uint8_t r = 0; r < LIST_ROWS; r++) {
        uint8_t k = (uint8_t)(list_top + r);
        if (k < n) list_row(r, order[k], k == sel, now);
        else lcd_fill_rect(0, (uint16_t)(LIST_Y0 + r * LIST_ROW_H), LCD_WIDTH, LIST_ROW_H, C_BG);
    }
    char buf[32];
    char *p = put_uint(buf, (uint32_t)sel + 1U, 1); *p++ = '/';
    p = put_uint(p, n, 1);
    p = put_str(p, sat_pred_done() ? "        " : "  predicting...");
    pad_to(buf, 26);
    text_s(4, 260, buf, C_DARK);
    draw_qth_line(284);
}

/* ===================================================================== */
/*  TRACK screen                                                          */
/* ===================================================================== */

static void pp_xy(double az_deg, double el_deg, int16_t *x, int16_t *y)
{
    double s, c;
    if (el_deg < 0.0) el_deg = 0.0;
    double r = PP_R * (90.0 - el_deg) / 90.0;
    sm_sincos(az_deg * SM_DEG2RAD, &s, &c);
    *x = (int16_t)(PP_CX + r * s + 0.5);
    *y = (int16_t)(PP_CY - r * c + 0.5);
}

static void plot_px(int16_t x, int16_t y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    lcd_fill_rect((uint16_t)x, (uint16_t)y, 1, 1, c);
}

static void circle(int16_t cx, int16_t cy, int16_t r, uint16_t c)
{
    int16_t x = r, y = 0, err = 1 - r;
    while (x >= y) {
        plot_px(cx + x, cy + y, c); plot_px(cx - x, cy + y, c);
        plot_px(cx + x, cy - y, c); plot_px(cx - x, cy - y, c);
        plot_px(cx + y, cy + x, c); plot_px(cx - y, cy + x, c);
        plot_px(cx + y, cy - x, c); plot_px(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

static void draw_polar(const sat_track_t *t)
{
    lcd_fill_rect(PP_CX - PP_R - 8, PP_CY - PP_R - 10, 2 * PP_R + 16, 2 * PP_R + 19, C_BG);
    circle(PP_CX, PP_CY, PP_R, C_DIM);
    circle(PP_CX, PP_CY, PP_R * 2 / 3, C_GRID);
    circle(PP_CX, PP_CY, PP_R / 3, C_GRID);
    lcd_fill_rect(PP_CX - PP_R, PP_CY, 2 * PP_R + 1, 1, C_GRID);
    lcd_fill_rect(PP_CX, PP_CY - PP_R, 1, 2 * PP_R + 1, C_GRID);
    text_s(PP_CX - 2, PP_CY - PP_R - 9, "N", C_TXT);
    text_s(PP_CX + PP_R + 3, PP_CY - 3, "E", C_DIM);
    text_s(PP_CX - 2, PP_CY + PP_R + 3, "S", C_DIM);
    text_s(PP_CX - PP_R - 8, PP_CY - 3, "W", C_DIM);

    if (!t) return;
    /* pass track */
    int16_t px = 0, py = 0;
    for (uint8_t i = 0; i < t->track_n; i++) {
        int16_t x, y;
        pp_xy(t->track_az[i] * 2.0, t->track_el[i], &x, &y);
        if (i > 0) {
            /* simple line: interpolate 4 points */
            for (int k = 1; k <= 4; k++)
                plot_px((int16_t)(px + (x - px) * k / 4), (int16_t)(py + (y - py) * k / 4), C_INFO);
        } else {
            lcd_fill_rect((uint16_t)(x - 2), (uint16_t)(y - 2), 5, 5, C_OK);     /* AOS */
        }
        px = x; py = y;
    }
    if (t->track_n > 1)
        lcd_fill_rect((uint16_t)(px - 2), (uint16_t)(py - 2), 5, 5, C_TX);      /* LOS */

    /* current position (only above the horizon) */
    if (t->valid && t->look.el_deg >= 0.0) {
        int16_t x, y;
        pp_xy(t->look.az_deg, t->look.el_deg, &x, &y);
        lcd_fill_rect((uint16_t)(x - 3), (uint16_t)(y - 3), 7, 7, C_HDR);
        lcd_fill_rect((uint16_t)(x - 1), (uint16_t)(y - 1), 3, 3, C_BG);
    }
}

static void kv_line(uint16_t y, const char *k, const char *v, uint16_t vc)
{
    char buf[14];
    strcpy(buf, k);
    pad_to(buf, 4);
    lcd_draw_string(142, y, buf, C_DIM, C_BG);
    strcpy(buf, v);
    pad_to(buf, 8);
    lcd_draw_string(174, y, buf, vc, C_BG);
}

static void draw_track(uint8_t full)
{
    const sat_track_t *t = sat_track();
    char buf[44], name[SAT_DB_NAME_LEN + 1];
    if (!t) { screen = SCR_LIST; dirty = 1; return; }
    const sat_db_sat_t *d = sat_get(t->idx);
    uint32_t now = sat_time_now(0);

    if (full) {
        lcd_fill_rect(0, LAYOUT_STATUS_H, LCD_WIDTH, LCD_HEIGHT - LAYOUT_STATUS_H, C_BG);
        draw_keys(296, "A/B:sat *:arm 0:doppler 1:passes", "Enc:RX trim 5:trim=0 D:cfg #:list");
    }
    name_of(t->idx, name);
    draw_title(name);
    /* mode tag next to the name */
    char *p = buf;
    *p++ = '['; p = put_str(p, sat_mode_name(d->mode)); *p++ = ']'; *p = 0;
    text_s((uint16_t)(8 + 8 * strlen(name)), 24, buf, C_DIM);

    /* status line */
    p = buf;
    uint16_t sc = C_DIM;
    if (!sat_time_valid()) { p = put_str(p, "NO UTC TIME - GPS fix or PC sync"); sc = C_WARN; }
    else if (!sat_position(0, 0, 0)) { p = put_str(p, "NO LOCATION - GPS or DB QTH"); sc = C_WARN; }
    else if (t->valid && t->in_view) {
        p = put_str(p, "IN VIEW   LOS in ");
        p = put_duration(p, t->pass.los > now ? t->pass.los - now : 0);
        sc = C_OK;
    } else if (t->pass.los) {
        p = put_str(p, "AOS in ");
        p = put_duration(p, t->pass.aos > now ? t->pass.aos - now : 0);
        p = put_str(p, "  max ");
        p = put_uint(p, t->pass.max_el, 1);
        p = put_str(p, "deg");
        sc = C_INFO;
    } else {
        p = put_str(p, sat_next_pass(t->idx) ? "no pass in 36 h" : "predicting next pass...");
    }
    *p = 0;
    pad_to(buf, 38);
    text_s(4, 38, buf, sc);

    /* polar plot (once per second) */
    draw_polar(t);

    /* right column */
    if (t->valid) {
        p = put_fix(buf, (int32_t)(t->look.az_deg * 10.0 + 0.5), 1, 0); *p = 0;
        kv_line(52, "AZ", buf, C_TXT);
        p = put_fix(buf, (int32_t)(t->look.el_deg * 10.0 + (t->look.el_deg >= 0 ? 0.5 : -0.5)), 1, 0); *p = 0;
        kv_line(64, "EL", buf, t->in_view ? C_OK : C_TXT);
        p = put_uint(buf, (uint32_t)(t->look.range_km + 0.5), 1); p = put_str(p, "km");
        kv_line(76, "RNG", buf, C_TXT);
        p = put_fix(buf, (int32_t)(t->look.range_rate_kms * 100.0 + (t->look.range_rate_kms >= 0 ? 0.5 : -0.5)), 2, 1); *p = 0;
        kv_line(88, "RR", buf, C_TXT);
        p = put_uint(buf, (uint32_t)(t->look.alt_km + 0.5), 1); p = put_str(p, "km");
        kv_line(100, "ALT", buf, C_DIM);
    } else {
        kv_line(52, "AZ", "---", C_DARK);
        kv_line(64, "EL", "---", C_DARK);
        kv_line(76, "RNG", "---", C_DARK);
        kv_line(88, "RR", "---", C_DARK);
        kv_line(100, "ALT", "---", C_DARK);
    }
    if (t->pass.los) {
        put_hhmmss(buf, t->pass.aos, 0); kv_line(118, "AOS", buf, C_OK);
        put_hhmmss(buf, t->pass.tca, 0); kv_line(130, "TCA", buf, C_TXT);
        put_hhmmss(buf, t->pass.los, 0); kv_line(142, "LOS", buf, C_TX);
        p = put_uint(buf, t->pass.max_el, 1); p = put_str(p, "deg");
        kv_line(154, "MAX", buf, C_HDR);
        p = put_uint(buf, t->pass.aos_az, 3); *p++ = '>'; p = put_uint(p, t->pass.los_az, 3);
        kv_line(166, "AZ", buf, C_DIM);
    } else {
        for (uint16_t y = 118; y <= 166; y += 12) lcd_fill_rect(142, y, 98, 8, C_BG);
    }

    /* frequencies */
    uint8_t txing = radio_is_transmitting();
    display_draw_hline(0, 180, LCD_WIDTH, C_DARK);
    text_s(4, 186, "RX", C_OK);
    text_s(4, 195, "VFO A", C_DARK);
    if (t->rx_hz) { put_freq(buf, t->rx_hz); text_l(40, 184, buf, txing ? C_DIM : C_TXT); }
    else text_l(40, 184, "---.----", C_DARK);
    p = put_str(buf, "dop ");
    p = put_khz_shift(p, t->rx_shift_hz);
    p = put_str(p, "  trim ");
    p = put_khz_shift(p, t->trim_hz);
    p = put_str(p, "  sq ");
    p = put_tone(p, d->ctcss_down_dhz);
    pad_to(buf, 38);
    text_s(4, 203, buf, C_DIM);

    text_s(4, 218, "TX", C_TX);
    text_s(4, 227, sat_cfg()->tx_mode ? "VFO B" : "VFO A", C_DARK);
    if (t->tx_hz) { put_freq(buf, t->tx_hz); text_l(40, 216, buf, txing ? C_TX : C_TXT); }
    else text_l(40, 216, "RX ONLY ", C_WARN);
    p = put_str(buf, "dop ");
    p = put_khz_shift(p, t->tx_shift_hz);
    p = put_str(p, "  CTCSS ");
    if (t->arm_pending && d->arm_tone_dhz) {
        p = put_tone(p, d->arm_tone_dhz);
        p = put_str(p, " ARM!");
    } else {
        p = put_tone(p, d->ctcss_up_dhz);
    }
    pad_to(buf, 38);
    text_s(4, 235, buf, t->arm_pending ? C_WARN : C_DIM);

    /* info lines */
    p = put_str(buf, "Doppler ");
    p = put_str(p, sat_cfg()->doppler_on ? "ON " : "OFF");
    p = put_str(p, "  RX BW ");
    p = put_str(p, sat_cfg()->rx_wide ? "25k" : "12.5k");
    if (d->arm_tone_dhz) { p = put_str(p, "  *=arm "); p = put_tone(p, d->arm_tone_dhz); }
    pad_to(buf, 38);
    text_s(4, 249, buf, sat_cfg()->doppler_on ? C_INFO : C_WARN);

    memcpy(buf, d->info, SAT_DB_INFO_LEN);
    buf[SAT_DB_INFO_LEN] = 0;
    for (uint8_t i = 0; i < SAT_DB_INFO_LEN && buf[i]; i++)
        if ((uint8_t)buf[i] < 0x20 || (uint8_t)buf[i] > 0x7E) buf[i] = ' ';
    pad_to(buf, 38);
    text_s(4, 260, buf, C_DIM);
    draw_qth_line(274);
}

/* ===================================================================== */
/*  PASSES screen                                                         */
/* ===================================================================== */

static void draw_passes(uint8_t full)
{
    char buf[44], name[SAT_DB_NAME_LEN + 1];
    uint8_t idx = sat_passlist_sat();      /* the satellite the job runs for */
    if (full) {
        lcd_fill_rect(0, LAYOUT_STATUS_H, LCD_WIDTH, LCD_HEIGHT - LAYOUT_STATUS_H, C_BG);
        draw_keys(300, "Enc:scroll MENU:track #:back", 0);
    }
    name_of(idx, name);
    char *p = put_str(buf, "PASSES ");
    put_str(p, name);
    draw_title(buf);
    text_s(4, 40, "DATE  AOS   LOS   LEN   MAX", C_DIM);
    display_draw_hline(0, 50, LCD_WIDTH, C_DARK);

    uint8_t n = sat_passlist_count();
    for (uint8_t r = 0; r < SAT_PASSLIST_MAX; r++) {
        uint16_t y = (uint16_t)(56 + r * 26);
        const sat_pass_t *ps = sat_passlist_get(r);
        if (!ps) { lcd_fill_rect(0, y, LCD_WIDTH, 24, C_BG); continue; }
        int yy, mo, dd, hh, mi, ss;
        sat_unix_to_ymdhms(ps->aos, &yy, &mo, &dd, &hh, &mi, &ss);
        p = put_uint(buf, (uint32_t)mo, 2); *p++ = '-';
        p = put_uint(p, (uint32_t)dd, 2); *p++ = ' ';
        p = put_hhmmss(p, ps->aos, 0); *p++ = ' ';
        p = put_hhmmss(p, ps->los, 0); *p++ = ' ';
        p = put_len(p, ps->los - ps->aos); *p++ = ' ';
        if (ps->los - ps->aos < 600U) *p++ = ' ';
        p = put_uint(p, ps->max_el, 2);
        *p = 0;
        uint16_t c = (ps->max_el >= 30) ? C_OK : (ps->max_el >= 10 ? C_TXT : C_DIM);
        text_s(4, y, buf, c);
        p = put_str(buf, "      az ");
        p = put_uint(p, ps->aos_az, 3); p = put_str(p, " > ");
        p = put_uint(p, ps->tca_az, 3); p = put_str(p, " > ");
        p = put_uint(p, ps->los_az, 3);
        pad_to(buf, 38);
        text_s(4, (uint16_t)(y + 10), buf, C_DARK);
    }
    p = buf;
    if (sat_passlist_busy()) { p = put_str(p, "calculating "); p = put_uint(p, n, 1); p = put_str(p, "/8 ..."); }
    else if (n == 0) p = put_str(p, (sat_time_valid() && sat_position(0, 0, 0)) ? "no passes in 4 days" : "need UTC time and QTH");
    *p = 0;
    pad_to(buf, 38);
    text_s(4, 270, buf, C_INFO);
    draw_qth_line(284);
    (void)pass_scroll;
}

/* ===================================================================== */
/*  Public UI API                                                         */
/* ===================================================================== */

void sat_ui_enter(void)
{
    ui_active = 1;
    screen = SCR_LIST;
    dirty = 1;
    /* put the cursor on the last used satellite */
    build_order();
    for (uint8_t k = 0; k < sat_count(); k++)
        if (order[k] == sat_cfg()->selected) { sel = k; break; }
}

void sat_ui_exit(void)
{
    sat_disengage();
    ui_active = 0;
    display_set_mode(DISPLAY_MODE_MAIN);
}

uint8_t sat_ui_is_active(void) { return ui_active; }

static void go(sat_screen_t s) { screen = s; dirty = 1; }

void sat_ui_handle_encoder(int8_t dir)
{
    uint8_t n = sat_count();
    switch (screen) {
    case SCR_LIST:
        if (!n) break;
        if (dir > 0 && sel + 1 < n) sel++;
        else if (dir < 0 && sel > 0) sel--;
        last_dyn_sec = 0xFFFFFFFFUL;            /* redraw now */
        break;
    case SCR_TRACK:
        sat_trim(dir > 0 ? 100 : -100);
        last_dyn_sec = 0xFFFFFFFFUL;
        break;
    case SCR_PASSES:
        if (dir > 0 && pass_scroll < SAT_PASSLIST_MAX) pass_scroll++;
        else if (dir < 0 && pass_scroll > 0) pass_scroll--;
        break;
    }
}

static void step_sat(int8_t d)
{
    uint8_t n = sat_count();
    if (!n || !sat_is_engaged()) return;
    uint8_t i = sat_track()->idx;
    for (uint8_t k = 0; k < n; k++) {
        i = (uint8_t)((i + n + d) % n);
        if (sat_get(i)->flags & SAT_FLAG_ENABLED) break;
    }
    sat_select(i);
    dirty = 1;
}

void sat_ui_handle_key(uint8_t key)
{
    switch (screen) {
    case SCR_LIST:
        if (key == KEY_HASH) { sat_ui_exit(); return; }
        if (key == KEY_C_MENU && sat_count()) {
            if (sat_engage(order[sel]) == 0) go(SCR_TRACK);
        } else if (key == KEY_1 && sat_count()) {
            sat_passlist_start(order[sel]);
            pass_scroll = 0;
            go(SCR_PASSES);
        } else if (key == KEY_D_BAND) {
            menu_open_category(MENU_CAT_SATELLITE);
        } else if (key == KEY_B_SCAN || key == KEY_STAR) {
            sat_ui_handle_encoder(+1);
        } else if (key == KEY_A_VFO || key == KEY_0) {
            sat_ui_handle_encoder(-1);
        }
        break;

    case SCR_TRACK:
        switch (key) {
        case KEY_HASH:   sat_disengage(); go(SCR_LIST); break;
        case KEY_A_VFO:  step_sat(-1); break;
        case KEY_B_SCAN: step_sat(+1); break;
        case KEY_STAR:   sat_arm_toggle(); last_dyn_sec = 0xFFFFFFFFUL; break;
        case KEY_0:      sat_doppler_toggle(); last_dyn_sec = 0xFFFFFFFFUL; break;
        case KEY_5:      sat_trim_reset(); last_dyn_sec = 0xFFFFFFFFUL; break;
        case KEY_1:
            sat_passlist_start(sat_track()->idx);
            pass_scroll = 0;
            go(SCR_PASSES);
            break;
        case KEY_C_MENU:
        case KEY_D_BAND:
            menu_open_category(MENU_CAT_SATELLITE);
            break;
        default: break;
        }
        break;

    case SCR_PASSES:
        if (key == KEY_HASH) go(sat_is_engaged() ? SCR_TRACK : SCR_LIST);
        else if (key == KEY_C_MENU) {
            if (sat_engage(sat_passlist_sat()) == 0) go(SCR_TRACK);
        }
        break;
    }
}

void sat_ui_draw(void)
{
    if (!ui_active) return;
    uint32_t tick = get_tick();
    /* coming back from the menu overlay (or first frame): full redraw */
    if (tick - last_draw_tick > 200U) dirty = 1;
    last_draw_tick = tick;

    uint8_t tx = radio_is_transmitting();
    uint32_t sec = sat_time_now(0);
    uint8_t full = dirty;
    if (!full && sec == last_dyn_sec && tx == last_tx_state)
        return;                                  /* nothing changed */
    dirty = 0;
    last_dyn_sec = sec;
    last_tx_state = tx;

    display_draw_status_bar();             /* clock, battery, SAT icon */
    switch (screen) {
    case SCR_LIST:   draw_list(full);   break;
    case SCR_TRACK:  draw_track(full);  break;
    case SCR_PASSES: draw_passes(full); break;
    }
}
