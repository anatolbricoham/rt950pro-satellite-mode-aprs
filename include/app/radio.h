/*
 * radio.h - High-level radio control for the RT-950 Pro
 */

#ifndef APP_RADIO_H
#define APP_RADIO_H

#include <stdint.h>

typedef enum {
    RADIO_VFO_A = 0,
    RADIO_VFO_B = 1,
    RADIO_VFO_C = 2,
} radio_vfo_t;

typedef enum {
    RADIO_MOD_FM = 0,
    RADIO_MOD_AM = 1,
    RADIO_MOD_USB = 2,
    RADIO_MOD_LSB = 3,
} radio_mod_t;

void radio_init(void);
void radio_set_frequency(radio_vfo_t vfo, uint32_t freq_hz);
void radio_set_modulation(radio_vfo_t vfo, radio_mod_t mod);
void radio_ptt_on(void);
void radio_ptt_off(void);

/* Returns 1 if currently transmitting */
int radio_is_transmitting(void);

/* Call at 1 Hz - enforces TX time-out timer, forces ptt_off on expiry */
void radio_tot_poll(void);

/* Returns 1 if TOT warning zone active (last 10 s before cutoff) */
uint8_t radio_tot_warning_active(void);

/*
 * Dual-watch poll - call at ~5 Hz when idle.
 * Alternates RSSI reads on inactive VFO.  When signal detected,
 * temporarily switches display/audio focus until carrier drops.
 */
void radio_dual_watch_poll(void);

/* Returns the VFO currently receiving audio (may differ from active when DW) */
radio_vfo_t radio_get_rx_vfo(void);

/*
 * DTMF RX decode - accumulates incoming DTMF digits during RX.
 * Call radio_dtmf_decode_poll() at ~50 Hz (keypad tick).
 * Buffer is cleared when TX starts or after 3 s of no new digits.
 */
void radio_dtmf_decode_poll(void);
const char *radio_dtmf_decode_buf(void);  /* current decode string */

/* UI event handlers - called from main loop */
void radio_handle_key(uint8_t key, uint8_t event_type);
void radio_handle_encoder(int8_t direction);

#endif /* APP_RADIO_H */
