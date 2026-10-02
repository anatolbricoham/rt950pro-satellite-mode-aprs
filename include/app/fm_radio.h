/*
 * fm_radio.h - FM broadcast receiver UI for the RT-950 Pro
 *
 * User interface layer on top of the SI4732 AM/FM/SW receiver driver.
 * Provides frequency display, manual tuning, seek, presets, and
 * stereo / signal-strength indicators.
 */

#ifndef APP_FM_RADIO_H
#define APP_FM_RADIO_H

#include <stdint.h>

/* FM radio mode states */
typedef enum {
    FM_STATE_OFF = 0,
    FM_STATE_PLAYING,
    FM_STATE_SEEKING,
} fm_state_t;

/* FM band limits */
#define FM_FREQ_MIN     8750    /* 87.50 MHz in 10kHz units */
#define FM_FREQ_MAX     10800   /* 108.00 MHz */
#define FM_FREQ_STEP    10      /* 100 kHz step (in 10kHz units) */

/* Number of preset stations */
#define FM_PRESET_COUNT 16

/* Initialize FM radio subsystem */
void fm_radio_init(void);

/* Enter/exit FM radio mode */
void fm_radio_enter(void);
void fm_radio_exit(void);

/* Get current state */
fm_state_t fm_radio_get_state(void);

/* Manual tune (encoder) */
void fm_radio_tune_step(int8_t direction);

/* Seek next/previous station */
void fm_radio_seek(int8_t direction);  /* +1=up, -1=down */

/* Get current frequency (in 10kHz units, e.g., 10150 = 101.50 MHz) */
uint16_t fm_radio_get_freq(void);

/* Set frequency directly */
void fm_radio_set_freq(uint16_t freq_10khz);

/* Preset management */
void fm_radio_save_preset(uint8_t index);
void fm_radio_load_preset(uint8_t index);
uint16_t fm_radio_get_preset(uint8_t index);

/* Persist all presets + current freq to flash (WL_EXTCFG) */
void fm_radio_persist(void);

/* Restore presets from flash (call at init) */
void fm_radio_restore(void);

/* Get signal info */
uint8_t fm_radio_get_rssi(void);
uint8_t fm_radio_is_stereo(void);

/* Handle input events (called from main loop when FM mode active) */
void fm_radio_handle_encoder(int8_t direction);
void fm_radio_handle_key(uint8_t key);

/* Draw FM radio screen */
void fm_radio_draw(void);

#endif /* APP_FM_RADIO_H */
