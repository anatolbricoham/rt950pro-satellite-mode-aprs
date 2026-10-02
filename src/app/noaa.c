/*
 * noaa.c - NOAA Weather Radio receiver for the RT-950 Pro
 *
 * Uses the SI4732 in WB (Weather Band) mode to receive the 7 standard
 * NOAA weather radio frequencies.  Background alert monitoring can be
 * enabled via settings.noaa_alarm to periodically check for weather
 * alerts on the selected channel.
 *
 * NOAA All Hazards frequencies:
 *   Ch 1: 162.400 MHz    Ch 2: 162.425 MHz    Ch 3: 162.450 MHz
 *   Ch 4: 162.475 MHz    Ch 5: 162.500 MHz    Ch 6: 162.525 MHz
 *   Ch 7: 162.550 MHz
 */

#include "app/noaa.h"
#include "app/audio.h"
#include "app/settings.h"
#include "drivers/si4732.h"

#include <stdint.h>

extern uint32_t get_tick(void);

const uint32_t noaa_freq_khz[NOAA_NUM_CHANNELS] = {
    162400, 162425, 162450, 162475, 162500, 162525, 162550
};

static uint8_t active;
static uint8_t cur_channel;
static uint8_t last_rssi;

/* Alert monitoring state */
static uint32_t alert_next_ms;
static uint8_t  alert_triggered;

#define ALERT_POLL_INTERVAL_MS  5000   /* check every 5 seconds */
#define ALERT_RSSI_THRESHOLD   20      /* minimum RSSI to trigger alert */

void noaa_enter(void)
{
    if (active) return;

    si4732_power_down();
    if (si4732_power_up_wb() < 0)
        return;

    si4732_wb_tune(noaa_freq_khz[cur_channel]);
    active = 1;
    last_rssi = 0;
}

void noaa_exit(void)
{
    if (!active) return;
    si4732_power_down();
    active = 0;
    last_rssi = 0;
}

void noaa_step(int8_t direction)
{
    if (!active) return;

    if (direction > 0) {
        cur_channel = (cur_channel + 1) % NOAA_NUM_CHANNELS;
    } else if (direction < 0) {
        cur_channel = (cur_channel == 0)
                    ? (uint8_t)(NOAA_NUM_CHANNELS - 1)
                    : (uint8_t)(cur_channel - 1);
    }

    si4732_wb_tune(noaa_freq_khz[cur_channel]);
}

uint8_t noaa_get_channel(void)  { return cur_channel; }
uint32_t noaa_get_freq_khz(void) { return noaa_freq_khz[cur_channel]; }
int noaa_is_active(void)        { return active; }

uint8_t noaa_get_rssi(void)
{
    if (!active) return 0;

    struct si4732_tune_status st;
    if (si4732_wb_tune_status(&st) == 0)
        last_rssi = st.rssi;

    return last_rssi;
}

void noaa_alert_poll(void)
{
    const settings_t *s = settings_get();
    if (!s->noaa_alarm) {
        alert_triggered = 0;
        return;
    }

    /* Don't interfere with active NOAA listening */
    if (active) return;

    uint32_t now = get_tick();
    if (now < alert_next_ms) return;
    alert_next_ms = now + ALERT_POLL_INTERVAL_MS;

    /*
     * Quick probe: power up WB, tune, check RSSI, power down.
     * This briefly interrupts SI4732 if it's in FM/AM mode.
     * A future optimization could use the SI4732's interrupt pin
     * to avoid repeated power cycles.
     */
    si4732_power_down();
    if (si4732_power_up_wb() < 0) return;
    si4732_wb_tune(noaa_freq_khz[cur_channel]);

    /* Short settle time for tuning */
    for (volatile int i = 0; i < 50000; i++) { }

    struct si4732_tune_status st;
    if (si4732_wb_tune_status(&st) == 0 && st.rssi >= ALERT_RSSI_THRESHOLD) {
        if (!alert_triggered) {
            audio_beep();
            alert_triggered = 1;
        }
    } else {
        alert_triggered = 0;
    }

    si4732_power_down();
}
