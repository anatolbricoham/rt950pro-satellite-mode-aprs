/*
 * am_radio.c - AM/SW/SSB broadcast receiver UI for the RT-950 Pro
 *
 * SI4732 AM receiver mode with multi-band support:
 *   MW:  520-1710 kHz, steps 1/9/10 kHz
 *   SW:  2300-26100 kHz, steps 1/5 kHz
 *   SSB: 150-30000 kHz, USB/LSB, BFO +/-16 kHz, step 1 kHz
 *
 * Key mapping:
 *   Encoder:    Tune frequency (or BFO in SSB mode when BFO edit active)
 *   MENU:       Cycle band (MW -> SW -> SSB)
 *   UP/DOWN:    Cycle step size
 *   KEY_1:      Toggle SSB sideband (LSB/USB)
 *   KEY_2:      Cycle bandwidth (6K/4K/3K/2K/1K)
 *   KEY_3:      Enter BFO adjust mode (SSB only)
 *   EXIT:       Leave AM radio mode
 */

#include "app/am_radio.h"
#include "drivers/si4732.h"
#include "drivers/lcd.h"
#include "app/display.h"
#include "app/font.h"
#include "app/keypad.h"

#include <string.h>

/* Layout ------------------------------------------------------------ */
#define AM_UI_HEADER_Y      4
#define AM_UI_FREQ_Y        40
#define AM_UI_KHZ_X         170
#define AM_UI_KHZ_Y         85
#define AM_UI_STATUS_Y      110
#define AM_UI_RSSI_Y        135
#define AM_UI_RSSI_BAR_X    60
#define AM_UI_RSSI_BAR_W    160
#define AM_UI_RSSI_BAR_H    8
#define AM_UI_HELP_Y        170

/* Band parameters --------------------------------------------------- */

typedef struct {
    uint16_t min_khz;
    uint16_t max_khz;
    const uint16_t *steps;
    uint8_t  step_count;
    const char *name;
} band_info_t;

static const uint16_t mw_steps[] = { 1, 9, 10 };
static const uint16_t sw_steps[] = { 1, 5 };
static const uint16_t ssb_steps[] = { 1 };

static const band_info_t bands[AM_BAND_COUNT] = {
    [AM_BAND_MW]  = { AM_MW_MIN,  AM_MW_MAX,  mw_steps,  3, "AM" },
    [AM_BAND_SW]  = { AM_SW_MIN,  AM_SW_MAX,  sw_steps,  2, "SW" },
    [AM_BAND_SSB] = { AM_SSB_MIN, AM_SSB_MAX, ssb_steps, 1, "SSB" },
};

/* AM bandwidth filter labels */
static const char *bw_labels[] = { "6K", "4K", "3K", "2K", "1K" };
static const uint16_t bw_values[] = {
    SI4732_AM_BW_6K, SI4732_AM_BW_4K, SI4732_AM_BW_3K,
    SI4732_AM_BW_2K, SI4732_AM_BW_1K
};

/* State ------------------------------------------------------------- */
static am_state_t     state;
static am_band_t      current_band;
static uint16_t       current_freq;     /* kHz */
static uint8_t        step_idx;         /* index into band's step table */
static uint8_t        bw_idx;           /* bandwidth filter index */
static ssb_sideband_t sideband;
static int16_t        bfo_offset;       /* Hz, +/-16383 */
static uint8_t        bfo_edit_mode;    /* 1 = encoder adjusts BFO */
static uint8_t        last_rssi;
static uint8_t        last_snr;
static uint8_t        ssb_patch_loaded;

/* Default frequencies per band -------------------------------------- */
static const uint16_t default_freqs[AM_BAND_COUNT] = {
    1000,   /* MW: 1000 kHz */
    7200,   /* SW: 7.2 MHz (40m broadcast) */
    14200,  /* SSB: 14.2 MHz (20m amateur) */
};

/* Helpers ----------------------------------------------------------- */

static void apply_tune(void)
{
    if (current_band == AM_BAND_SSB) {
        si4732_ssb_tune(current_freq);
    } else {
        si4732_am_tune(current_freq);
    }

    /* Read tune status for RSSI/SNR */
    struct si4732_tune_status st;
    if (si4732_am_tune_status(&st) == 0) {
        last_rssi = st.rssi;
        last_snr  = st.snr;
    }
}

