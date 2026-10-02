/*
 * bk4829.c - BK4829 RF transceiver driver for the RT-950 Pro
 *
 * SPI protocol (24-bit frame, MSB first):
 *   Byte 0: [7] always 1  [6:0] register address
 *   Byte 1: data[15:8]
 *   Byte 2: data[7:0]
 *   Direction determined by SDA pin state after cmd byte (not bit 7).
 *
 * BOTH chips share a bit-bang SPI bus on GPIOE:
 *   PE10 = SCK,  PE11 = SDA (bidirectional)
 *   PE8  = SEN1 (chip 0 select),  PE15 = SEN2 (chip 1 select)
 *
 * Verified from V0.27 binary (init @ 0x08007144, SPI @ 0x0801C090).
 * The firmware uses GPIOE (0x40011800) for all BK4829 communication --
 * confirmed by checking register writes in the vtable functions and the
 * "band relay" shift-in function which is actually BK4829 SPI.
 *
 * Init register table extracted from firmware: all 61 register operations.
 */

#include "drivers/bk4829.h"
#include "drivers/gpio.h"
#include "rt950_pinmap.h"
#include "at32f403a.h"

/* ========================================================================
 *  Bit-bang SPI helpers - shared bus on GPIOE (PE8/PE10/PE11/PE15)
 *  V0.27: Chip0_Write @ 0x0801C090, Chip0_Read @ 0x0801BFC4
 *         Chip1_Write @ 0x0801C0E4, Chip1_Read @ 0x0801B908
 *         SPI_SendByte @ 0x0801B260
 * ======================================================================== */

static void bb_delay(void)
{
    /*
     * OEM delay_short(5) @ 0x0800A962:
     *   rsb r0, r0, r0, lsl #5   ; r0 = 5*31 = 155
     *   loop: subs r0,#1 / uxth r0 / bcs loop
     *   155 iterations × 4 cycles = 620 cycles ≈ 5.17 µs at 120 MHz.
     *
     * Our original 24 NOPs ≈ 200 ns was 26× too fast for the BK4829.
     * Match OEM timing exactly to ensure reliable SPI communication.
     */
    __asm volatile (
        "movw r0, #155\n"
        "1: subs r0, r0, #1\n"
        "   bne 1b\n"
        ::: "r0", "cc"
    );
}

/*
 * VCO/PLL settling delay - V0.27 @ 0x0800A962.
 *
 * OEM delay(n): computes n*31, then tight loop that many iterations.
 * Called with n=0x32 (50) for VCO settling: 50*31 = 1550 ASM iterations.
 * At 120 MHz, ~3 cycles/iteration = ~4650 cycles = ~39 us.
 */
static void pll_settle_delay(void)
{
    /*
     * OEM: delay(0x32) = delay(50) @ 0x0800A962.
     * Computes 50*31 = 1550 iterations, 4 cycles each = 6200 cycles ≈ 52 µs.
     */
    __asm volatile (
        "movw r0, #1550\n"
        "1: subs r0, r0, #1\n"
        "   bne 1b\n"
        ::: "r0", "cc"
    );
}

static void bb_cs_low(uint8_t chip)
{
    if (chip == BK4829_CHIP0)
        gpio_clear_pin(BK4829_SEN1_PORT, BK4829_SEN1_PIN);
    else
        gpio_clear_pin(BK4829_SEN2_PORT, BK4829_SEN2_PIN);
    bb_delay();
}

static void bb_cs_high(uint8_t chip)
{
    bb_delay();
    if (chip == BK4829_CHIP0)
        gpio_set_pin(BK4829_SEN1_PORT, BK4829_SEN1_PIN);
    else
        gpio_set_pin(BK4829_SEN2_PORT, BK4829_SEN2_PIN);
}

static void bb_clk_low(void)
{
    gpio_clear_pin(BK4829_SCK_PORT, BK4829_SCK_PIN);
}

static void bb_clk_high(void)
{
    gpio_set_pin(BK4829_SCK_PORT, BK4829_SCK_PIN);
}

static void bb_sda_set(uint8_t bit)
{
    if (bit)
        gpio_set_pin(BK4829_SDA_PORT, BK4829_SDA_PIN);
    else
        gpio_clear_pin(BK4829_SDA_PORT, BK4829_SDA_PIN);
}

static uint8_t bb_sda_read(void)
{
    return gpio_read_pin(BK4829_SDA_PORT, BK4829_SDA_PIN) ? 1 : 0;
}

