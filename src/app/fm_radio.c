/*
 * fm_radio.c - FM broadcast receiver UI for the RT-950 Pro
 *
 * User interface layer for the SI4732 FM receiver: frequency display,
 * manual tuning, software seek, preset management, and signal indicators.
 *
 * The SI4732 low-level I2C driver (si4732.c) handles chip communication.
 * This module manages state and draws the on-screen UI.
 */

#include "app/fm_radio.h"
#include "drivers/si4732.h"
#include "drivers/lcd.h"
#include "app/display.h"
#include "app/font.h"
#include "app/keypad.h"

/* RGB565 colours ------------------------------------------------------- */

/* Colours not in display.h */
#define COLOR_DKGRAY  COLOR_DARK_GRAY

/* Tuning parameters ---------------------------------------------------- */

#define FM_SEEK_RSSI_THRESHOLD  20      /* minimum RSSI (dBuV) for seek */
#define FM_DEFAULT_FREQ         10110   /* 101.10 MHz */

/* Screen layout (240x320 LCD) ------------------------------------------ */

#define FM_UI_HEADER_Y      4
#define FM_UI_SEP1_Y       18
#define FM_UI_FREQ_Y       40
#define FM_UI_MHZ_X       170
#define FM_UI_MHZ_Y        85
#define FM_UI_STATUS_Y    110
#define FM_UI_STEREO_X     60
#define FM_UI_RSSI_LBL_X  108
#define FM_UI_RSSI_BAR_X  140
#define FM_UI_RSSI_BAR_W   92
#define FM_UI_RSSI_BAR_H    8
#define FM_UI_SEP2_Y      128
#define FM_UI_PRESET_HDR_Y 136
#define FM_UI_PRESET_Y    150

/* Module state --------------------------------------------------------- */

static fm_state_t  fm_state;
static uint16_t    current_freq;
static uint16_t    presets[FM_PRESET_COUNT];
static uint8_t     cached_rssi;
static uint8_t     cached_stereo;
static uint8_t     active_preset;       /* last accessed preset slot */

/* Internal helpers ----------------------------------------------------- */

/*
 * Read SI4732 tune status and update cached RSSI / stereo flag.
 *
 * True stereo-pilot detection requires the FM_RSQ_STATUS (0x23) command
 * which is not yet exposed by the SI4732 driver.  As a stand-in we use
 * a signal-strength heuristic: strong + valid ~ stereo.
 */
static void fm_update_status(void)
{
    struct si4732_tune_status st;

    if (si4732_fm_tune_status(&st) == 0) {
        cached_rssi   = st.rssi;
        cached_stereo = (st.valid && st.rssi >= 30) ? 1 : 0;
    }
}

/*
 * Format a frequency (10 kHz units) as "NNN.NN" into buf.
 * Buffer must be at least 8 bytes.
 */
static void format_freq(uint16_t freq_10khz, char *buf)
{
    uint16_t whole = freq_10khz / 100;
    uint16_t frac  = freq_10khz % 100;
    int idx = 0;

    if (whole >= 100)
        buf[idx++] = (char)('0' + whole / 100);
    buf[idx++] = (char)('0' + (whole / 10) % 10);
    buf[idx++] = (char)('0' + whole % 10);
    buf[idx++] = '.';
    buf[idx++] = (char)('0' + frac / 10);
    buf[idx++] = (char)('0' + frac % 10);
    buf[idx]   = '\0';
}

/* Map a KEY_* code to digit 0-9.  Returns -1 for non-digit keys. */
static int key_to_digit(uint8_t key)
{
    switch (key) {
    case KEY_0: return 0;
    case KEY_1: return 1;
    case KEY_2: return 2;
    case KEY_3: return 3;
    case KEY_4: return 4;
    case KEY_5: return 5;
    case KEY_6: return 6;
    case KEY_7: return 7;
    case KEY_8: return 8;
    case KEY_9: return 9;
    default:    return -1;
    }
}

/* ===========================================================================
 *  Public API - Lifecycle
 * ===========================================================================*/

void fm_radio_init(void)
{
    fm_state      = FM_STATE_OFF;
    current_freq  = FM_DEFAULT_FREQ;
    cached_rssi   = 0;
    cached_stereo = 0;
    active_preset = 0;

    for (uint8_t i = 0; i < FM_PRESET_COUNT; i++)
        presets[i] = 0;

    /* Restore presets + last freq from flash */
    fm_radio_restore();
}

