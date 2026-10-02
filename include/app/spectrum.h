/*
 * spectrum.h - Spectrum analyzer mode for the RT-950 Pro
 *
 * Sweeps one BK4829 across a frequency range, reads RSSI at each step,
 * and renders a real-time bar graph on the LCD. The encoder adjusts
 * center frequency; keys control span, step, and hold/peak modes.
 *
 * LCD layout (240x320):
 *   Row 0-19:    Header (center freq, span, step)
 *   Row 20-289:  Spectrum bars (270 pixels tall, 240 columns)
 *   Row 290-319: Footer (marker freq, RSSI, controls)
 */

#ifndef APP_SPECTRUM_H
#define APP_SPECTRUM_H

#include <stdint.h>

/* Display geometry */
#define SPEC_BAR_X          0
#define SPEC_BAR_Y          20
#define SPEC_BAR_W          240     /* one bar per pixel column */
#define SPEC_BAR_H          270     /* vertical height for bars */
#define SPEC_HEADER_Y       0
#define SPEC_HEADER_H       20
#define SPEC_FOOTER_Y       290
#define SPEC_FOOTER_H       30

/* Sweep defaults */
#define SPEC_DEFAULT_CENTER 145000000   /* 145.000 MHz */
#define SPEC_DEFAULT_SPAN   3000000     /* 3 MHz total span */
#define SPEC_DEFAULT_STEP   12500       /* 12.5 kHz per bin */
#define SPEC_MIN_STEP       2500        /* 2.5 kHz minimum */
#define SPEC_MAX_STEP       100000      /* 100 kHz maximum */
#define SPEC_MIN_SPAN       250000      /* 250 kHz minimum span */
#define SPEC_MAX_SPAN       30000000    /* 30 MHz maximum span */

/* Number of bins = LCD width */
#define SPEC_NUM_BINS       SPEC_BAR_W

typedef enum {
    SPEC_MODE_LIVE,     /* continuous sweep, bars update each pass */
    SPEC_MODE_PEAK,     /* peak-hold: bars only grow, never shrink */
    SPEC_MODE_FROZEN    /* display frozen, no sweeps */
} spectrum_mode_t;

typedef struct {
    uint32_t        center_freq;    /* center frequency in Hz */
    uint32_t        span;           /* total span in Hz */
    uint32_t        step;           /* step size in Hz */
    spectrum_mode_t mode;
    uint8_t         chip;           /* BK4829 chip index (0 or 1) */
    uint8_t         active;         /* nonzero = spectrum mode running */
    uint8_t         marker_bin;     /* cursor position (0-239) */
    uint16_t        rssi[SPEC_NUM_BINS];
    uint16_t        peak[SPEC_NUM_BINS];
} spectrum_state_t;

/*
 * spectrum_init - Reset state to defaults. Does not start sweeping.
 */
void spectrum_init(void);

/*
 * spectrum_enter - Enter spectrum analyzer mode.
 * Mutes normal radio operation, clears screen, begins sweeping.
 * chip: which BK4829 to use (0 or 1).
 */
void spectrum_enter(uint8_t chip);

/*
 * spectrum_exit - Leave spectrum mode and restore normal radio operation.
 */
void spectrum_exit(void);

/*
 * spectrum_poll - Call from main loop while spectrum is active.
 * Performs one full sweep, updates the display, handles input.
 * Returns nonzero while spectrum mode is active.
 */
int spectrum_poll(void);

/*
 * spectrum_is_active - Returns nonzero if spectrum mode is running.
 */
int spectrum_is_active(void);

#endif /* APP_SPECTRUM_H */
