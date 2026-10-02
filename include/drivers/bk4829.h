/*
 * bk4829.h - BK4829 RF transceiver driver for the RT-950 Pro
 *
 * Two BK4829 chips on a shared bit-bang SPI bus (GPIOE):
 *   PE10 = SCK,  PE11 = SDA (bidirectional)
 *   PE8  = SEN1 (chip 0 select)
 *   PE15 = SEN2 (chip 1 select)
 *
 * SPI protocol (24-bit frame):
 *   [7:  R/W] [6:0 register address] [15:0 data]
 *   R/W = 1 for write, 0 for read
 *
 * See also: docs/bk4829.md for full register map and app notes.
 *
 * V0.27 verified addresses:
 *   Init sequence     @ 0x08007144 (61 register operations)
 *   SPI write (chip0) @ 0x0801C090
 *   SPI read  (chip0) @ 0x0801BFC4
 *   SPI write (chip1) @ 0x0801C0E4
 *   SPI read  (chip1) @ 0x0801B908
 *   set_frequency     @ 0x0801BD40
 *   set_mode (REG_47) @ 0x0801BC2C
 *   mode_set (REG_30) @ 0x0801B9CC
 *   set_tx_power      @ 0x0801BD64
 *   REG_47 LUT        @ 0x0802B80C (4 x uint16)
 *   TX power table    @ 0x0802B724 (3 x uint16)
 *   CTCSS tone table  @ 0x08028C36 (50 x uint16)
 *   DCS code table    @ 0x08028B2A (104 x uint16)
 *   Squelch function  @ 0x0801BA4C (freq-dependent calibration)
 *   PLL settle delay  @ 0x0800A962
 */

#ifndef DRIVERS_BK4829_H
#define DRIVERS_BK4829_H

#include <stdint.h>

/* Chip index constants */
#define BK4829_CHIP0    0   /* SEN1 = PE8 */
#define BK4829_CHIP1    1   /* SEN2 = PE15 */

/* ---- Key BK4829 register addresses (7-bit, 0x00-0x7F) ---- */
#define BK4829_REG_00   0x00    /* Power / chip reset */
#define BK4829_REG_01   0x01    /* GPIO config */
#define BK4829_REG_07   0x07    /* Frequency low word */
#define BK4829_REG_08   0x08    /* Frequency high word (PLL integer/frac) */
#define BK4829_REG_09   0x09    /* Calibration register (step in [15:12]) */
#define BK4829_REG_0C   0x0C    /* RSSI readback */
#define BK4829_REG_10   0x10    /* PLL config */
#define BK4829_REG_13   0x13    /* LO config */
#define BK4829_REG_19   0x19    /* Unknown config */
#define BK4829_REG_1C   0x1C    /* IF filter bandwidth */
#define BK4829_REG_25   0x25    /* Deviation config */
#define BK4829_REG_28   0x28    /* RSSI threshold */
#define BK4829_REG_30   0x30    /* General enable */
#define BK4829_REG_37   0x37    /* RF config */
#define BK4829_REG_40   0x40    /* FSK mode / read-modify-write target */
#define BK4829_REG_44   0x44    /* AGC control 1 */
#define BK4829_REG_45   0x45    /* AGC control 2 */
#define BK4829_REG_46   0x46    /* IF bandwidth / detection */
#define BK4829_REG_47   0x47    /* RF AGC - mode-dependent, see set_mode() */
#define BK4829_REG_48   0x48    /* RF gain / calibration - see set_mode() */
#define BK4829_REG_49   0x49    /* AGC (fixed 0x2AB2) */
#define BK4829_REG_4A   0x4A    /* AGC timing (fixed 0x5430) */
#define BK4829_REG_4D   0x4D    /* Squelch glitch close threshold */
#define BK4829_REG_4E   0x4E    /* Squelch glitch open threshold */
#define BK4829_REG_4F   0x4F    /* Squelch noise threshold */
#define BK4829_REG_51   0x51    /* TX tone control (RF_Set_Frequency) */
#define BK4829_REG_70   0x70    /* Demodulator config (AM/FM select) */
#define BK4829_REG_71   0x71    /* CTCSS/CDCSS frequency */
#define BK4829_REG_74   0x74    /* CTCSS TX config */
#define BK4829_REG_75   0x75    /* CTCSS detect config */
#define BK4829_REG_7D   0x7D    /* MIC sensitivity / AGC */
#define BK4829_REG_7E   0x7E    /* IF/AGC config */

/* ---- RF operating modes (V0.27 verified @ 0x0801B9CC, 0x0801BC2C) ----
 *
 *  REG_30 per mode (verified @ 0x0801B9CC):
 *    STANDBY=0x0000  RX=0xBFF1  TX=0xC1FE  AM_RX=0x0302  SCAN=0xC3FA
 *
 *  REG_47 base 0x6042 | lut[mode] (verified @ 0x0801BC2C, LUT @ 0x0802B80C):
 *    [0]=0xDB27  [1]=0xBB94  [2]=0x6616  [3]=0xEB4F
 *
 *  REG_48 (verified @ 0x0801BC60):
 *    FM: 0xB00F | (cal_value[5:0] << 4)   AM: 0xB0A3 (fixed)
 */