void fm_radio_enter(void)
{
    si4732_power_up_fm();
    si4732_fm_tune(current_freq);
    fm_state = FM_STATE_PLAYING;
    fm_update_status();
}

void fm_radio_exit(void)
{
    if (fm_state == FM_STATE_OFF)
        return;
    fm_radio_persist();  /* save current freq + presets */
    si4732_power_down();
    fm_state = FM_STATE_OFF;
}

fm_state_t fm_radio_get_state(void)
{
    return fm_state;
}

/* ===========================================================================
 *  Public API - Tuning
 * ===========================================================================*/

void fm_radio_tune_step(int8_t direction)
{
    if (fm_state != FM_STATE_PLAYING || direction == 0)
        return;

    int32_t freq = (int32_t)current_freq +
                   (int32_t)direction * (int32_t)FM_FREQ_STEP;

    if (freq > (int32_t)FM_FREQ_MAX)
        freq = FM_FREQ_MIN;
    else if (freq < (int32_t)FM_FREQ_MIN)
        freq = FM_FREQ_MAX;

    current_freq = (uint16_t)freq;
    si4732_fm_tune(current_freq);
    fm_update_status();
}

/*
 * Software seek - step through the band one channel at a time and stop
 * at the first frequency whose RSSI exceeds the seek threshold.
 *
 * The SI4732 has a hardware seek command (FM_SEEK_START, 0x21) but the
 * low-level driver does not yet expose a wrapper for it, so we perform
 * the scan in software using si4732_fm_tune() + si4732_fm_tune_status().
 */
void fm_radio_seek(int8_t direction)
{
    if (fm_state != FM_STATE_PLAYING || direction == 0)
        return;

    fm_state = FM_STATE_SEEKING;

    uint16_t start = current_freq;
    uint16_t freq  = current_freq;
    int steps_max  = (FM_FREQ_MAX - FM_FREQ_MIN) / FM_FREQ_STEP;

    for (int n = 0; n < steps_max; n++) {
        if (direction > 0) {
            freq += FM_FREQ_STEP;
            if (freq > FM_FREQ_MAX)
                freq = FM_FREQ_MIN;
        } else {
            if (freq <= FM_FREQ_MIN)
                freq = FM_FREQ_MAX;
            else
                freq -= FM_FREQ_STEP;
        }

        si4732_fm_tune(freq);

        struct si4732_tune_status st;
        if (si4732_fm_tune_status(&st) == 0 &&
            st.valid && st.rssi >= FM_SEEK_RSSI_THRESHOLD) {
            current_freq  = freq;
            cached_rssi   = st.rssi;
            cached_stereo = (st.rssi >= 30) ? 1 : 0;
            fm_state = FM_STATE_PLAYING;
            return;
        }
    }

    /* No station found - return to the original frequency */
    si4732_fm_tune(start);
    current_freq = start;
    fm_state = FM_STATE_PLAYING;
    fm_update_status();
}

uint16_t fm_radio_get_freq(void)
{
    return current_freq;
}

void fm_radio_set_freq(uint16_t freq_10khz)
{
    if (freq_10khz < FM_FREQ_MIN)
        freq_10khz = FM_FREQ_MIN;
    if (freq_10khz > FM_FREQ_MAX)
        freq_10khz = FM_FREQ_MAX;

    current_freq = freq_10khz;

    if (fm_state == FM_STATE_PLAYING) {
        si4732_fm_tune(current_freq);
        fm_update_status();
    }
}

/* ===========================================================================
 *  Public API - Presets (RAM-only; flash persistence comes later)
 * ===========================================================================*/

void fm_radio_save_preset(uint8_t index)
{
    if (index < FM_PRESET_COUNT) {
        presets[index] = current_freq;
        fm_radio_persist();
    }
}

void fm_radio_load_preset(uint8_t index)
{
    if (index >= FM_PRESET_COUNT || presets[index] == 0)
        return;

    active_preset = index;
    fm_radio_set_freq(presets[index]);
}

uint16_t fm_radio_get_preset(uint8_t index)
{
    if (index < FM_PRESET_COUNT)
        return presets[index];
    return 0;
}

