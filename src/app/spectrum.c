/*
 * spectrum.c - Spectrum analyzer mode for the RT-950 Pro
 *
 * Sweeps a BK4829 receiver across a frequency range, reads RSSI at each
 * step, and renders a real-time bar graph on the 240x320 LCD.
 *
 * Controls:
 *   Encoder:     Shift center frequency (CW = up, CCW = down)
 *   Keys 1/4:    Decrease/increase span
 *   Keys 2/5:    Decrease/increase step size
 *   Key 3:       Toggle mode (live / peak-hold / frozen)
 *   Key 0:       Reset peak hold data
 *   Key HASH:    Move marker left/right (with encoder)
 *   Key MENU:    Exit spectrum mode
 *
 * Binary references (V0.27):
 *   BK4829 freq set:   fw 0x0801BD40  (REG_38/39 + PLL settle)
 *   BK4829 RSSI read:  fw 0x0801BEC4  (REG_0C)
 *   BK4829 RX enable:  fw 0x0801B9CC  (REG_30 = 0xBFF1)
 */

#include "app/spectrum.h"
#include "app/display.h"
#include "app/keypad.h"
#include "app/encoder.h"
#include "drivers/bk4829.h"
#include "drivers/lcd.h"

#include <string.h>

/* State ------------------------------------------------------------------ */

static spectrum_state_t spec;

/* RSSI-to-bar height scaling.
 * REG_0C raw value is 16-bit but useful RSSI range is roughly 0-1023.
 * Scale to SPEC_BAR_H (270) pixels. Shift right by 2 and clamp. */
static uint16_t rssi_to_height(uint16_t raw)
{
    uint16_t val = raw >> 2;
    if (val > SPEC_BAR_H)
        val = SPEC_BAR_H;
    return val;
}

/* Color gradient: green (weak) -> yellow (mid) -> red (strong) */
static uint16_t height_to_color(uint16_t h)
{
    if (h < SPEC_BAR_H / 3)
        return COLOR_GREEN;
    if (h < (SPEC_BAR_H * 2) / 3)
        return COLOR_YELLOW;
    return COLOR_RED;
}

/* Frequency for a given bin index */
static uint32_t bin_freq(uint16_t bin)
{
    uint32_t start = spec.center_freq - spec.span / 2;
    uint32_t step_per_bin = spec.span / SPEC_NUM_BINS;
    if (step_per_bin < spec.step)
        step_per_bin = spec.step;
    return start + (uint32_t)bin * step_per_bin;
}

/* Draw header: center freq, span, step, mode */
static void draw_header(void)
{
    lcd_fill_rect(0, SPEC_HEADER_Y, 240, SPEC_HEADER_H, COLOR_BLACK);

    uint32_t mhz = spec.center_freq / 1000000;
    uint32_t khz = (spec.center_freq / 1000) % 1000;
    display_printf(2, SPEC_HEADER_Y + 2, COLOR_WHITE, COLOR_BLACK,
                   "%3lu.%03lu MHz", mhz, khz);

    uint32_t span_khz = spec.span / 1000;
    const char *mode_str = "LIVE";
    if (spec.mode == SPEC_MODE_PEAK)
        mode_str = "PEAK";
    else if (spec.mode == SPEC_MODE_FROZEN)
        mode_str = "HOLD";

    display_printf(130, SPEC_HEADER_Y + 2, COLOR_CYAN, COLOR_BLACK,
                   "%luK %s", span_khz, mode_str);
}