static void enter_band(am_band_t band)
{
    const band_info_t *bi = &bands[band];
    current_band = band;
    step_idx = 0;

    /* Clamp frequency to band limits */
    if (current_freq < bi->min_khz || current_freq > bi->max_khz)
        current_freq = default_freqs[band];

    if (band == AM_BAND_SSB) {
        /* SSB requires patch + AM power-up mode */
        if (!ssb_patch_loaded) {
            si4732_power_down();
            si4732_power_up_am();
            si4732_ssb_patch_load();
            ssb_patch_loaded = 1;
        }
        si4732_ssb_set_mode((uint8_t)sideband);
        si4732_ssb_set_bfo(bfo_offset);
        si4732_ssb_set_bandwidth(bw_values[bw_idx]);
    } else {
        /* Regular AM - if coming from SSB, re-init AM mode */
        if (ssb_patch_loaded) {
            si4732_power_down();
            si4732_power_up_am();
            ssb_patch_loaded = 0;
        }
        si4732_am_set_bandwidth(bw_values[bw_idx]);
    }

    apply_tune();
}

/* ==========================================================================
 *  Public API
 * ========================================================================== */

void am_radio_init(void)
{
    state = AM_STATE_OFF;
    current_band = AM_BAND_MW;
    current_freq = default_freqs[AM_BAND_MW];
    step_idx = 0;
    bw_idx = 0;
    sideband = SSB_USB;
    bfo_offset = 0;
    bfo_edit_mode = 0;
    ssb_patch_loaded = 0;
}

void am_radio_enter(am_band_t band)
{
    si4732_power_up_am();
    state = AM_STATE_PLAYING;
    ssb_patch_loaded = 0;
    enter_band(band);
}

void am_radio_exit(void)
{
    si4732_power_down();
    state = AM_STATE_OFF;
    ssb_patch_loaded = 0;
    bfo_edit_mode = 0;
}

am_state_t am_radio_get_state(void)    { return state; }
am_band_t am_radio_get_band(void)      { return current_band; }
uint16_t am_radio_get_freq(void)       { return current_freq; }
int16_t am_radio_get_bfo(void)         { return bfo_offset; }
ssb_sideband_t am_radio_get_sideband(void) { return sideband; }
uint8_t am_radio_get_bandwidth(void)   { return bw_idx; }
uint16_t am_radio_get_step(void)       { return bands[current_band].steps[step_idx]; }
uint8_t am_radio_get_rssi(void)        { return last_rssi; }
uint8_t am_radio_get_snr(void)         { return last_snr; }

void am_radio_cycle_band(void)
{
    am_band_t next = (am_band_t)((current_band + 1) % AM_BAND_COUNT);
    enter_band(next);
}

void am_radio_tune_step(int8_t direction)
{
    const band_info_t *bi = &bands[current_band];
    int32_t f = (int32_t)current_freq + direction * (int32_t)bi->steps[step_idx];

    if (f < bi->min_khz) f = bi->max_khz;
    else if (f > bi->max_khz) f = bi->min_khz;

    current_freq = (uint16_t)f;
    apply_tune();
}

void am_radio_cycle_step(void)
{
    step_idx = (uint8_t)((step_idx + 1) % bands[current_band].step_count);
}

void am_radio_set_freq(uint16_t freq_khz)
{
    const band_info_t *bi = &bands[current_band];
    if (freq_khz < bi->min_khz) freq_khz = bi->min_khz;
    if (freq_khz > bi->max_khz) freq_khz = bi->max_khz;
    current_freq = freq_khz;
    apply_tune();
}

void am_radio_set_bfo(int16_t offset_hz)
{
    if (offset_hz < -16383) offset_hz = -16383;
    if (offset_hz > 16383) offset_hz = 16383;
    bfo_offset = offset_hz;
    if (current_band == AM_BAND_SSB)
        si4732_ssb_set_bfo(bfo_offset);
}

void am_radio_set_sideband(ssb_sideband_t sb)
{
    sideband = sb;
    if (current_band == AM_BAND_SSB)
        si4732_ssb_set_mode((uint8_t)sideband);
}

