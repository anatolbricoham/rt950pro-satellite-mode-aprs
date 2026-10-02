/*
 * vfo.h - Triple VFO management for the RT-950 Pro
 *
 * VFO A maps to BK4829 CHIP1 (PE15/SEN2)
 * VFO B maps to BK4829 CHIP0 (PE8/SEN1)
 * VFO C shares BK4829 CHIP1 with VFO A (time-multiplexed)
 *
 * VFO-C is activated via the CPS "VFO C Mode" setting (0x9010+10[5:4]).
 * When VFO-C is active and selected, chip 1 is reprogrammed with VFO-C
 * settings, and VFO-A is suspended until the user switches back.
 */

#ifndef APP_VFO_H
#define APP_VFO_H

#include <stdint.h>
#include "app/radio.h"

/* Number of VFO slots (A, B, C) */
#define VFO_COUNT   3

/* Per-VFO state */
typedef struct {
    uint32_t    freq_hz;
    uint8_t     chip;           /* BK4829_CHIP0 or BK4829_CHIP1 */
    uint8_t     tx_power;       /* 0=low, 1=mid, 2=high */
    uint8_t     modulation;     /* radio_mod_t */
    uint8_t     squelch_level;  /* 0-9 */
    uint8_t     ctcss_tx_idx;   /* 0xFF = off, 0-49 = tone index */
    uint8_t     ctcss_rx_idx;   /* 0xFF = off */
    uint8_t     dcs_code_idx;   /* 0xFF = off, 0-103 = code index */
    uint8_t     dcs_polarity;   /* 0=normal, 1=inverted */
    uint8_t     scrambler;      /* 0=off, 1=on */
    uint8_t     bandwidth;      /* 0=narrow (12.5kHz), 1=wide (25kHz) */
    uint8_t     rf_cal;         /* 6-bit RF calibration for BK4829 REG_48 */
    uint8_t     offset_dir;     /* 0=None, 1=positive(+), 2=negative(-) */
    uint8_t     busy_lockout;   /* 0=off, 1=on (BCL: refuse TX if channel busy) */
    uint16_t    channel_num;    /* Memory channel # (0-989), 0xFFFF = VFO mode */
    uint32_t    step_hz;        /* Tuning step size */
    uint32_t    offset_freq_hz; /* TX offset frequency in Hz */
} vfo_state_t;

/* Initialize dual VFO system */
void vfo_init(void);

/* Get/set active VFO (A or B) */
radio_vfo_t vfo_get_active(void);
void vfo_set_active(radio_vfo_t vfo);

/* Toggle active VFO A<->B (or A<->B<->C when VFO-C is enabled) */
void vfo_toggle(void);

/* Swap VFO A and B settings */
void vfo_swap(void);

/* Copy active VFO settings to inactive */
void vfo_copy(void);

/* Get pointer to VFO state (for reading settings) */
const vfo_state_t *vfo_get_state(radio_vfo_t vfo);

/* Apply all VFO settings to the BK4829 chip (frequency, tone, squelch, etc.) */
void vfo_apply(radio_vfo_t vfo);

/* Tune active VFO by one step in given direction */
void vfo_step(int8_t direction);

/* Set frequency on a VFO (also applies to hardware) */
void vfo_set_frequency(radio_vfo_t vfo, uint32_t freq_hz);

/* Set sub-audio (CTCSS/DCS) on a VFO */
void vfo_set_ctcss_tx(radio_vfo_t vfo, uint8_t tone_idx);
void vfo_set_ctcss_rx(radio_vfo_t vfo, uint8_t tone_idx);
void vfo_set_dcs(radio_vfo_t vfo, uint8_t code_idx, uint8_t polarity);
void vfo_clear_tone(radio_vfo_t vfo);

/* Set squelch level on a VFO */
void vfo_set_squelch(radio_vfo_t vfo, uint8_t level);

/* Set TX power */
void vfo_set_power(radio_vfo_t vfo, uint8_t level);

/* Set tuning step */
void vfo_set_step(radio_vfo_t vfo, uint32_t step_hz);

/* Set modulation mode (radio_mod_t) */
void vfo_set_modulation(radio_vfo_t vfo, uint8_t mod);

/* Set bandwidth (0=narrow, 1=wide) */
void vfo_set_bandwidth(radio_vfo_t vfo, uint8_t bw);

/* Set scrambler (0=off, 1=on); programs BK4829 immediately */
void vfo_set_scrambler(radio_vfo_t vfo, uint8_t on);

/* TX offset: direction (0=None, 1=+, 2=-) and frequency in Hz */
void vfo_set_offset_dir(radio_vfo_t vfo, uint8_t dir);
void vfo_set_offset_freq(radio_vfo_t vfo, uint32_t freq_hz);

/* Busy Channel Lockout (0=off, 1=on) */
void vfo_set_busy_lockout(radio_vfo_t vfo, uint8_t on);

/* Compute the actual TX frequency: rx_freq +/- offset (or rx_freq if dir==0) */
uint32_t vfo_get_tx_freq(radio_vfo_t vfo);

/* Toggle reverse: swap RX and TX frequencies for monitoring repeater input */
void vfo_reverse_toggle(radio_vfo_t vfo);

/*
 * Persist all VFO states + active VFO to flash (WL_VFOCFG).
 * Call after menu confirms, VFO swap/copy, or on a periodic save timer.
 * Do NOT call during rapid tuning - flash writes are expensive.
 */
void vfo_save(void);

/* VFO-C: enable/disable third VFO (set from CPS or menu) */
void vfo_set_c_enabled(uint8_t on);
uint8_t vfo_is_c_enabled(void);

#endif /* APP_VFO_H */
