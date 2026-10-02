/*
 * am_radio.h - AM/SW broadcast receiver UI for the RT-950 Pro
 *
 * Extends the broadcast receiver beyond FM to support:
 *   - AM (MW): 520-1710 kHz, step 1/9/10 kHz
 *   - SW:      2300-26100 kHz, step 1/5 kHz
 *   - SSB:     150-30000 kHz, USB/LSB, BFO tuning
 *
 * Uses SI4732 in AM receiver mode (POWER_UP func=0x01).
 * SSB requires the SI4732 firmware patch ROM upload.
 */

#ifndef APP_AM_RADIO_H
#define APP_AM_RADIO_H

#include <stdint.h>

/* Band definitions -------------------------------------------------- */
typedef enum {
    AM_BAND_MW = 0,     /* Medium Wave  520-1710 kHz */
    AM_BAND_SW,         /* Short Wave   2300-26100 kHz */
    AM_BAND_SSB,        /* SSB (LSB/USB) 150-30000 kHz */
    AM_BAND_COUNT,
} am_band_t;

/* SSB sideband ------------------------------------------------------ */
typedef enum {
    SSB_LSB = 1,
    SSB_USB = 2,
} ssb_sideband_t;

/* AM radio state ---------------------------------------------------- */
typedef enum {
    AM_STATE_OFF = 0,
    AM_STATE_PLAYING,
    AM_STATE_SEEKING,
} am_state_t;

/* Band limits ------------------------------------------------------- */
#define AM_MW_MIN       520     /* kHz */
#define AM_MW_MAX       1710
#define AM_SW_MIN       2300
#define AM_SW_MAX       26100
#define AM_SSB_MIN      150
#define AM_SSB_MAX      30000

/* API --------------------------------------------------------------- */

/* Initialize AM radio subsystem */
void am_radio_init(void);

/* Enter AM/SW mode with specified band */
void am_radio_enter(am_band_t band);

/* Exit AM radio mode, power down SI4732 */
void am_radio_exit(void);

/* Get current state */
am_state_t am_radio_get_state(void);

/* Get current band */
am_band_t am_radio_get_band(void);

/* Switch to next band (MW -> SW -> SSB -> MW) */
void am_radio_cycle_band(void);

/* Tune step via encoder */
void am_radio_tune_step(int8_t direction);

/* Cycle through available step sizes for current band */
void am_radio_cycle_step(void);

/* Get current frequency in kHz */
uint16_t am_radio_get_freq(void);

/* Set frequency directly (kHz) */
void am_radio_set_freq(uint16_t freq_khz);

/* SSB: get/set BFO offset in Hz */
int16_t am_radio_get_bfo(void);
void am_radio_set_bfo(int16_t offset_hz);

/* SSB: get/set sideband */
ssb_sideband_t am_radio_get_sideband(void);
void am_radio_set_sideband(ssb_sideband_t sb);

/* AM/SW: set bandwidth filter index (0-4: 6K,4K,3K,2K,1K) */
void am_radio_set_bandwidth(uint8_t bw_idx);
uint8_t am_radio_get_bandwidth(void);

/* Get current step size in kHz */
uint16_t am_radio_get_step(void);

/* Get RSSI/SNR from last tune status */
uint8_t am_radio_get_rssi(void);
uint8_t am_radio_get_snr(void);

/* Handle key press in AM radio mode */
void am_radio_handle_key(uint8_t key);

/* Handle encoder rotation in AM radio mode */
void am_radio_handle_encoder(int8_t direction);

/* Draw AM radio UI (full screen) */
void am_radio_draw(void);

#endif /* APP_AM_RADIO_H */
