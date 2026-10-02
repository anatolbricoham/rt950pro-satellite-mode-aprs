/*
 * noaa.h - NOAA Weather Radio receiver for the RT-950 Pro
 *
 * 7 standard NOAA weather channels (162.400-162.550 MHz) via SI4732 WB mode.
 * Background alert monitoring polls RSSI on user's selected NOAA channel
 * and triggers an audible alarm when signal detected (if noaa_alarm enabled).
 */

#ifndef APP_NOAA_H
#define APP_NOAA_H

#include <stdint.h>

#define NOAA_NUM_CHANNELS  7

/* NOAA channel frequencies in kHz */
extern const uint32_t noaa_freq_khz[NOAA_NUM_CHANNELS];

/* Enter NOAA weather radio mode (powers up SI4732 in WB, tunes channel) */
void noaa_enter(void);

/* Exit NOAA weather radio mode (powers down SI4732, restores radio) */
void noaa_exit(void);

/* Step to next/previous NOAA channel. direction: +1 or -1 */
void noaa_step(int8_t direction);

/* Get current channel index (0-6) */
uint8_t noaa_get_channel(void);

/* Get current frequency in kHz */
uint32_t noaa_get_freq_khz(void);

/* Returns 1 if NOAA mode is active */
int noaa_is_active(void);

/* Read current RSSI from SI4732 WB tune status */
uint8_t noaa_get_rssi(void);

/*
 * Background alert poll (call at ~1 Hz from main loop).
 * When noaa_alarm is enabled in settings, periodically checks the
 * selected NOAA channel for signal presence. If RSSI exceeds threshold,
 * triggers audio alert.
 */
void noaa_alert_poll(void);

#endif /* APP_NOAA_H */