typedef enum {
    BK4829_MODE_STANDBY  = 0,   /* All off (REG_30 = 0x0000) */
    BK4829_MODE_RX       = 1,   /* FM receive (REG_30 = 0xBFF1) */
    BK4829_MODE_TX       = 2,   /* FM transmit (REG_30 = 0xC1FE) */
    BK4829_MODE_AM_RX    = 3,   /* AM receive (REG_30 = 0x0302, REG_70 modified) */
    BK4829_MODE_SCAN     = 4,   /* Scan/APRS (REG_30 = 0xC3FA) */
} bk4829_mode_t;

/* ---- Register init table entry ---- */
typedef struct {
    uint8_t  reg;
    uint16_t value;
} bk4829_reg_init_t;

/* ---- API ---- */

/* Low-level register access */
void     bk4829_write_reg(uint8_t chip, uint8_t reg, uint16_t val);
uint16_t bk4829_read_reg(uint8_t chip, uint8_t reg);

/* Full chip initialization (all 5 phases + calibration) */
void bk4829_init(uint8_t chip);

/* Set VCO frequency in Hz */
void bk4829_set_frequency(uint8_t chip, uint32_t freq_hz);

/* Read RSSI from register 0x0C */
uint16_t bk4829_read_rssi(uint8_t chip);

/* Enable/disable TX or RX path via register 0x30 */
void bk4829_enable_rx(uint8_t chip);
void bk4829_enable_tx(uint8_t chip);
void bk4829_standby(uint8_t chip);
void bk4829_audio_filter_config(uint8_t chip);
void bk4829_set_af_beep(uint8_t chip);
void bk4829_set_af_rx(uint8_t chip);

/*
 * Set RF operating mode - configures REG_30 + REG_47 + REG_48.
 *
 * V0.27 analysis: the BK4829 does NOT use per-frequency-band register tables
 * per-band tables. Instead, REG_47/48 are set per operating MODE:
 *   - FM RX/TX: REG_47 from lookup table, REG_48 from calibration
 *   - AM RX:    fixed REG_47 and REG_48, plus REG_70 AM demod config
 *   - REG_49 (0x2AB2) and REG_4A (0x5430) are fixed at init, never changed
 *
 * cal_value: 6-bit calibration nibble from flash (0x00F000 area).
 *            Applied as REG_48 = 0xB00F | (cal_value << 4).
 *            Pass 0 if calibration not yet loaded.
 */
void bk4829_set_mode(uint8_t chip, bk4829_mode_t mode, uint8_t cal_value);

/*
 * Set TX power via REG_36.
 * V0.27 @ 0x0801BD64: 3 fixed levels from table @ 0x0802B724:
 *   level 0 (low):  0x8E79
 *   level 1 (mid):  0xA498
 *   level 2 (high): 0xF016
 *
 * OEM divides raw power_level by 3 (udiv) to collapse into 3 bins,
 * then indexes the table.  The primary path @ 0x0800B700 uses PA
 * bias ramping; we use the simpler direct-write path.
 */
#define BK4829_REG_36   0x36    /* PA bias / TX power control */
void bk4829_set_tx_power(uint8_t chip, uint8_t level);

/* ---- Squelch threshold register ---- */
#define BK4829_REG_78   0x78    /* Squelch threshold (open/close) */

/* ---- CTCSS tone table ---- */
#define CTCSS_TONE_COUNT 50
extern const uint16_t ctcss_tone_table[CTCSS_TONE_COUNT];

/* CTCSS API */
void    bk4829_set_ctcss_tx(uint8_t chip, uint8_t tone_index);
void    bk4829_set_ctcss_rx(uint8_t chip, uint8_t tone_index);
void    bk4829_disable_ctcss(uint8_t chip);
uint8_t bk4829_ctcss_detected(uint8_t chip);

/*
 * STE (Squelch Tail Elimination) - send 134.4 Hz tail tone.
 *
 * Called just before dropping carrier when CTCSS or DCS is active.
 * The receiving radio detects the 134.4 Hz sub-tone and mutes its
 * speaker before the carrier disappears, preventing the squelch tail
 * noise burst.  Caller must delay ~55 ms after this before cutting TX.
 */
void bk4829_send_tail_tone(uint8_t chip);

/* ---- DCS code table ---- */
#define DCS_CODE_COUNT 104
extern const uint16_t dcs_code_table[DCS_CODE_COUNT];

/* DCS API - polarity: 0 = normal, 1 = inverted */
void    bk4829_set_dcs_tx(uint8_t chip, uint8_t code_index, uint8_t polarity);
void    bk4829_set_dcs_rx(uint8_t chip, uint8_t code_index, uint8_t polarity);
void    bk4829_disable_dcs(uint8_t chip);
uint8_t bk4829_dcs_detected(uint8_t chip);

/* ---- Squelch control ---- */
void    bk4829_set_squelch(uint8_t chip, uint8_t level);   /* 0-9, 0=open */
uint8_t bk4829_is_squelch_open(uint8_t chip);

/* ---- Voice scrambler (frequency inversion) ---- */
void bk4829_scrambler_enable(uint8_t chip);
void bk4829_scrambler_disable(uint8_t chip);

#endif /* DRIVERS_BK4829_H */
