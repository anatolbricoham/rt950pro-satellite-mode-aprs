/*
 * crossband.c - Cross-band repeat for the RT-950 Pro
 *
 * The RT-950 has two independent BK4829 RF transceiver chips:
 *   VFO A -> CHIP1 (PE15/SEN2)
 *   VFO B -> CHIP0 (PE8/SEN1)
 *
 * Cross-band repeat works by monitoring squelch on the RX band and
 * keying up TX on the opposite band to relay received audio.
 *
 * Audio routing relies on the relay board (PE7/PB1/PE14) switching
 * between MIC and accessory paths.  The OEM relay_select() mode 2
 * (audio path) handles this per-subband.  For cross-band repeat,
 * the relay must route audio from one BK4829's AF output to the
 * other BK4829's modulation input.
 *
 * TOT (TX timeout) is enforced independently to prevent stuck relays.
 */

#include "app/crossband.h"
#include "app/radio.h"
#include "app/vfo.h"
#include "app/settings.h"
#include "app/audio.h"
#include "drivers/bk4829.h"
#include "drivers/calibration.h"

extern uint32_t get_tick(void);

static xband_mode_t mode;
static uint8_t      relaying;
static uint16_t     relay_seconds;    /* TX elapsed for TOT enforcement */
static uint8_t      hang_countdown;   /* carrier hang time before drop */

#define HANG_TIME_TICKS  10   /* 2 seconds at 5 Hz poll rate */
#define XBAND_TOT_SEC    180  /* 3-minute max relay time */

xband_mode_t crossband_get_mode(void) { return mode; }

void crossband_set_mode(xband_mode_t m)
{
    if (m == mode) return;

    /* Stop any active relay before changing mode */
    if (relaying) {
        radio_ptt_off();
        relaying = 0;
    }

    mode = m;
    relay_seconds = 0;
    hang_countdown = 0;
}

int crossband_is_relaying(void) { return relaying; }

/*
 * Determine which VFO to monitor for RX and which to TX on,
 * based on the current mode.  Returns 0 if mode is off.
 */
static int get_rx_tx_vfos(radio_vfo_t *rx_vfo, radio_vfo_t *tx_vfo)
{
    switch (mode) {
    case XBAND_A_TO_B:
        *rx_vfo = RADIO_VFO_A;
        *tx_vfo = RADIO_VFO_B;
        return 1;
    case XBAND_B_TO_A:
        *rx_vfo = RADIO_VFO_B;
        *tx_vfo = RADIO_VFO_A;
        return 1;
    case XBAND_DUPLEX:
        /* For duplex, detect which side has signal */
        if (bk4829_is_squelch_open(vfo_get_state(RADIO_VFO_B)->chip)) {
            *rx_vfo = RADIO_VFO_B;
            *tx_vfo = RADIO_VFO_A;
            return 1;
        }
        if (bk4829_is_squelch_open(vfo_get_state(RADIO_VFO_A)->chip)) {
            *rx_vfo = RADIO_VFO_A;
            *tx_vfo = RADIO_VFO_B;
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

void crossband_poll(void)
{
    if (mode == XBAND_OFF) return;

    /* Don't interfere with manual PTT */
    if (radio_is_transmitting() && !relaying) return;

    radio_vfo_t rx_vfo, tx_vfo;

    if (relaying) {
        /* Check if RX carrier is still present */
        int have_signal = get_rx_tx_vfos(&rx_vfo, &tx_vfo);
        const vfo_state_t *vs_rx = vfo_get_state(rx_vfo);

        if (have_signal && bk4829_is_squelch_open(vs_rx->chip)) {
            hang_countdown = HANG_TIME_TICKS;

            /* Enforce TOT */
            relay_seconds++;
            if (relay_seconds >= XBAND_TOT_SEC * 5) {
                radio_ptt_off();
                relaying = 0;
                relay_seconds = 0;
                audio_error_beep();
                return;
            }
        } else {
            if (hang_countdown > 0) {
                hang_countdown--;
            } else {
                radio_ptt_off();
                relaying = 0;
                relay_seconds = 0;
            }
        }
        return;
    }

    /* Not relaying: check for incoming signal */
    if (!get_rx_tx_vfos(&rx_vfo, &tx_vfo)) return;

    const vfo_state_t *vs_rx = vfo_get_state(rx_vfo);
    if (!bk4829_is_squelch_open(vs_rx->chip)) return;

    /* TX band check */
    uint32_t tx_freq = vfo_get_tx_freq(tx_vfo);
    if (!calibration_tx_allowed(&cal_data, tx_freq)) return;

    /* Signal detected on RX band, start relay TX on opposite band */
    vfo_set_active(tx_vfo);
    radio_ptt_on();
    relaying = 1;
    relay_seconds = 0;
    hang_countdown = HANG_TIME_TICKS;
}