/* ===========================================================================
 *  Public API - Signal info
 * ===========================================================================*/

uint8_t fm_radio_get_rssi(void)
{
    return cached_rssi;
}

uint8_t fm_radio_is_stereo(void)
{
    return cached_stereo;
}

/* ===========================================================================
 *  Public API - Input handling
 * ===========================================================================*/

void fm_radio_handle_encoder(int8_t direction)
{
    fm_radio_tune_step(direction);
}

void fm_radio_handle_key(uint8_t key)
{
    if (fm_state == FM_STATE_OFF)
        return;

    /* Number keys 0-9: recall preset (if stored) */
    int digit = key_to_digit(key);
    if (digit >= 0) {
        fm_radio_load_preset((uint8_t)digit);
        return;
    }

    switch (key) {
    case KEY_SIDE1:                     /* seek up */
        fm_radio_seek(1);
        break;
    case KEY_SIDE2:                     /* seek down */
        fm_radio_seek(-1);
        break;
    case KEY_STAR:                      /* save current freq to active slot */
        fm_radio_save_preset(active_preset);
        break;
    case KEY_HASH:                      /* cycle active preset slot 0-9 */
        active_preset = (uint8_t)((active_preset + 1) % 10);
        break;
    case KEY_C_MENU:                    /* exit FM mode */
        fm_radio_exit();
        break;
    default:
        break;
    }
}

/* ===========================================================================
 *  Public API - Display
 *
 *  Layout on 240x320 ST7789V LCD:
 *  +------------------------------+
 *  |        FM RADIO              |  header   (FONT_LARGE)
 *  +------------------------------+
 *  |                              |
 *  |     101.10            MHz    |  frequency (FONT_DISPLAY + FONT_MEDIUM)
 *  |                              |
 *  |  >> STEREO   RSSI ########  |  status   (5x7 built-in font)
 *  +------------------------------+
 *  |  Presets:                    |
 *  |  1:98.10  2:101.10  3:...     |  preset bar
 *  +------------------------------+
 * ===========================================================================*/

/* Draw a single preset entry.  Returns the X advance in pixels. */
static uint16_t draw_preset_entry(uint16_t px, uint16_t py,
                                  uint8_t slot, uint16_t freq)
{
    char pbuf[12];
    int pi = 0;

    pbuf[pi++] = (char)('0' + slot);
    pbuf[pi++] = ':';
    format_freq(freq, &pbuf[pi]);
    while (pbuf[pi] != '\0')
        pi++;
    pbuf[pi++] = ' ';
    pbuf[pi]   = '\0';

    uint16_t fg = (slot == active_preset) ? COLOR_YELLOW : COLOR_WHITE;
    display_draw_text(px, py, pbuf, fg, COLOR_BLACK);

    return (uint16_t)((uint16_t)pi * (DISPLAY_FONT_W + DISPLAY_CHAR_GAP));
}

