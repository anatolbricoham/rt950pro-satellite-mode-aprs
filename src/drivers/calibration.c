/*
 * calibration.c - Calibration data loader for the RT-950 Pro
 *
 * V0.27 OEM calibration loader: 0x0800F1C8 - 0x0800F586
 * V0.27 OEM flash_read (called from loader): 0x08021D7C
 *
 * Reads factory calibration from SPI flash at 0x00F000:
 *   0xF000-0xF0BF: 12 x 16-byte TX power calibration blocks
 *   0xF0E0:        Validity flag (!=0xFF means programmed)
 *   0xF230-0xF23F: VHF sub-band edges (16 bytes: 3 sub-bands, BCD)
 *   0xF240-0xF247: UHF sub-band edges (8 bytes: 1 sub-band, BCD)
 *
 * OEM also reads 0xF0C0(10B), 0xF0D0(10B), 0xF0E0-0xF1B0(many blocks),
 * 0xF210(16B) for additional calibration data not yet parsed here.
 *
 * BCD encoding (V0.27 @ 0x0800F3F6): each flash byte = 2 BCD digits.
 *   Decode: hi_nibble * 10 + lo_nibble -> decimal 0-99.
 *   Frequency pair: decoded_byte1 * 1000 + decoded_byte2 * 10
 *     -> value in 0.1 MHz units (e.g., 1360 = 136.0 MHz).
 *   Word value: halfword * 10000 -> 10 Hz units.
 *
 * The calibration sector is fixed (not wear-leveled).
 */

#include "drivers/calibration.h"
#include "drivers/flash_layout.h"
#include "drivers/spi.h"

#include <string.h>

/* Flash offsets relative to FLASH_ADDR_CALIBRATION (0x00F000) */
#define CAL_TX_POWER_OFFSET     0x000   /* 12 x 16 = 192 bytes */
#define CAL_TX_POWER_SIZE       192
#define CAL_VALIDITY_OFFSET     0x0E0   /* Single flag byte */
#define CAL_VHF_EDGE_OFFSET     0x230   /* 16 bytes (3 sub-bands, BCD) */
#define CAL_VHF_EDGE_SIZE       16
#define CAL_UHF_EDGE_OFFSET     0x240   /* 8 bytes (1 sub-band, BCD) */
#define CAL_UHF_EDGE_SIZE       8

/*
 * Decode a single BCD byte to its decimal value (0-99).
 * OEM algorithm @ 0x0800F3FC:
 *   lo = byte & 0xF
 *   hi = byte >> 4
 *   result = hi*10 + lo  (computed as hi + hi*4 = hi*5, then lo + hi*5*2)
 */
static uint8_t bcd_decode_byte(uint8_t bcd)
{
    return ((bcd >> 4) & 0xF) * 10 + (bcd & 0xF);
}

/*
 * Decode a 2-byte BCD frequency pair to 0.1 MHz units.
 * OEM algorithm @ 0x0800F416:
 *   freq = decoded_hi * 1000 + decoded_lo * 10
 *   e.g., BCD [0x01, 0x36] -> [1, 36] -> 1*1000 + 36*10 = 1360 = 136.0 MHz
 */
static uint16_t bcd_pair_to_01mhz(uint8_t hi_bcd, uint8_t lo_bcd)
{
    uint16_t hi = bcd_decode_byte(hi_bcd);
    uint16_t lo = bcd_decode_byte(lo_bcd);
    return hi * 1000 + lo * 10;
}

/*
 * Parse sub-band edges from a raw BCD block.
 * Each sub-band is 5 bytes: [enable_flag, low_hi, low_lo, high_hi, high_lo]
 * OEM stores 3 sub-bands in the VHF block (15 bytes + 1 pad) and
 * 1 sub-band in the UHF block (5 bytes + 3 pad).
 */
static void decode_subband(cal_subband_t *sb, const uint8_t *raw)
{
    sb->enabled    = raw[0];
    sb->low_01mhz  = bcd_pair_to_01mhz(raw[1], raw[2]);
    sb->high_01mhz = bcd_pair_to_01mhz(raw[3], raw[4]);
    /* OEM multiplies halfword by 10000 to get 10 Hz units (@ 0x08012450),
     * then the value * 10 = Hz. We store Hz directly. */
    sb->low_hz     = (uint32_t)sb->low_01mhz  * 100000u;
    sb->high_hz    = (uint32_t)sb->high_01mhz * 100000u;
}

