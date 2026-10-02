/*
 * si4732.c - SI4732 AM/FM/SW broadcast receiver driver for the RT-950 Pro
 *
 * Bit-bang I2C on GPIOB: PB6 = SCL (mask 0x40), PB7 = SDA (mask 0x80).
 * I2C address 0x22 write / 0x23 read (7-bit: 0x11, SEN pin = low).
 *
 * V0.27 binary I2C protocol (verified @ fw 0x08025A7C):
 *   Start:  SDA high -> SCL high -> delay(5) -> SDA low -> delay(5) -> SCL low
 *   Stop:   SDA low -> SCL low -> delay(5) -> SCL high -> delay(5) -> SDA high
 *   Byte:   MSB first, ACK clock performed but ACK value NOT checked by OEM
 *   Timing: delay(2) for data setup, delay(5) for clock high, delay(10) for
 *           pin reconfiguration (SDA input/output switching)
 */

#include "drivers/si4732.h"
#include "drivers/gpio.h"
#include "rt950_pinmap.h"

/* ========================================================================
 *  I2C timing - spin-wait matching firmware delay (~2-5 loop iterations)
 * ======================================================================== */

static void i2c_delay(void)
{
    for (volatile int i = 0; i < 10; i++) {
        /* ~83 ns/iter at 120 MHz ~ 830 ns total */
    }
}

/* ========================================================================
 *  SDA line control - open-drain emulation via input/output switching
 *
 *  High: configure as input with pull-up (external/internal pull-up drives)
 *  Low:  configure as push-pull output, drive low
 * ======================================================================== */

static void sda_high(void)
{
    gpio_config_pin(SI4732_SDA_PORT, SI4732_SDA_PIN,
                    GPIO_MODE_INPUT, GPIO_CNF_PULL);
    gpio_set_pin(SI4732_SDA_PORT, SI4732_SDA_PIN);
}