void fm_radio_draw(void)
{
    /* Clear screen */
    display_clear(COLOR_BLACK);

    /* Header --------------------------------------------------------- */
    font_draw_string(FONT_LARGE, 70, FM_UI_HEADER_Y,
                     "FM RADIO", COLOR_CYAN, COLOR_BLACK);
    display_draw_hline(0, FM_UI_SEP1_Y, LCD_WIDTH, COLOR_GRAY);

    /* - "Seeking" splash (early return while scanning) ----------------- */
    if (fm_state == FM_STATE_SEEKING) {
        font_draw_string(FONT_LARGE, 60, 60,
                         "SEEKING...", COLOR_YELLOW, COLOR_BLACK);
        return;
    }

    /* Frequency (large display font) --------------------------------- */
    {
        char fbuf[8];
        format_freq(current_freq, fbuf);
        font_draw_string(FONT_DISPLAY, 20, FM_UI_FREQ_Y,
                         fbuf, COLOR_WHITE, COLOR_BLACK);
    }

    /* "MHz" label */
    font_draw_string(FONT_MEDIUM, FM_UI_MHZ_X, FM_UI_MHZ_Y,
                     "MHz", COLOR_GRAY, COLOR_BLACK);

    /* Status line (built-in 5x7 font) -------------------------------- */
    if (cached_stereo)
        display_draw_text(FM_UI_STEREO_X, FM_UI_STATUS_Y,
                          "STEREO", COLOR_CYAN, COLOR_BLACK);
    else
        display_draw_text(FM_UI_STEREO_X, FM_UI_STATUS_Y,
                          "MONO  ", COLOR_GRAY, COLOR_BLACK);

    display_draw_text(FM_UI_RSSI_LBL_X, FM_UI_STATUS_Y,
                      "RSSI", COLOR_GRAY, COLOR_BLACK);

    /* RSSI bar: background then proportional fill */
    lcd_fill_rect(FM_UI_RSSI_BAR_X, FM_UI_STATUS_Y,
                  FM_UI_RSSI_BAR_W, FM_UI_RSSI_BAR_H, COLOR_DKGRAY);

    {
        uint16_t clamped = (cached_rssi > 60) ? 60 : cached_rssi;
        uint16_t bar_w   = (uint16_t)(clamped * FM_UI_RSSI_BAR_W / 60);

        if (bar_w > 0) {
            uint16_t bar_color;
            if (clamped < 15)
                bar_color = COLOR_ORANGE;
            else if (clamped < 30)
                bar_color = COLOR_YELLOW;
            else
                bar_color = COLOR_GREEN;

            lcd_fill_rect(FM_UI_RSSI_BAR_X, FM_UI_STATUS_Y,
                          bar_w, FM_UI_RSSI_BAR_H, bar_color);
        }
    }

    /* Separator ------------------------------------------------------ */
    display_draw_hline(0, FM_UI_SEP2_Y, LCD_WIDTH, COLOR_GRAY);

    /* Preset bar ----------------------------------------------------- */
    display_draw_text(4, FM_UI_PRESET_HDR_Y,
                      "Presets:", COLOR_GRAY, COLOR_BLACK);

    {
        uint16_t px = 4;
        uint16_t py = FM_UI_PRESET_Y;

        for (uint8_t i = 0; i < 10; i++) {
            if (presets[i] == 0)
                continue;
            if (py + DISPLAY_FONT_H > LCD_HEIGHT)
                break;

            uint16_t adv = draw_preset_entry(px, py, i, presets[i]);
            px += adv;

            /* Wrap to next line when the next entry won't fit */
            if (px + 54 > LCD_WIDTH) {
                px = 4;
                py += DISPLAY_FONT_H + 3;
            }
        }
    }
}

/* ===========================================================================
 *  FM preset persistence - WL_EXTCFG sector
 *
 *  Record layout (fits in 160-byte EXTCFG record):
 *    [0]      magic   0xFB ("FM Block")
 *    [1]      active_preset
 *    [2..3]   current_freq (LE)
 *    [4..35]  presets[0..15] (16 x 2 bytes LE)
 *    [36..159] reserved / zero
 * ===========================================================================*/

#include "drivers/flash_wearleveling.h"
#include <string.h>

#define FM_PERSIST_MAGIC  0xFB

void fm_radio_persist(void)
{
    uint8_t buf[160];
    memset(buf, 0, sizeof(buf));

    buf[0] = FM_PERSIST_MAGIC;
    buf[1] = active_preset;
    buf[2] = (uint8_t)(current_freq & 0xFF);
    buf[3] = (uint8_t)(current_freq >> 8);

    for (uint8_t i = 0; i < FM_PRESET_COUNT; i++) {
        buf[4 + i * 2]     = (uint8_t)(presets[i] & 0xFF);
        buf[4 + i * 2 + 1] = (uint8_t)(presets[i] >> 8);
    }

    wl_write(&WL_EXTCFG, buf);
}

void fm_radio_restore(void)
{
    uint8_t buf[160];

    if (wl_read(&WL_EXTCFG, buf) != 0)
        return;
    if (buf[0] != FM_PERSIST_MAGIC)
        return;

    active_preset = buf[1];
    current_freq  = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);

    if (current_freq < FM_FREQ_MIN || current_freq > FM_FREQ_MAX)
        current_freq = FM_DEFAULT_FREQ;

    for (uint8_t i = 0; i < FM_PRESET_COUNT; i++)
        presets[i] = (uint16_t)buf[4 + i * 2] | ((uint16_t)buf[4 + i * 2 + 1] << 8);
}