/*
 * Default sub-band edges (OEM hardcoded fallback @ 0x0800F346).
 * Used when flash band edge data has first byte == 0xFF (unprogrammed).
 */
static const uint8_t default_vhf_raw[16] = {
    0x01, 0x01, 0x36, 0x01, 0x74,   /* Sub-band 0: enabled, 136.0-174.0 MHz */
    0x01, 0x04, 0x00, 0x05, 0x20,   /* Sub-band 1: enabled, 400.0-520.0 MHz */
    0x01, 0x02, 0x20, 0x02, 0x60,   /* Sub-band 2: enabled, 220.0-260.0 MHz */
    0x00                             /* padding */
};

static const uint8_t default_uhf_raw[8] = {
    0x00, 0x03, 0x50, 0x03, 0x90,   /* Sub-band 3: disabled, 350.0-390.0 MHz */
    0x00, 0x00, 0x00                 /* padding */
};

/* V0.27 OEM function @ 0x0800F1C8 */
int calibration_load(calibration_t *cal)
{
    uint8_t vhf_raw[CAL_VHF_EDGE_SIZE];
    uint8_t uhf_raw[CAL_UHF_EDGE_SIZE];

    if (!cal)
        return -1;

    memset(cal, 0, sizeof(*cal));

    /* Read validity flag at offset 0x0E0 (OEM reads this first) */
    spi_flash_read(FLASH_ADDR_CALIBRATION + CAL_VALIDITY_OFFSET,
                   &cal->validity_flag, 1);

    if (cal->validity_flag == 0xFF)
        return -1;

    /* Read TX power blocks: 12 bands x 16 bytes at offset 0x000 */
    spi_flash_read(FLASH_ADDR_CALIBRATION + CAL_TX_POWER_OFFSET,
                   (uint8_t *)cal->tx_power, CAL_TX_POWER_SIZE);

    /* Read VHF sub-band edges at offset 0x230 (16 bytes) */
    spi_flash_read(FLASH_ADDR_CALIBRATION + CAL_VHF_EDGE_OFFSET,
                   vhf_raw, CAL_VHF_EDGE_SIZE);

    /* Read UHF sub-band edge at offset 0x240 (8 bytes) */
    spi_flash_read(FLASH_ADDR_CALIBRATION + CAL_UHF_EDGE_OFFSET,
                   uhf_raw, CAL_UHF_EDGE_SIZE);

    /* OEM checks first byte of VHF block for 0xFF (unprogrammed) and
     * falls back to hardcoded defaults if so (@ 0x0800F32C) */
    if (vhf_raw[0] == 0xFF) {
        memcpy(vhf_raw, default_vhf_raw, sizeof(default_vhf_raw));
        memcpy(uhf_raw, default_uhf_raw, sizeof(default_uhf_raw));
    }

    /* Decode 3 VHF sub-bands (5 bytes each at offsets 0, 5, 10) */
    decode_subband(&cal->subbands[0], &vhf_raw[0]);
    decode_subband(&cal->subbands[1], &vhf_raw[5]);
    decode_subband(&cal->subbands[2], &vhf_raw[10]);

    /* Decode 1 UHF sub-band */
    decode_subband(&cal->subbands[3], &uhf_raw[0]);

    return 0;
}

/* V0.27: TX power lookup not yet traced to specific OEM function */
uint16_t calibration_get_tx_power(const calibration_t *cal,
                                  uint8_t band, uint8_t level)
{
    if (!cal || band >= 12 || level >= 3)
        return 0;

    /*
     * Each 16-byte block contains power data for one band.
     * Power levels are stored as pairs of bytes (low byte, high byte)
     * at offsets 0, 2, 4 within the block for Low/Mid/High.
     */
    uint8_t lo = cal->tx_power[band][level * 2];
    uint8_t hi = cal->tx_power[band][level * 2 + 1];
    return ((uint16_t)hi << 8) | lo;
}

/* V0.27 OEM band check uses decoded sub-band Hz values (@ 0x0800F3BA+) */
int calibration_tx_allowed(const calibration_t *cal, uint32_t freq_hz)
{
    if (!cal || cal->validity_flag == 0xFF)
        return 1; /* no cal data - allow (defer to hardcoded limits) */

    for (uint8_t i = 0; i < CAL_NUM_SUBBANDS; i++) {
        if (!cal->subbands[i].enabled)
            continue;
        if (freq_hz >= cal->subbands[i].low_hz &&
            freq_hz <= cal->subbands[i].high_hz)
            return 1;
    }

    return 0;
}
