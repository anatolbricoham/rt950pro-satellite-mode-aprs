/*
 * scanner.c - VFO and memory channel scanning for the RT-950 Pro
 *
 * Implements a polling-based scan engine called from the main loop at ~100Hz.
 * Supports VFO frequency sweep and memory channel stepping with three
 * resume modes: carrier-detect, timed, and manual stop.
 *
 * Timing is based on a tick counter incremented each poll call (~10ms/tick):
 *   Step interval:    100ms default (10 ticks)
 *   Settle time:       30ms after frequency change (3 ticks)
 *   Carrier holdoff:  500ms after squelch closes (50 ticks)
 *   Time resume:     5000ms default (500 ticks)
 */

#include "app/scanner.h"
#include "app/vfo.h"
#include "app/channel.h"
#include "drivers/bk4829.h"

/* Timing constants (in poll ticks at ~100Hz ~ 10ms each) */
#define DEFAULT_STEP_TICKS      10  /* 100ms per step */
#define SETTLE_TICKS             3  /* 30ms settle after freq change */
#define CARRIER_HOLDOFF_TICKS   50  /* 500ms holdoff for carrier resume */
#define DEFAULT_RESUME_TICKS   500  /* 5000ms for TIME resume */

/* Scanner state -------------------------------------------------------- */

static scan_mode_t    scan_mode       = SCAN_OFF;
static scan_resume_t  resume_mode     = SCAN_RESUME_CARRIER;
static uint8_t        paused;
static uint16_t       tick_counter;
static uint16_t       step_ticks      = DEFAULT_STEP_TICKS;
static uint16_t       settle_counter;
static uint16_t       resume_countdown;
static uint16_t       resume_ticks    = DEFAULT_RESUME_TICKS;
static uint16_t       current_channel;
static uint8_t        squelch_debounce; /* Consecutive open readings needed */

/* VFO scan range (0/0 = full band, no custom range) */
static uint32_t       vfo_range_start;
static uint32_t       vfo_range_stop;

#define SQUELCH_DEBOUNCE_COUNT  3  /* 3 consecutive open readings (~30ms) */

/* Helpers -------------------------------------------------------------- */

/* Step VFO frequency within a custom range, wrapping at edges.
 * Handles both forward (positive step) and reverse (negative step) scanning.
 * OEM scanner @ 0x08009B88 wraps at both band boundaries. */
static void vfo_step_in_range(radio_vfo_t vfo)
{
    const vfo_state_t *st = vfo_get_state(vfo);
    uint32_t freq = st->freq_hz + st->step_hz;

    if (freq > vfo_range_stop)
        freq = vfo_range_start;
    else if (freq < vfo_range_start)
        freq = vfo_range_stop;

    vfo_set_frequency(vfo, freq);
}

/* Public API ----------------------------------------------------------- */

void scanner_init(void)
{
    scan_mode       = SCAN_OFF;
    resume_mode     = SCAN_RESUME_CARRIER;
    paused          = 0;
    tick_counter    = 0;
    step_ticks      = DEFAULT_STEP_TICKS;
    settle_counter  = 0;
    resume_countdown = 0;
    resume_ticks    = DEFAULT_RESUME_TICKS;
    current_channel = 0;
    vfo_range_start = 0;
    vfo_range_stop  = 0;
}

void scanner_start(scan_mode_t mode)
{
    if (mode == SCAN_OFF) {
        scanner_stop();
        return;
    }

    scan_mode       = mode;
    paused          = 0;
    tick_counter    = 0;
    settle_counter  = 0;

    if (mode == SCAN_MEMORY) {
        current_channel = channel_find_next_scannable(current_channel, +1);
    }
}

void scanner_stop(void)
{
    scan_mode      = SCAN_OFF;
    paused         = 0;
    tick_counter   = 0;
    settle_counter = 0;
}

scan_mode_t scanner_get_mode(void)
{
    return scan_mode;
}