/* Draw footer: marker frequency and RSSI */
static void draw_footer(void)
{
    lcd_fill_rect(0, SPEC_FOOTER_Y, 240, SPEC_FOOTER_H, COLOR_BLACK);

    uint32_t mf = bin_freq(spec.marker_bin);
    uint32_t mhz = mf / 1000000;
    uint32_t khz = (mf / 1000) % 1000;
    uint16_t rssi_val = spec.rssi[spec.marker_bin];

    display_printf(2, SPEC_FOOTER_Y + 2, COLOR_ORANGE, COLOR_BLACK,
                   "M:%3lu.%03lu  RSSI:%u", mhz, khz, rssi_val);

    uint32_t step_khz = spec.step / 1000;
    uint32_t step_frac = (spec.step % 1000) / 100;
    if (step_frac)
        display_printf(2, SPEC_FOOTER_Y + 14, COLOR_GRAY, COLOR_BLACK,
                       "Step:%lu.%luK  Ch:%u", step_khz, step_frac,
                       spec.chip);
    else
        display_printf(2, SPEC_FOOTER_Y + 14, COLOR_GRAY, COLOR_BLACK,
                       "Step:%luK  Ch:%u", step_khz, spec.chip);
}

/* Draw one column of the bar graph */
static void draw_bar(uint16_t bin)
{
    uint16_t h = rssi_to_height(spec.rssi[bin]);
    uint16_t ph = rssi_to_height(spec.peak[bin]);
    uint16_t x = SPEC_BAR_X + bin;
    uint16_t bar_bottom = SPEC_BAR_Y + SPEC_BAR_H;

    /* Clear column above the bar */
    if (h < SPEC_BAR_H)
        lcd_fill_rect(x, SPEC_BAR_Y, 1, SPEC_BAR_H - h, COLOR_BLACK);

    /* Draw the bar */
    if (h > 0)
        lcd_fill_rect(x, bar_bottom - h, 1, h, height_to_color(h));

    /* Peak marker (single white pixel line) */
    if (spec.mode == SPEC_MODE_PEAK && ph > h && ph <= SPEC_BAR_H)
        lcd_fill_rect(x, bar_bottom - ph, 1, 1, COLOR_WHITE);

    /* Marker cursor */
    if (bin == spec.marker_bin)
        lcd_fill_rect(x, bar_bottom - 2, 1, 2, COLOR_CYAN);
}

/* Perform one full sweep */
static void do_sweep(void)
{
    uint32_t step_per_bin = spec.span / SPEC_NUM_BINS;
    if (step_per_bin < spec.step)
        step_per_bin = spec.step;

    uint32_t freq = spec.center_freq - spec.span / 2;

    for (uint16_t i = 0; i < SPEC_NUM_BINS; i++) {
        bk4829_set_frequency(spec.chip, freq);
        uint16_t raw = bk4829_read_rssi(spec.chip);

        if (spec.mode == SPEC_MODE_PEAK) {
            spec.rssi[i] = raw;
            if (raw > spec.peak[i])
                spec.peak[i] = raw;
        } else {
            spec.rssi[i] = raw;
        }

        draw_bar(i);
        freq += step_per_bin;
    }
}

/* Clamp center frequency to keep sweep in valid RF range */
static void clamp_center(void)
{
    uint32_t half = spec.span / 2;
    if (spec.center_freq < half + 18000000)
        spec.center_freq = half + 18000000;
    if (spec.center_freq + half > 1300000000)
        spec.center_freq = 1300000000 - half;
}

/* Handle encoder rotation */
static void handle_encoder(void)
{
    int8_t dir = encoder_poll();
    if (dir == 0)
        return;

    /* Shift center frequency by step * 10 per detent */
    int32_t shift = (int32_t)spec.step * 10 * dir;
    uint32_t new_center = spec.center_freq + (uint32_t)shift;

    /* Underflow protection */
    if (dir < 0 && new_center > spec.center_freq)
        new_center = spec.span / 2 + 18000000;

    spec.center_freq = new_center;
    clamp_center();
    draw_header();
}

