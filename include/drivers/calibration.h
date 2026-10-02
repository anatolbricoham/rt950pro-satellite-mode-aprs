/*
 * calibration.h - Calibration data loader for the RT-950 Pro
 *
 * V0.27 OEM calibration loader: 0x0800F1C8 - 0x0800F586
 *   Flash read target: 0x08021D7C
 *   BCD decode loops: 0x0800F3F6 (16B VHF), 0x0800F4EE (8B UHF)
 *   Default fallback: 0x0800F346 (ROM defaults @ 0x0802F722)
 *
 * Flash sector 0xF000 layout:
 *   +0x000 (192B) TX power: 12 bands x 16 bytes
 *   +0x0E0 (1B)   Validity flag (0xFF = invalid, also band edge marker)
 *   +0x0E0+0x49   Band edge marker 0xA5 (sub-band enable sentinel)
 *   +0x230 (16B)  VHF sub-band edges: 3 sub-bands, 2-byte BCD per edge
 *   +0x240 (8B)   UHF sub-band edge: 1 sub-band, 2-byte BCD per edge
 *
 * Battery thresholds: 7 levels, level[6] forced to 0xBB (9.01V cutoff).
 *   OEM does NOT read battery thresholds from 0xF200 (removed from our code).
 *
 * BCD decoding: byte-level (hi_nib*10 + lo_nib), halfword * 100000 -> Hz.
 * ADC-to-mV conversion uses 8-bit UBFX result (see adc.h).
 *
 * Unparsed OEM blocks at +0x0C0, +0x0D0, +0x0F0-0x1B0, +0x210 (RSSI, etc.)
 */

#ifndef DRIVERS_CALIBRATION_H
#define DRIVERS_CALIBRATION_H

#include <stdint.h>

/* Maximum number of TX sub-bands with enable flags and BCD band edges */
#define CAL_NUM_SUBBANDS  4

/*
 * Decoded TX sub-band edge record.
 * OEM stores each edge as 2-byte BCD in flash; we decode to MHz/10 and Hz
 * at load time.
 *
 * Default sub-band assignments (from OEM hardcoded fallback @ 0x0800F346):
 *   [0] 136.0 - 174.0 MHz  (2m VHF)     enabled
 *   [1] 400.0 - 520.0 MHz  (70cm UHF)   enabled
 *   [2] 220.0 - 260.0 MHz  (1.25m)      enabled
 *   [3] 350.0 - 390.0 MHz  (300 MHz)    disabled
 */
typedef struct {
    uint8_t  enabled;         /* Non-zero = TX permitted in this sub-band */
    uint16_t low_01mhz;       /* Lower edge in 0.1 MHz units */
    uint16_t high_01mhz;      /* Upper edge in 0.1 MHz units */
    uint32_t low_hz;           /* Lower edge in Hz */
    uint32_t high_hz;          /* Upper edge in Hz */
} cal_subband_t;

/* Calibration data structure (loaded from SPI flash 0xF000) */
typedef struct {
    /* TX power calibration: 12 bands x 16 bytes per band */
    uint8_t tx_power[12][16];          /* 0xF000-0xF0BF */

    /* Validity flag */
    uint8_t validity_flag;              /* 0xF0E0: non-0xFF = valid */

    /* Decoded TX sub-band edges (from 0xF230 and 0xF240) */
    cal_subband_t subbands[CAL_NUM_SUBBANDS];
} calibration_t;

/* Load calibration from SPI flash. Returns 0 on success, -1 if invalid */
int calibration_load(calibration_t *cal);

/* Get TX power register value for given band index and power level */
uint16_t calibration_get_tx_power(const calibration_t *cal,
                                  uint8_t band, uint8_t level);

/* Check if freq (Hz) is within any enabled calibrated TX sub-band.
 * Returns 1 if TX is permitted, 0 if outside all band limits. */
int calibration_tx_allowed(const calibration_t *cal, uint32_t freq_hz);

/* Global calibration data (defined in main.c, loaded once at boot) */
extern calibration_t cal_data;

#endif /* DRIVERS_CALIBRATION_H */