static void sda_low(void)
{
    gpio_config_pin(SI4732_SDA_PORT, SI4732_SDA_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_clear_pin(SI4732_SDA_PORT, SI4732_SDA_PIN);
}

static uint8_t sda_read(void)
{
    return gpio_read_pin(SI4732_SDA_PORT, SI4732_SDA_PIN) ? 1 : 0;
}

static void scl_high(void)
{
    gpio_set_pin(SI4732_SCL_PORT, SI4732_SCL_PIN);
}

static void scl_low(void)
{
    gpio_clear_pin(SI4732_SCL_PORT, SI4732_SCL_PIN);
}

/* ========================================================================
 *  I2C bit-bang primitives
 * ======================================================================== */

/* OEM @ fw 0x08025A7C: Start condition */
/* SDA high -> SCL high -> delay(5) -> SDA low -> delay(5) -> SCL low */
static void i2c_start(void)
{
    sda_high();
    scl_high();
    i2c_delay();
    sda_low();
    i2c_delay();
    scl_low();
}

/* OEM @ fw 0x08025B5C: Stop condition */
/* SDA low -> SCL low -> delay(5) -> SCL high -> delay(5) -> SDA high */
static void i2c_stop(void)
{
    sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high();
}

/*
 * OEM @ fw 0x08025BC0: Write one byte MSB-first.
 * OEM clocks ACK bit but does NOT check the ACK value - fire-and-forget.
 * Our version does check ACK for safety, but callers should be aware
 * the OEM doesn't rely on ACK.
 */
static int i2c_write_byte(uint8_t data)
{
    int ack;

    for (int i = 7; i >= 0; i--) {
        scl_low();
        if (data & (1U << i))
            sda_high();
        else
            sda_low();
        i2c_delay();
        scl_high();
        i2c_delay();
    }

    /* ACK clock: release SDA, clock in ACK bit */
    scl_low();
    sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    ack = sda_read();
    scl_low();

    return ack;  /* 0 = ACK, 1 = NACK */
}

/*
 * OEM @ fw 0x080259B4: Read one byte MSB-first.
 * OEM param is remaining_count (>0 = ACK, 0 = NACK for last byte).
 * Our ack parameter serves the same purpose: 1=ACK, 0=NACK.
 */
static uint8_t i2c_read_byte(int ack)
{
    uint8_t data = 0;

    sda_high();  /* release SDA for reading */

    for (int i = 7; i >= 0; i--) {
        scl_low();
        i2c_delay();
        scl_high();
        i2c_delay();
        if (sda_read())
            data |= (uint8_t)(1U << i);
    }

    /* Send ACK (SDA low) or NACK (SDA high) */
    scl_low();
    if (ack)
        sda_low();
    else
        sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    sda_high();

    return data;
}

/* ========================================================================
 *  I2C command/response helpers
 * ======================================================================== */

/*
 * OEM @ fw 0x08026584: Send a command buffer to the SI4732.
 * Returns 0 on success (all bytes ACK'd), -1 on NACK.
 */
static int i2c_send_cmd(const uint8_t *cmd, uint8_t len)
{
    int ret = 0;

    i2c_start();
    if (i2c_write_byte(SI4732_I2C_ADDR_W)) {
        i2c_stop();
        return -1;
    }
    for (uint8_t i = 0; i < len; i++) {
        if (i2c_write_byte(cmd[i])) {
            ret = -1;
            break;
        }
    }
    i2c_stop();
    return ret;
}

/*
 * OEM @ fw 0x080264F0: Read response bytes from the SI4732.
 * Returns 0 on success, -1 on address NACK.
 */
static int i2c_read_resp(uint8_t *buf, uint8_t len)
{
    i2c_start();
    if (i2c_write_byte(SI4732_I2C_ADDR_R)) {
        i2c_stop();
        return -1;
    }
    for (uint8_t i = 0; i < len; i++) {
        /* ACK all bytes except the last one */
        buf[i] = i2c_read_byte(i < (uint8_t)(len - 1));
    }
    i2c_stop();
    return 0;
}

/* ========================================================================
 *  Wait for CTS (Clear To Send) - poll status byte bit 7
 * ======================================================================== */

/*
 * OEM @ fw 0x0802662C: Wait for CTS (Clear To Send) - poll status byte bit 7.
 * OEM polls up to 100 times with delay(500) between each attempt.
 */

static int si4732_wait_cts(void)
{
    uint8_t status;

    for (int attempt = 0; attempt < 100; attempt++) {
        if (i2c_read_resp(&status, 1) < 0)
            continue;
        if (status & SI4732_STATUS_CTS)
            return 0;
        i2c_delay();
    }
    return -1;  /* timeout */
}

/* ========================================================================
 *  Public API
 * ======================================================================== */

void si4732_init(void)
{
    /* Enable GPIOB clock (may already be enabled - safe to re-enable) */
    gpio_enable_clock(SI4732_SCL_PORT);

    /* SCL: push-pull output, idle high */
    gpio_config_pin(SI4732_SCL_PORT, SI4732_SCL_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_set_pin(SI4732_SCL_PORT, SI4732_SCL_PIN);

    /* SDA: input with pull-up, idle high */
    gpio_config_pin(SI4732_SDA_PORT, SI4732_SDA_PIN,
                    GPIO_MODE_INPUT, GPIO_CNF_PULL);
    gpio_set_pin(SI4732_SDA_PORT, SI4732_SDA_PIN);

    /* Hardware reset via PA6 (FM_RESET) - OEM toggles before patch upload.
     * Active-low reset: drive LOW to reset, then HIGH to release. */
    gpio_enable_clock(FM_RESET_PORT);
    gpio_config_pin(FM_RESET_PORT, FM_RESET_PIN,
                    GPIO_MODE_OUT_2MHZ, GPIO_CNF_PP);
    gpio_set_pin(FM_RESET_PORT, FM_RESET_PIN);     /* idle HIGH */
    for (volatile int i = 0; i < 12000; i++) { }    /* ~100 us */
    gpio_clear_pin(FM_RESET_PORT, FM_RESET_PIN);   /* assert reset */
    for (volatile int i = 0; i < 1200000; i++) { }  /* ~10 ms */
    gpio_set_pin(FM_RESET_PORT, FM_RESET_PIN);     /* release reset */
    for (volatile int i = 0; i < 1200000; i++) { }  /* ~10 ms settle */
}

/* OEM @ fw 0x08026460: POWER_UP [0x01, 0x10, 0x05] for FM */
int si4732_power_up_fm(void)
{
    const uint8_t cmd[] = {
        SI4732_CMD_POWER_UP,
        SI4732_FUNC_FM_RECV | SI4732_POWERUP_PATCH,
        SI4732_OUT_ANALOG
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

/* OEM @ fw 0x08026460 (offset +0x20): POWER_UP [0x01, 0x11, 0x05] for AM */
int si4732_power_up_am(void)
{
    const uint8_t cmd[] = {
        SI4732_CMD_POWER_UP,
        SI4732_FUNC_AM_RECV | SI4732_POWERUP_PATCH,
        SI4732_OUT_ANALOG
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

/* OEM @ fw 0x0800E94C: sends [0x11], reads 1 byte, delay(600) */
void si4732_power_down(void)
{
    const uint8_t cmd[] = { SI4732_CMD_POWER_DOWN };
    uint8_t status;
    i2c_send_cmd(cmd, sizeof(cmd));
    i2c_read_resp(&status, 1);
    /* OEM uses delay(600) NOP-loop after power_down - spin wait */
    for (volatile int i = 0; i < 600; i++) { }
}

int si4732_fm_tune(uint16_t freq_10khz)
{
    const uint8_t cmd[] = {
        SI4732_CMD_FM_TUNE_FREQ,
        0x00,
        (uint8_t)(freq_10khz >> 8),
        (uint8_t)(freq_10khz & 0xFF),
        0x00
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

/* OEM @ fw 0x08003C34: AM_TUNE_FREQ [0x40, flags, freq_hi, freq_lo, 0x00, 0x01] */
int si4732_am_tune(uint16_t freq_khz)
{
    const uint8_t cmd[] = {
        SI4732_CMD_AM_TUNE_FREQ,
        0x00,                           /* flags: 0x00=normal, 0x40=FAST, 0x80=FREEZE/SSB */
        (uint8_t)(freq_khz >> 8),
        (uint8_t)(freq_khz & 0xFF),
        0x00,                           /* reserved */
        0x01                            /* ANTCAP = 1 always in OEM */
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

int si4732_fm_tune_status(struct si4732_tune_status *st)
{
    const uint8_t cmd[] = { SI4732_CMD_FM_TUNE_STATUS, 0x01 };
    uint8_t resp[8];

    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    if (si4732_wait_cts() < 0)
        return -1;
    if (i2c_read_resp(resp, sizeof(resp)) < 0)
        return -1;

    st->valid = resp[1] & SI4732_TUNE_VALID;
    st->freq  = (uint16_t)((uint16_t)resp[2] << 8) | resp[3];
    st->rssi  = resp[4];
    st->snr   = resp[5];

    return 0;
}

int si4732_am_tune_status(struct si4732_tune_status *st)
{
    const uint8_t cmd[] = { SI4732_CMD_AM_TUNE_STATUS, 0x01 };
    uint8_t resp[8];

    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    if (si4732_wait_cts() < 0)
        return -1;
    if (i2c_read_resp(resp, sizeof(resp)) < 0)
        return -1;

    st->valid = resp[1] & SI4732_TUNE_VALID;
    st->freq  = (uint16_t)((uint16_t)resp[2] << 8) | resp[3];
    st->rssi  = resp[4];
    st->snr   = resp[5];

    return 0;
}

/* Weather band (NOAA) support */

int si4732_power_up_wb(void)
{
    const uint8_t cmd[] = {
        SI4732_CMD_POWER_UP,
        SI4732_FUNC_WB_RECV | SI4732_POWERUP_PATCH,
        SI4732_OUT_ANALOG
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

int si4732_wb_tune(uint32_t freq_khz)
{
    /*
     * SI4732 WB_TUNE_FREQ command format (AN332):
     *   Byte 0: 0x50 (WB_TUNE_FREQ)
     *   Byte 1: 0x00 (reserved)
     *   Byte 2: FREQ[15:8]
     *   Byte 3: FREQ[7:0]
     *
     * NOAA frequencies (162400-162550 kHz) require full 18-bit range.
     * The SI4732 WB tune uses the frequency in kHz directly,
     * which fits in the 16-bit command field for the standard
     * WB range (162.4-162.55 MHz = 162400-162550 kHz).
     *
     * NOTE: The SI4732 AN332 actually specifies WB frequency
     * in units of 2.5 kHz above 162 MHz, not raw kHz. The
     * encoding is: freq_word = (freq_khz - 162000) / 2.5 * 64
     * However, the OEM binary sends raw kHz values for WB tune,
     * and the Si4735-D60 variant accepts raw kHz.
     */
    const uint8_t cmd[] = {
        SI4732_CMD_WB_TUNE_FREQ,
        0x00,
        (uint8_t)((freq_khz >> 8) & 0xFF),
        (uint8_t)(freq_khz & 0xFF)
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

int si4732_wb_tune_status(struct si4732_tune_status *st)
{
    const uint8_t cmd[] = { SI4732_CMD_WB_TUNE_STATUS, 0x01 };
    uint8_t resp[8];

    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    if (si4732_wait_cts() < 0)
        return -1;
    if (i2c_read_resp(resp, sizeof(resp)) < 0)
        return -1;

    st->valid = resp[1] & SI4732_TUNE_VALID;
    st->freq  = (uint16_t)((uint16_t)resp[2] << 8) | resp[3];
    st->rssi  = resp[4];
    st->snr   = resp[5];

    return 0;
}

/* OEM @ fw 0x08026604: SET_PROPERTY [0x12, 0x00, prop_hi, prop_lo, val_hi, val_lo] */
int si4732_set_property(uint16_t prop, uint16_t value)
{
    const uint8_t cmd[] = {
        SI4732_CMD_SET_PROPERTY,
        0x00,
        (uint8_t)(prop >> 8),
        (uint8_t)(prop & 0xFF),
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF)
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

int si4732_get_rev(uint8_t *part_number)
{
    const uint8_t cmd[] = { SI4732_CMD_GET_REV };
    uint8_t resp[9];

    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    if (si4732_wait_cts() < 0)
        return -1;
    if (i2c_read_resp(resp, sizeof(resp)) < 0)
        return -1;

    *part_number = resp[1];
    return 0;
}

uint8_t si4732_get_status(void)
{
    uint8_t status = 0;
    i2c_read_resp(&status, 1);
    return status;
}

/* ========================================================================
 *  AM bandwidth control
 * ======================================================================== */

int si4732_am_set_bandwidth(uint16_t bw_value)
{
    return si4732_set_property(SI4732_PROP_AM_CHANNEL_FILTER, bw_value);
}

/* ========================================================================
 *  SSB support - patch ROM upload + SSB-specific commands
 *
 *  OEM V0.27 @ fw 0x080263E4 (power_up_am_ssb):
 *    1. POWER_UP [0x01, 0x31, 0x05] - AM+PATCH+XOSCEN
 *    2. ssb_patch_load: 15,832 bytes (0x3DD8) from fw 0x08028BA0
 *       uploaded as 1,979 x 8-byte I2C commands with wait_cts between
 *    3. Post-patch property configuration
 *
 *  The SSB patch binary is stored in firmware FLASH, NOT SPI flash.
 *  It is proprietary to Silicon Labs. The OEM binary includes it at
 *  file offset 0x028BA0.
 * ======================================================================== */

/*
 * SSB patch data placeholder. The actual 15,832-byte patch must be
 * extracted from the OEM binary (fw 0x08028BA0) and placed here or
 * in a separate data file.
 *
 * OEM upload loop @ fw 0x080265D0:
 *   for (offset = 0; offset < 0x3DD8; offset += 8) {
 *       wait_cts();
 *       i2c_send_cmd(&patch[offset], 8);
 *   }
 */

int si4732_ssb_patch_load(void)
{
    /* TODO: Replace with actual patch upload once patch data is extracted.
     *
     * The required sequence is:
     *   1. Power up with SI4732_FUNC_AM_RECV | PATCH | XOSCEN
     *   2. For each 8-byte block: wait_cts(), i2c_send_cmd(block, 8)
     *   3. Configure post-patch properties (GPO_IEN, AM_NB_RATE, etc.)
     *
     * Without the patch data, SSB mode will not function.
     */

    /* Set default SSB properties (OEM post-patch config) */
    si4732_set_property(SI4732_PROP_GPO_IEN, 0x0001);
    si4732_set_property(SI4732_PROP_AM_NB_RATE, 0x7800);
    si4732_set_property(0x0101, 0x0002);    /* SSB_MODE = USB */
    si4732_set_property(0x0100, 0x0000);    /* BFO = 0 Hz */

    return 0;
}

/* OEM SSB tune uses AM_TUNE_FREQ with flags=0x80 and ANTCAP=1 */
int si4732_ssb_tune(uint16_t freq_khz)
{
    const uint8_t cmd[] = {
        SI4732_CMD_AM_TUNE_FREQ,
        0x80,                           /* flags: 0x80 = FREEZE/SSB mode */
        (uint8_t)(freq_khz >> 8),
        (uint8_t)(freq_khz & 0xFF),
        0x00,                           /* reserved */
        0x01                            /* ANTCAP = 1 always in OEM */
    };
    if (i2c_send_cmd(cmd, sizeof(cmd)) < 0)
        return -1;
    return si4732_wait_cts();
}

int si4732_ssb_set_mode(uint8_t mode)
{
    /* 1=LSB, 2=USB */
    return si4732_set_property(SI4732_PROP_SSB_MODE, (uint16_t)mode);
}

int si4732_ssb_set_bfo(int16_t offset_hz)
{
    return si4732_set_property(SI4732_PROP_SSB_BFO, (uint16_t)offset_hz);
}

int si4732_ssb_set_bandwidth(uint16_t bw_value)
{
    return si4732_set_property(SI4732_PROP_SSB_IF_BW, bw_value);
}

int si4732_ssb_set_agc(uint8_t agc_disable, uint8_t gain)
{
    uint16_t val = agc_disable ? (uint16_t)(1 | ((uint16_t)gain << 1)) : 0;
    return si4732_set_property(SI4732_PROP_SSB_AGC_OVERRIDE, val);
}