/* Handle key events */
static int handle_keys(void)
{
    key_event_t evt;
    keypad_get_event(&evt);

    if (evt.type != KEY_EVT_PRESS)
        return 1;

    switch (evt.key) {
    case KEY_C_MENU:
        /* Exit spectrum mode */
        return 0;

    case KEY_1:
        /* Decrease span */
        if (spec.span > SPEC_MIN_SPAN) {
            spec.span /= 2;
            if (spec.span < SPEC_MIN_SPAN)
                spec.span = SPEC_MIN_SPAN;
            clamp_center();
            draw_header();
        }
        break;

    case KEY_4:
        /* Increase span */
        if (spec.span < SPEC_MAX_SPAN) {
            spec.span *= 2;
            if (spec.span > SPEC_MAX_SPAN)
                spec.span = SPEC_MAX_SPAN;
            clamp_center();
            draw_header();
        }
        break;

    case KEY_2:
        /* Decrease step */
        if (spec.step > SPEC_MIN_STEP) {
            spec.step /= 2;
            if (spec.step < SPEC_MIN_STEP)
                spec.step = SPEC_MIN_STEP;
            draw_footer();
        }
        break;

    case KEY_5:
        /* Increase step */
        if (spec.step < SPEC_MAX_STEP) {
            spec.step *= 2;
            if (spec.step > SPEC_MAX_STEP)
                spec.step = SPEC_MAX_STEP;
            draw_footer();
        }
        break;

    case KEY_3:
        /* Cycle mode: LIVE -> PEAK -> FROZEN -> LIVE */
        if (spec.mode == SPEC_MODE_LIVE)
            spec.mode = SPEC_MODE_PEAK;
        else if (spec.mode == SPEC_MODE_PEAK)
            spec.mode = SPEC_MODE_FROZEN;
        else
            spec.mode = SPEC_MODE_LIVE;
        draw_header();
        break;

    case KEY_0:
        /* Reset peak hold */
        memset(spec.peak, 0, sizeof(spec.peak));
        break;

    case KEY_STAR:
        /* Marker left */
        if (spec.marker_bin > 0) {
            uint16_t old = spec.marker_bin;
            spec.marker_bin--;
            draw_bar(old);
            draw_bar(spec.marker_bin);
            draw_footer();
        }
        break;

    case KEY_HASH:
        /* Marker right */
        if (spec.marker_bin < SPEC_NUM_BINS - 1) {
            uint16_t old = spec.marker_bin;
            spec.marker_bin++;
            draw_bar(old);
            draw_bar(spec.marker_bin);
            draw_footer();
        }
        break;

    default:
        break;
    }

    return 1;
}

/* Public API ------------------------------------------------------------- */

void spectrum_init(void)
{
    memset(&spec, 0, sizeof(spec));
    spec.center_freq = SPEC_DEFAULT_CENTER;
    spec.span        = SPEC_DEFAULT_SPAN;
    spec.step        = SPEC_DEFAULT_STEP;
    spec.mode        = SPEC_MODE_LIVE;
    spec.marker_bin  = SPEC_NUM_BINS / 2;
}

void spectrum_enter(uint8_t chip)
{
    spectrum_init();
    spec.chip   = chip;
    spec.active = 1;

    /* Mute the other chip to avoid interference */
    bk4829_write_reg(chip ^ 1, 0x30, 0x0000);

    display_clear(COLOR_BLACK);
    draw_header();
    draw_footer();

    /* Draw frequency axis ticks */
    for (uint16_t x = 0; x < 240; x += 48)
        display_draw_vline(x, SPEC_BAR_Y + SPEC_BAR_H, 3, COLOR_DARK_GRAY);
}

void spectrum_exit(void)
{
    spec.active = 0;

    /* Restore both chips to standby; radio_init will reconfigure */
    bk4829_write_reg(0, 0x30, 0x0000);
    bk4829_write_reg(1, 0x30, 0x0000);
}

int spectrum_poll(void)
{
    if (!spec.active)
        return 0;

    /* Handle user input */
    if (!handle_keys()) {
        spectrum_exit();
        return 0;
    }

    handle_encoder();

    /* Perform sweep unless frozen */
    if (spec.mode != SPEC_MODE_FROZEN)
        do_sweep();

    draw_footer();

    return 1;
}

int spectrum_is_active(void)
{
    return spec.active;
}