/* Shift out 'nbits' from MSB of 'data' via bit-bang SPI (CPOL=0, CPHA=0) */
static void bb_shift_out(uint32_t data, uint8_t nbits)
{
    for (int i = nbits - 1; i >= 0; i--) {
        bb_clk_low();
        bb_sda_set((data >> i) & 1);
        bb_delay();
        bb_clk_high();
        bb_delay();
    }
    bb_clk_low();
}

/* Shift in 'nbits' via bit-bang SPI (CPOL=0, CPHA=0, reads on rising edge) */
static uint16_t bb_shift_in(uint8_t nbits)
{
    uint16_t val = 0;
    /* Switch SDA pin to input for reading */
    gpio_config_pin(BK4829_SDA_PORT, BK4829_SDA_PIN,
                    GPIO_MODE_INPUT, GPIO_CNF_FLOATING);

    for (int i = nbits - 1; i >= 0; i--) {
        bb_clk_low();
        bb_delay();
        bb_clk_high();
        bb_delay();
        val |= (uint16_t)(bb_sda_read() << i);
    }
    bb_clk_low();

    /* Restore SDA to output */
    gpio_config_pin(BK4829_SDA_PORT, BK4829_SDA_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    return val;
}

/* ========================================================================
 *  Low-level register access
 * ======================================================================== */

void bk4829_write_reg(uint8_t chip, uint8_t reg, uint16_t val)
{
    if (chip > BK4829_CHIP1) return;      /* bounds check */
    /*
     * BK4829 3-wire SPI: bit 7 = 0 for WRITE, bit 7 = 1 for READ
     * (opposite of BK4819! confirmed by OEM ORR #0x80 in read function
     *  and by 0xFFFF readback when we tried bit7=0 for reads)
     */
    uint8_t cmd = reg & 0x7F;            /* bit 7 = 0 for WRITE */

    bb_cs_low(chip);
    bb_shift_out((uint32_t)cmd << 16 | val, 24);
    bb_cs_high(chip);
}

uint16_t bk4829_read_reg(uint8_t chip, uint8_t reg)
{
    if (chip > BK4829_CHIP1) return 0;    /* bounds check */
    uint8_t cmd = 0x80 | (reg & 0x7F);   /* bit 7 = 1 for READ */
    uint16_t val;

    bb_cs_low(chip);
    bb_shift_out(cmd, 8);
    val = bb_shift_in(16);
    bb_cs_high(chip);

    return val;
}

/* ========================================================================
 *  Init register table (V0.27 @ 0x08007144)
 * ======================================================================== */

/* Phase 1: 36 core register writes - OEM bk4829_hal_init @ 0x08007144 */
static const bk4829_reg_init_t init_phase1[] = {
    { 0x00, 0x8000 },  /* Chip reset */
    { 0x00, 0x0000 },  /* Release reset */
    { 0x37, 0x9D1F },  /* CTCSS/CDCSS control */
    { 0x13, 0x03DF },  /* RX comparator threshold */
    { 0x12, 0x03DB },  /* Comparator control */
    { 0x11, 0x033A },  /* AGC table index */
    { 0x10, 0x0318 },  /* AGC config */
    { 0x14, 0x0210 },  /* AGC gain */
    { 0x49, 0x2AB2 },  /* RF gain config */
    { 0x7B, 0x73DC },  /* Internal calibration */
    { 0x1C, 0x0F80 },  /* AF gain DAC */
    { 0x1D, 0xE555 },  /* RSSI compensation */
    { 0x1E, 0x4C58 },  /* IF filter bandwidth */
    { 0x1F, 0x865A },  /* IF frequency */
    { 0x3E, 0x94C6 },  /* Band select / VCO */
    { 0x3F, 0x07FE },  /* Band control */
    { 0x25, 0xC1BA },  /* TX audio path */
    { 0x3A, 0x9A7C },  /* Modulation config */
    { 0x19, 0x1041 },  /* Deviation setting */
    { 0x28, 0x0B40 },  /* TX power bias (low) */
    { 0x29, 0xAA00 },  /* TX power bias (mid) */
    { 0x2A, 0xCC00 },  /* TX power bias (high) */
    { 0x2C, 0x0022 },  /* TX control */
    { 0x2F, 0x9890 },  /* Squelch mode / RX AGC */
    { 0x53, 0x2028 },  /* Internal config */
    { 0x7E, 0x303E },  /* Calibration */
    { 0x46, 0x6050 },  /* Audio filter */
    { 0x4A, 0x5430 },  /* AGC table */
    { 0x48, 0xB3BF },  /* AGC config */
    { 0x49, 0x2AB2 },  /* RF gain (2nd write) */
    { 0x4A, 0x5430 },  /* AGC table (2nd write) */
    { 0x4D, 0xA004 },  /* RSSI squelch threshold */
    { 0x4E, 0x3815 },  /* Noise squelch threshold */
    { 0x4F, 0x3F3B },  /* Combined squelch threshold */
    { 0x77, 0xCCEF },  /* Internal */
    { 0x7E, 0x303E },  /* Calibration (2nd write) */
};

/* Phase 3: Post read-modify-write - OEM @ 0x080072B4 */
static const bk4829_reg_init_t init_phase3[] = {
    { 0x7D, 0xE912 },  /* Calibration */
    { 0x48, 0xB3FF },  /* AGC config (updated from 0xB3BF) */
};

/* Phase 4 calibration (reg 0x09 loop) was present in V0.18 but removed by V0.27.
 * Not emitted in the V0.27 init sequence - confirmed by binary analysis. */

/* Phase 5: Final registers - OEM @ 0x080072DE */
static const bk4829_reg_init_t init_phase5[] = {
    { 0x74, 0xDF22 },  /* Internal */
    { 0x44, 0x91C1 },  /* Dual-tone detector A */
    { 0x45, 0x3040 },  /* Dual-tone detector B */
    { 0x75, 0xE61C },  /* Internal */
    { 0x54, 0x91C1 },  /* Dual-tone detector A2 */
    { 0x55, 0x3040 },  /* Dual-tone detector B2 */
};

/* ========================================================================
 *  bk4829_init - Full 5-phase initialization sequence
 * ======================================================================== */

void bk4829_init(uint8_t chip)
{
    unsigned i;

    /* Phase 1: Core register init (36 writes) */
    for (i = 0; i < sizeof(init_phase1) / sizeof(init_phase1[0]); i++) {
        bk4829_write_reg(chip, init_phase1[i].reg, init_phase1[i].value);
    }

    /* Phase 2: Read-modify-write of register 0x40
     * reg40 = (read(0x40) & 0xF000) | 0x04D2 */
    {
        uint16_t reg40 = bk4829_read_reg(chip, 0x40);
        bk4829_write_reg(chip, 0x40, (reg40 & 0xF000) | 0x04D2);
    }

    /* Phase 3: Post-RMW writes */
    for (i = 0; i < sizeof(init_phase3) / sizeof(init_phase3[0]); i++) {
        bk4829_write_reg(chip, init_phase3[i].reg, init_phase3[i].value);
    }

    /* Phase 4: Removed in V0.27 (reg 0x09 cal loop not present in binary) */

    /* Phase 5: Final registers */
    for (i = 0; i < sizeof(init_phase5) / sizeof(init_phase5[0]); i++) {
        bk4829_write_reg(chip, init_phase5[i].reg, init_phase5[i].value);
    }

    /* Phase 6: AF path configuration (OEM tail-call @ 0x0800731C with mode=0)
     *
     * The OEM bk4829_hal_init ends with a tail-call to 0x0801BC2C(mode=0)
     * which writes REG 0x47 (AF source/gain) and REG 0x48 (AF output gain).
     * Without this, the BK4829 speaker path is misconfigured.
     *
     * Mode 0 LUT: REG_47 = 0x6042 | 0xDB27 = 0xFB67
     * REG_48: OEM uses calibration-based value; init table already set 0xB3FF */
    bk4829_write_reg(chip, 0x47, 0xFB67);
}

/* ========================================================================
 *  bk4829_set_frequency - Program VCO frequency
 *
 *  V0.27 @ 0x0801BD40:
 *    REG_38 = freq_hz[15:0], REG_39 = freq_hz[31:16]
 *    Raw Hz, no scaling.
 *    OEM tail-calls mode_set(1) @ 0x0801B9CC which writes:
 *      REG_30 = 0x0200 (VCO cal only), delay(50) @ 0x0800A962,
 *      then REG_30 = 0xBFF1 (RX enable).
 * ======================================================================== */

void bk4829_set_frequency(uint8_t chip, uint32_t freq_hz)
{
    bk4829_write_reg(chip, 0x38, (uint16_t)(freq_hz & 0xFFFF));
    bk4829_write_reg(chip, 0x39, (uint16_t)(freq_hz >> 16));
    /* V0.27 tail-calls mode_set(1) @ 0x0801B9CC for VCO calibration */
    bk4829_write_reg(chip, 0x30, 0x0200);
    pll_settle_delay();
    bk4829_write_reg(chip, 0x30, 0xBFF1);
}

/* ========================================================================
 *  bk4829_read_rssi - Read RSSI value from register 0x0C
 *
 *  V0.27 @ 0x0801BEC4: reads REG_0C, then extracts bits based on
 *  chip type and modulation mode (bit 14 for FM, bit 15 for AM/type3,
 *  bits 11:10 for default). We return raw value; callers extract.
 * ======================================================================== */

uint16_t bk4829_read_rssi(uint8_t chip)
{
    return bk4829_read_reg(chip, BK4829_REG_0C);
}

/* ========================================================================
 *  RX/TX/Standby control via register 0x30 - V0.27 @ 0x080079CC
 *
 *  BK4829 register 0x30 bit assignments (from community RE):
 *    [15]   = PA enable
 *    [14]   = MIC ADC enable
 *    [13]   = TX DSP enable
 *    [10]   = RX enable
 *    [9]    = VCO calibrate
 *    [5]    = PLL enable
 *    [3]    = Band select
 *    [2]    = IF chip enable
 *    [1]    = XTAL enable
 *    [0]    = Band gap enable
 * ======================================================================== */

#define REG30_XTAL_EN     (1U << 1)
#define REG30_BANDGAP_EN  (1U << 0)
#define REG30_PLL_EN      (1U << 5)
#define REG30_VCO_CAL     (1U << 9)
#define REG30_RX_EN       (1U << 10)
#define REG30_TX_DSP_EN   (1U << 13)
#define REG30_MIC_ADC_EN  (1U << 14)
#define REG30_PA_EN       (1U << 15)

/* Common base: bandgap + XTAL + PLL always on */
#define REG30_BASE  (REG30_BANDGAP_EN | REG30_XTAL_EN | REG30_PLL_EN)

void bk4829_enable_rx(uint8_t chip)
{
    bk4829_write_reg(chip, 0x30, 0xBFF1);
}

void bk4829_enable_tx(uint8_t chip)
{
    bk4829_write_reg(chip, 0x30, 0xC1FE);
}

void bk4829_standby(uint8_t chip)
{
    bk4829_write_reg(chip, 0x30, 0x0000);
}

/* ========================================================================
 *  bk4829_audio_filter_config - Configure audio processing registers
 *
 *  OEM audio_route_setup @ 0x080067A4 calls bk4829_audio_filter_config
 *  @ 0x0801B4D0 before every beep/audio playback.  These registers
 *  configure the internal AF path (filters, IF sync, AGC) needed for
 *  audio to pass through the chip to AF_OUT.
 *
 *  Without these writes, the BK4829 AF processing chain is unconfigured
 *  and audio from the DAC (routed through BEEP_SW/PC12 to the chip's
 *  analog input) will NOT reach AF_OUT → amplifier → speaker.
 * ======================================================================== */

void bk4829_audio_filter_config(uint8_t chip)
{
    bk4829_write_reg(chip, 0x58, 0x3FC3);  /* Audio filter 1 */
    bk4829_write_reg(chip, 0x72, 0x3065);  /* Audio deviation */
    bk4829_write_reg(chip, 0x70, 0x00EC);  /* Audio processing */
    bk4829_write_reg(chip, 0x5D, 0x1B00);  /* AGC config */
    bk4829_write_reg(chip, 0x59, 0x6028);  /* IF sync (request) */
    bk4829_write_reg(chip, 0x59, 0x3028);  /* IF sync (clear) */
    bk4829_write_reg(chip, 0x5A, 0xFB72);  /* Filter aux 1 */
    bk4829_write_reg(chip, 0x5B, 0x4099);  /* Filter aux 2 */
    bk4829_write_reg(chip, 0x5C, 0xA730);  /* Filter aux 3 */
    bk4829_write_reg(chip, 0x3F, 0x3000);  /* Band/BW control */
}

/* ========================================================================
 *  bk4829_set_af_beep / bk4829_set_af_rx - AF path switching
 *
 *  OEM @ 0x0801BC2C: REG_47 + REG_48 setter, mode-indexed LUT.
 *
 *  For BEEP mode (mode=3): REG_47=0xEB4F, REG_48=0xB0A3
 *    Routes MCU DAC (PA4) → BK4829 speaker driver via analog input
 *
 *  For FM RX mode (mode=0): REG_47=0xFB67, REG_48=cal-based
 *    Routes demodulated RF → BK4829 speaker driver
 * ======================================================================== */

void bk4829_set_af_beep(uint8_t chip)
{
    bk4829_write_reg(chip, 0x47, 0xEB4F);
    bk4829_write_reg(chip, 0x48, 0xB0A3);
}

void bk4829_set_af_rx(uint8_t chip)
{
    bk4829_write_reg(chip, 0x47, 0xFB67);
    bk4829_write_reg(chip, 0x48, 0xB3FF);
}

/* ========================================================================
 *  bk4829_set_mode - Full RF mode switch (REG_30 + REG_47 + REG_48)
 *
 *  V0.27 disassembly @ 0x0801BC2C + 0x0801B9CC:
 *
 *  REG_30 (enable register) per mode:
 *    STANDBY: 0x0000    RX: 0xBFF1    TX: 0xC1FE
 *    AM_RX:   0x0302    SCAN: 0xC3FA
 *
 *  REG_47 (RF AGC) per mode - lookup table at 0x0802B80C:
 *    Mode 0 (chip0): 0x6042 | lut[0]
 *    Mode 1 (chip1): 0x6042 | lut[1]
 *    Mode 3 (AM):    0x6042 | lut[3]
 *
 *  REG_48 (RF gain):
 *    Normal modes: 0xB00F | (cal_value << 4)  [6-bit from flash cal]
 *    AM/APRS:      0xB0A3 (fixed)
 *
 *  REG_70 (demodulator) for AM mode:
 *    Clear bits [11:8, 3:0], set bits [13:10] = 0xBC -> 0xBC00
 * ======================================================================== */

/* REG_47 lookup table - indexed by mode & 0xF (extracted from 0x0802B80C) */
static const uint16_t reg47_mode_lut[] = {
    0xDB27,     /* [0] FM RX chip 0 */
    0xBB94,     /* [1] FM RX chip 1 / APRS */
    0x6616,     /* [2] reserved */
    0xEB4F,     /* [3] AM RX */
};
#define REG47_BASE  0x6042

/* REG_30 values per mode */
static const uint16_t reg30_mode[] = {
    0x0000,     /* STANDBY */
    0xBFF1,     /* RX */
    0xC1FE,     /* TX */
    0x0302,     /* AM_RX */
    0xC3FA,     /* SCAN */
};

void bk4829_set_mode(uint8_t chip, bk4829_mode_t mode, uint8_t cal_value)
{
    uint8_t lut_idx = (uint8_t)mode & 0x0F;

    /* REG_30: two-step mode switch (V0.27 @ 0x080079CC)
     *   RX/TX: write 0x0200 (VCO cal only) -> delay -> final value
     *   AM/SCAN: write 0x0000 (standby) -> delay -> final value
     *   STANDBY: write 0x0000 directly */
    if (mode == BK4829_MODE_RX || mode == BK4829_MODE_TX) {
        bk4829_write_reg(chip, 0x30, 0x0200);
        pll_settle_delay();
    } else if (mode != BK4829_MODE_STANDBY) {
        bk4829_write_reg(chip, 0x30, 0x0000);
        pll_settle_delay();
    }
    if ((unsigned)mode < sizeof(reg30_mode) / sizeof(reg30_mode[0]))
        bk4829_write_reg(chip, 0x30, reg30_mode[mode]);
    else
        bk4829_write_reg(chip, 0x30, 0x0000);

    /* REG_47: RF AGC config */
    if (lut_idx < sizeof(reg47_mode_lut) / sizeof(reg47_mode_lut[0])) {
        bk4829_write_reg(chip, 0x47, REG47_BASE | reg47_mode_lut[lut_idx]);
    }

    /* REG_48: RF gain - calibration-based or fixed for AM/APRS */
    if (mode == BK4829_MODE_AM_RX) {
        bk4829_write_reg(chip, 0x48, 0xB0A3);
    } else {
        uint16_t reg48 = 0xB00F | (((uint16_t)(cal_value & 0x3F)) << 4);
        bk4829_write_reg(chip, 0x48, reg48);
    }

    /* REG_70: AM demodulator config */
    if (mode == BK4829_MODE_AM_RX) {
        uint16_t reg70 = bk4829_read_reg(chip, 0x70);
        reg70 = (reg70 & ~0x8F00U) | 0xBC00U;
        bk4829_write_reg(chip, 0x70, reg70);
    }
}

/* ========================================================================
 *  bk4829_set_tx_power - Set PA power level via REG_36
 *
 *  V0.27 disassembly @ 0x0801BD64 (direct path):
 *    3 power levels from table at 0x0802B724:
 *      Low  = 0x8E79, Mid = 0xA498, High = 0xF016
 *
 *  The OEM's primary path @ 0x0800B700 uses PA bias ramping
 *  (bias values: 0x52/0xAB/0x104) via a complex DMA-based PA ramp
 *  function. The direct path is simpler and functionally equivalent.
 * ======================================================================== */

static const uint16_t tx_power_table[3] = {
    0x8E79,     /* Low */
    0xA498,     /* Mid */
    0xF016,     /* High */
};

void bk4829_set_tx_power(uint8_t chip, uint8_t level)
{
    if (level > 2) level = 2;
    bk4829_write_reg(chip, 0x36, tx_power_table[level]);
}

/* ========================================================================
 *  CTCSS tone table - Standard 50-tone EIA/TIA-603 (freq x 10)
 *  Extracted from V0.27 firmware @ 0x08028C36
 * ======================================================================== */

const uint16_t ctcss_tone_table[CTCSS_TONE_COUNT] = {
     670,  693,  719,  744,  770,  797,  825,  854,  885,  915,
     948,  974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
    1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
    1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
    2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541,
};

/* ========================================================================
 *  DCS code table - Standard 104 codes (octal values as 9-bit words)
 *  Extracted from V0.27 firmware @ 0x08028B2A
 * ======================================================================== */

const uint16_t dcs_code_table[DCS_CODE_COUNT] = {
    0023, 0025, 0026, 0031, 0032, 0036, 0043, 0047,
    0051, 0053, 0054, 0065, 0071, 0072, 0073, 0074,
    0114, 0115, 0116, 0122, 0125, 0131, 0132, 0134,
    0143, 0145, 0152, 0155, 0156, 0162, 0165, 0172,
    0174, 0205, 0212, 0223, 0225, 0226, 0243, 0244,
    0245, 0246, 0251, 0252, 0255, 0261, 0263, 0265,
    0266, 0271, 0274, 0306, 0311, 0315, 0325, 0331,
    0332, 0343, 0346, 0351, 0356, 0364, 0365, 0371,
    0411, 0412, 0413, 0423, 0431, 0432, 0445, 0446,
    0452, 0454, 0455, 0462, 0464, 0465, 0466, 0503,
    0506, 0516, 0523, 0526, 0532, 0546, 0565, 0606,
    0612, 0624, 0627, 0631, 0632, 0654, 0662, 0664,
    0703, 0712, 0723, 0731, 0732, 0734, 0743, 0754,
};

/* ========================================================================
 *  CTCSS TX/RX programming
 *
 *  Verified from V0.27 binary at 0x0800785A:
 *    REG_51 = 0x904A (enable CTCSS, mode=CTCSS, TX gain=74)
 *    REG_07 = frequency control word: (tone_freq_x10 * 206489) / 100000
 *    REG_37 bit 15 enables sub-audio processing
 * ======================================================================== */

void bk4829_set_ctcss_tx(uint8_t chip, uint8_t tone_index)
{
    if (tone_index >= CTCSS_TONE_COUNT)
        return;
    uint16_t tone_freq = ctcss_tone_table[tone_index];
    uint16_t freq_word = (uint16_t)((uint32_t)tone_freq * 206489UL / 100000UL);
    bk4829_write_reg(chip, 0x51, 0x904A);  /* CTCSS mode, gain=74 */
    bk4829_write_reg(chip, 0x07, freq_word & 0x1FFF);  /* CTC1 freq word */
    bk4829_write_reg(chip, 0x37, 0x9F1F);
}

void bk4829_set_ctcss_rx(uint8_t chip, uint8_t tone_index)
{
    if (tone_index >= CTCSS_TONE_COUNT)
        return;
    uint16_t tone_freq = ctcss_tone_table[tone_index];
    uint16_t freq_word = (uint16_t)((uint32_t)tone_freq * 206489UL / 100000UL);
    bk4829_write_reg(chip, 0x51, 0x904A);
    bk4829_write_reg(chip, 0x07, freq_word & 0x1FFF);
}

void bk4829_disable_ctcss(uint8_t chip)
{
    bk4829_write_reg(chip, 0x51, 0x0300);  /* Disable tone; 0x0300 per V0.27 */
    /* Clear sub-audio enable (bit 15 of reg 0x37) */
    uint16_t r37 = bk4829_read_reg(chip, 0x37);
    bk4829_write_reg(chip, 0x37, r37 & ~0x8000U);
}

uint8_t bk4829_ctcss_detected(uint8_t chip)
{
    return (bk4829_read_reg(chip, 0x0C) >> 14) & 1;
}

/* ========================================================================
 *  STE (Squelch Tail Elimination) - 134.4 Hz tail tone
 *
 *  Standard STE mechanism: before dropping carrier, briefly switch the
 *  TX sub-audio tone to 134.4 Hz.  Receiving radios with CTCSS/DCS
 *  detect this "invalid" tone and mute audio before the carrier drops,
 *  preventing the squelch tail noise burst.
 *
 *  134.4 Hz frequency word = (1344 * 2065) / 1000 = 2775
 *  We use the same CTCSS TX path (REG_51 + REG_07).
 * ======================================================================== */

void bk4829_send_tail_tone(uint8_t chip)
{
    /* 134.4 Hz = 1344 tenths-of-Hz -> freq_word */
    uint16_t freq_word = (uint16_t)((uint32_t)1344U * 206489UL / 100000UL);
    bk4829_write_reg(chip, 0x51, 0x904A);           /* CTCSS mode, enable */
    bk4829_write_reg(chip, 0x07, freq_word & 0x1FFF); /* 134.4 Hz tail */
}

/* ========================================================================
 *  DCS TX/RX programming
 *
 *  V0.27: Golay encoder @ 0x08007820, bit-spread @ 0x080213B8
 *    REG_51 = 0xA033 (enable + CDCSS mode bit 13 + gain=51)
 *    REG_07 = 2775 (CDCSS bit clock)
 *    REG_08 = 23-bit Golay-encoded code word in two 12-bit writes
 * ======================================================================== */

/* Golay(23,12) generator polynomial: x^11+x^9+x^7+x^6+x^5+x+1 = 0x0AE3 */
#define GOLAY_POLY  0x0AE3

static uint32_t dcs_code_to_23bit(uint16_t code_9bit)
{
    /* Encode 12-bit data (9-bit code in lower bits) to 23-bit Golay codeword.
     * V0.27 @ 0x08007820: 23-iteration parity generation loop. */
    uint32_t data = (uint32_t)(code_9bit & 0x01FF) << 12;
    uint32_t cw = data;
    unsigned i;
    for (i = 0; i < 12; i++) {
        if (cw & (1U << 22))
            cw ^= (uint32_t)GOLAY_POLY << 11;
        cw <<= 1;
    }
    return data | ((cw >> 12) & 0x7FF);
}

void bk4829_set_dcs_tx(uint8_t chip, uint8_t code_index, uint8_t polarity)
{
    if (code_index >= DCS_CODE_COUNT)
        return;
    uint32_t code_word = dcs_code_to_23bit(dcs_code_table[code_index]);
    uint16_t config = 0xA033;  /* Enable + CDCSS mode (bit13) + gain=51 */
    if (polarity)
        config |= (1U << 14);  /* TX_CDCSS_NEGATIVE */
    bk4829_write_reg(chip, 0x51, config);
    bk4829_write_reg(chip, 0x07, 2775);  /* CDCSS bit clock */
    bk4829_write_reg(chip, 0x08, (uint16_t)(code_word & 0x0FFF));
    bk4829_write_reg(chip, 0x08, (uint16_t)(0x8000 | ((code_word >> 12) & 0x0FFF)));
    bk4829_write_reg(chip, 0x37, 0x9F1F);
}

void bk4829_set_dcs_rx(uint8_t chip, uint8_t code_index, uint8_t polarity)
{
    if (code_index >= DCS_CODE_COUNT)
        return;
    uint32_t code_word = dcs_code_to_23bit(dcs_code_table[code_index]);
    uint16_t config = 0xA033;
    if (polarity)
        config |= (1U << 14);
    bk4829_write_reg(chip, 0x51, config);
    bk4829_write_reg(chip, 0x07, 2775);
    bk4829_write_reg(chip, 0x08, (uint16_t)(code_word & 0x0FFF));
    bk4829_write_reg(chip, 0x08, (uint16_t)(0x8000 | ((code_word >> 12) & 0x0FFF)));
}

void bk4829_disable_dcs(uint8_t chip)
{
    bk4829_write_reg(chip, 0x51, 0x0000);
    uint16_t r37 = bk4829_read_reg(chip, 0x37);
    bk4829_write_reg(chip, 0x37, r37 & ~0x8000U);
}

uint8_t bk4829_dcs_detected(uint8_t chip)
{
    return (bk4829_read_reg(chip, 0x0C) >> 14) & 1;
}

/* ========================================================================
 *  Squelch control - multi-stage (RSSI + noise + glitch)
 *
 *  OEM V0.27 @ 0x0801BA4C: complex frequency-dependent squelch that reads
 *  band-specific calibration tables from flash (RAM @ 0x200000E4).
 *  Uses frequency breakpoints: <400 MHz (VHF), >400 MHz (UHF), with
 *  sub-band resolution dividing into ~12 index positions per band.
 *  RSSI thresholds are looked up from per-band calibration data,
 *  not a single static table.
 *
 *  Our simplified implementation uses a static 9-level table as a
 *  reasonable starting point. Accurate per-band squelch requires
 *  extracting the full calibration table from flash.
 *
 *  Register mapping (verified):
 *    REG_78: RSSI thresholds (open << 8 | close)  - V0.27 @ 0x0801BB94
 *    REG_4F: Noise thresholds (close << 8 | open)
 *    REG_4D: Glitch close threshold + 0xA000
 *    REG_4E: Glitch open threshold + 0x6F00
 *  Level 0 = squelch disabled (always open).
 *  Reg 0x0C bit 1 = squelch open indicator.
 * ======================================================================== */

static const uint8_t squelch_thresholds[9] = {
    10, 15, 20, 25, 30, 40, 50, 75, 100,
};

void bk4829_set_squelch(uint8_t chip, uint8_t level)
{
    if (level == 0) {
        /* Squelch disabled - all thresholds to 0 */
        bk4829_write_reg(chip, 0x4D, 0xA000);
        bk4829_write_reg(chip, 0x4E, 0x6F00);
        bk4829_write_reg(chip, 0x4F, 0x0000);
        bk4829_write_reg(chip, 0x78, 0x0000);
        return;
    }
    if (level > 9)
        level = 9;

    uint8_t rssi_open  = squelch_thresholds[level - 1];
    uint8_t rssi_close = (rssi_open > 5) ? (rssi_open - 5) : 0;

    /* Noise thresholds scale with RSSI level */
    uint8_t noise_open  = rssi_open / 2;
    uint8_t noise_close = rssi_close / 2;

    /* Glitch thresholds - fixed values from OEM binary */
    bk4829_write_reg(chip, 0x4D, 0xA000 | 0x20);  /* glitch close */
    bk4829_write_reg(chip, 0x4E, 0x6F00 | 0x30);  /* glitch open */
    bk4829_write_reg(chip, 0x4F,
                     ((uint16_t)noise_close << 8) | noise_open);
    bk4829_write_reg(chip, 0x78,
                     ((uint16_t)rssi_open << 8) | rssi_close);
}

uint8_t bk4829_is_squelch_open(uint8_t chip)
{
    return (bk4829_read_reg(chip, 0x0C) >> 1) & 1;
}

/* ========================================================================
 *  Voice scrambler - frequency inversion via reg 0x31 bit 0
 *
 *  Verified from V0.27 binary - scrambler enable/disable values confirmed:
 *    Reg 0x42 = 0x6F5C, reg 0x2A = 0x7434, reg 0x2B = 0x0500
 *  Disable: reg 0x42 = 0x6B5A, reg 0x2A = 0x7400, reg 0x2B = 0x0000
 *  Toggle via reg 0x31 bit 0 (enable/disable).
 * ======================================================================== */

void bk4829_scrambler_enable(uint8_t chip)
{
    bk4829_write_reg(chip, 0x42, 0x6F5C);
    bk4829_write_reg(chip, 0x2A, 0x7434);
    bk4829_write_reg(chip, 0x2B, 0x0500);
    uint16_t r31 = bk4829_read_reg(chip, 0x31);
    bk4829_write_reg(chip, 0x31, r31 | 0x0001);
}

void bk4829_scrambler_disable(uint8_t chip)
{
    uint16_t r31 = bk4829_read_reg(chip, 0x31);
    bk4829_write_reg(chip, 0x31, r31 & ~0x0001U);
    bk4829_write_reg(chip, 0x42, 0x6B5A);
    bk4829_write_reg(chip, 0x2A, 0x7400);
    bk4829_write_reg(chip, 0x2B, 0x0000);
}