void am_radio_set_bandwidth(uint8_t idx)
{
    if (idx > 4) idx = 4;
    bw_idx = idx;
    if (current_band == AM_BAND_SSB)
        si4732_ssb_set_bandwidth(bw_values[bw_idx]);
    else
        si4732_am_set_bandwidth(bw_values[bw_idx]);
}

/* ==========================================================================
 *  Key / encoder handlers
 * ========================================================================== */

void am_radio_handle_encoder(int8_t direction)
{
    if (state != AM_STATE_PLAYING) return;

    if (bfo_edit_mode && current_band == AM_BAND_SSB) {
        /* Encoder adjusts BFO in 10 Hz steps */
        am_radio_set_bfo(bfo_offset + direction * 10);
    } else {
        am_radio_tune_step(direction);
    }
}

void am_radio_handle_key(uint8_t key)
{
    if (state != AM_STATE_PLAYING) return;

    switch (key) {
    case KEY_C_MENU:
        /* Cycle band: MW -> SW -> SSB -> MW */
        am_radio_cycle_band();
        bfo_edit_mode = 0;
        break;

    case KEY_A_VFO:
    case KEY_B_SCAN:
        am_radio_cycle_step();
        break;

    case KEY_1:
        /* Toggle LSB/USB (SSB only) */
        if (current_band == AM_BAND_SSB) {
            am_radio_set_sideband(sideband == SSB_USB ? SSB_LSB : SSB_USB);
        }
        break;

    case KEY_2:
        /* Cycle bandwidth filter */
        am_radio_set_bandwidth((uint8_t)((bw_idx + 1) % 5));
        break;

    case KEY_3:
        /* Toggle BFO edit mode (SSB only) */
        if (current_band == AM_BAND_SSB)
            bfo_edit_mode = !bfo_edit_mode;
        break;

    case KEY_D_BAND:
        /* Exit AM radio */
        am_radio_exit();
        break;

    default:
        break;
    }
}

/* ==========================================================================
 *  Display
 * ========================================================================== */