uint8_t scanner_is_paused(void)
{
    return paused;
}

void scanner_set_resume(scan_resume_t resume)
{
    resume_mode = resume;
}

void scanner_set_speed(uint16_t ms_per_step)
{
    step_ticks = ms_per_step / 10;
    if (step_ticks == 0)
        step_ticks = 1;
}

void scanner_set_vfo_range(uint32_t start_hz, uint32_t stop_hz)
{
    vfo_range_start = start_hz;
    vfo_range_stop  = stop_hz;
}

uint16_t scanner_get_channel(void)
{
    if (scan_mode == SCAN_MEMORY)
        return current_channel;
    return 0xFFFF;
}

/* Poll - called from main loop at ~100Hz ------------------------------- */

void scanner_poll(void)
{
    if (scan_mode == SCAN_OFF)
        return;

    radio_vfo_t active_vfo = vfo_get_active();
    uint8_t chip = vfo_get_state(active_vfo)->chip;

    /* Paused state: handle resume logic -------------------------------- */
    if (paused) {
        switch (resume_mode) {
        case SCAN_RESUME_CARRIER:
            if (!bk4829_is_squelch_open(chip)) {
                /* Carrier dropped - count down holdoff */
                if (resume_countdown > 0)
                    resume_countdown--;
                if (resume_countdown == 0)
                    paused = 0;
            } else {
                /* Still receiving - reset holdoff */
                resume_countdown = CARRIER_HOLDOFF_TICKS;
            }
            break;

        case SCAN_RESUME_TIME:
            if (resume_countdown > 0)
                resume_countdown--;
            if (resume_countdown == 0)
                paused = 0;
            break;

        case SCAN_RESUME_STOP:
            /* Manual only - wait for scanner_start() or scanner_stop() */
            break;
        }
        return;
    }

    /* Settling: wait after frequency change, then check squelch -------- */
    if (settle_counter > 0) {
        settle_counter--;
        if (settle_counter == 0) {
            /* Start debounce - need SQUELCH_DEBOUNCE_COUNT consecutive opens */
            squelch_debounce = 0;
        }
        return;
    }

    /* Debounce squelch: require multiple consecutive open readings
     * to avoid false pauses from noise bursts near threshold. */
    if (squelch_debounce < SQUELCH_DEBOUNCE_COUNT) {
        if (bk4829_is_squelch_open(chip)) {
            squelch_debounce++;
            if (squelch_debounce >= SQUELCH_DEBOUNCE_COUNT) {
                paused = 1;
                resume_countdown = (resume_mode == SCAN_RESUME_CARRIER)
                                   ? CARRIER_HOLDOFF_TICKS
                                   : resume_ticks;
            }
        } else {
            /* Squelch closed - reset debounce, proceed to next step */
            squelch_debounce = SQUELCH_DEBOUNCE_COUNT;
        }
        return;
    }

    /* Wait for step interval ------------------------------------------- */
    tick_counter++;
    if (tick_counter < step_ticks)
        return;
    tick_counter = 0;

    /* Step to next frequency or channel -------------------------------- */
    if (scan_mode == SCAN_VFO) {
        if (vfo_range_start != 0 && vfo_range_stop != 0) {
            vfo_step_in_range(active_vfo);
        } else {
            vfo_step(+1);
        }
    } else {
        uint16_t next = channel_find_next_scannable(current_channel, +1);
        if (next == 0xFFFF) {
            /* Wrap around to beginning of channel list */
            next = channel_find_next_scannable(0, +1);
            if (next == 0xFFFF) {
                /* No scannable channels at all */
                scanner_stop();
                return;
            }
        }
        current_channel = next;

        channel_t ch;
        channel_load(current_channel, &ch);
        channel_to_vfo(&ch, active_vfo);
    }

    /* Begin settle period before checking squelch */
    settle_counter = SETTLE_TICKS;
}