void am_radio_draw(void)
{
    if (state == AM_STATE_OFF) return;

    const band_info_t *bi = &bands[current_band];

    /* Clear screen */
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK);

    /* Header: band name + step */
    font_draw_string(FONT_MEDIUM, 4, AM_UI_HEADER_Y,
                     bi->name, COLOR_CYAN, COLOR_BLACK);

    /* Step size indicator */
    {
        char step_str[12] = "Step:";
        uint16_t s = bi->steps[step_idx];
        uint8_t p = 5;
        if (s >= 10) step_str[p++] = (char)('0' + s / 10);
        step_str[p++] = (char)('0' + s % 10);
        step_str[p++] = 'k';
        step_str[p] = '\0';
        font_draw_string(FONT_SMALL, 60, AM_UI_HEADER_Y + 2,
                         step_str, COLOR_GRAY, COLOR_BLACK);
    }

    /* Bandwidth indicator */
    {
        char bw_str[12] = "BW:";
        uint8_t p = 3;
        const char *bl = bw_labels[bw_idx];
        while (*bl) bw_str[p++] = *bl++;
        bw_str[p] = '\0';
        font_draw_string(FONT_SMALL, 140, AM_UI_HEADER_Y + 2,
                         bw_str, COLOR_GRAY, COLOR_BLACK);
    }

    /* Separator */
    lcd_fill_rect(0, 18, LCD_WIDTH, 1, COLOR_DARK_GRAY);

    /* Frequency display - large */
    {
        char freq_str[10];
        uint16_t f = current_freq;
        /* Format: XXXXX kHz or XX.XXX MHz */
        if (f >= 1000) {
            /* Show as XX.XXX MHz */
            uint8_t mhz = (uint8_t)(f / 1000);
            uint16_t khz_frac = f % 1000;
            uint8_t p = 0;
            if (mhz >= 10) freq_str[p++] = (char)('0' + mhz / 10);
            freq_str[p++] = (char)('0' + mhz % 10);
            freq_str[p++] = '.';
            freq_str[p++] = (char)('0' + khz_frac / 100);
            freq_str[p++] = (char)('0' + (khz_frac / 10) % 10);
            freq_str[p++] = (char)('0' + khz_frac % 10);
            freq_str[p] = '\0';

            font_draw_string(FONT_LARGE, 20, AM_UI_FREQ_Y,
                             freq_str, COLOR_WHITE, COLOR_BLACK);
            font_draw_string(FONT_MEDIUM, AM_UI_KHZ_X, AM_UI_KHZ_Y,
                             "MHz", COLOR_GRAY, COLOR_BLACK);
        } else {
            /* Show as XXXX kHz */
            uint8_t p = 0;
            if (f >= 1000) freq_str[p++] = (char)('0' + f / 1000);
            freq_str[p++] = (char)('0' + (f / 100) % 10);
            freq_str[p++] = (char)('0' + (f / 10) % 10);
            freq_str[p++] = (char)('0' + f % 10);
            freq_str[p] = '\0';

            font_draw_string(FONT_LARGE, 40, AM_UI_FREQ_Y,
                             freq_str, COLOR_WHITE, COLOR_BLACK);
            font_draw_string(FONT_MEDIUM, AM_UI_KHZ_X, AM_UI_KHZ_Y,
                             "kHz", COLOR_GRAY, COLOR_BLACK);
        }
    }

    /* SSB-specific: sideband + BFO indicator */
    if (current_band == AM_BAND_SSB) {
        const char *sb_str = (sideband == SSB_USB) ? "USB" : "LSB";
        font_draw_string(FONT_MEDIUM, 4, AM_UI_STATUS_Y,
                         sb_str, COLOR_YELLOW, COLOR_BLACK);

        /* BFO offset */
        char bfo_str[16] = "BFO:";
        uint8_t p = 4;
        int16_t bfo = bfo_offset;
        if (bfo < 0) { bfo_str[p++] = '-'; bfo = -bfo; }
        else bfo_str[p++] = '+';
        if (bfo >= 10000) bfo_str[p++] = (char)('0' + bfo / 10000);
        if (bfo >= 1000) bfo_str[p++] = (char)('0' + (bfo / 1000) % 10);
        bfo_str[p++] = (char)('0' + (bfo / 100) % 10);
        bfo_str[p++] = (char)('0' + (bfo / 10) % 10);
        bfo_str[p++] = (char)('0' + bfo % 10);
        bfo_str[p] = '\0';

        uint16_t bfo_col = bfo_edit_mode ? COLOR_GREEN : COLOR_GRAY;
        font_draw_string(FONT_SMALL, 60, AM_UI_STATUS_Y + 2,
                         bfo_str, bfo_col, COLOR_BLACK);
    }

    /* RSSI bar */
    font_draw_string(FONT_SMALL, 4, AM_UI_RSSI_Y + 2,
                     "RSSI", COLOR_GRAY, COLOR_BLACK);
    lcd_fill_rect(AM_UI_RSSI_BAR_X, AM_UI_RSSI_Y,
                  AM_UI_RSSI_BAR_W, AM_UI_RSSI_BAR_H, COLOR_DARK_GRAY);
    {
        uint16_t bar_w = last_rssi;
        if (bar_w > AM_UI_RSSI_BAR_W) bar_w = AM_UI_RSSI_BAR_W;
        uint16_t bar_col = (last_rssi > 40) ? COLOR_GREEN :
                           (last_rssi > 20) ? COLOR_YELLOW : COLOR_RED;
        if (bar_w > 0)
            lcd_fill_rect(AM_UI_RSSI_BAR_X, AM_UI_RSSI_Y,
                          bar_w, AM_UI_RSSI_BAR_H, bar_col);
    }

    /* SNR */
    {
        char snr_str[10] = "SNR:";
        uint8_t p = 4;
        if (last_snr >= 10) snr_str[p++] = (char)('0' + last_snr / 10);
        snr_str[p++] = (char)('0' + last_snr % 10);
        snr_str[p] = '\0';
        font_draw_string(FONT_SMALL, 4, AM_UI_RSSI_Y + 14,
                         snr_str, COLOR_GRAY, COLOR_BLACK);
    }

    /* Help line */
    font_draw_string(FONT_SMALL, 4, AM_UI_HELP_Y,
                     "MENU:Band UP/DN:Step EXIT:Back",
                     COLOR_DARK_GRAY, COLOR_BLACK);
    if (current_band == AM_BAND_SSB) {
        font_draw_string(FONT_SMALL, 4, AM_UI_HELP_Y + 12,
                         "1:LSB/USB 2:BW 3:BFO",
                         COLOR_DARK_GRAY, COLOR_BLACK);
    }
}
