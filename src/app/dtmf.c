/*
 * dtmf.c - DTMF tone encode/decode for the RT-950 Pro
 *
 * Uses BK4829 hardware DTMF generation and detection.
 *
 * OEM DTMF detection (bk4829_check_dtmf_active @ 0x0801B2B0, 34B):
 *   1. Read REG 0x0D - bit 15 = DTMF detected flag
 *   2. If bit15 clear → no DTMF, return 0
 *   3. Extract bits [10:0] from REG 0x0D (tone status)
 *   4. Read REG 0x0E - extended status / decoded digit
 *   5. Return (0x0D[10:0] << 16) | 0x0E[15:0]
 *
 * DTMF tone generation registers:
 *   0x70  DTMF/AFSK control - tone generation (also used for APRS AFSK)
 *         Exact bit layout for DTMF mode not fully verified in OEM.
 *   0x71  Timing control (may overlap with VOX/CTCSS - use with caution)
 *
 * NOTE: Registers 0x70/0x71 for DTMF TX need BK4829 datasheet verification.
 *       The OEM uses 0x70=0x00AC for APRS AFSK TX (@ 0x0801EB8C).
 */

#include "app/dtmf.h"
#include "drivers/bk4829.h"

extern void delay_ms(uint32_t ms);

/* BK4829 DTMF-related registers */
#define BK4829_REG_DTMF_CTL    0x70  /* Tone generation control (unverified for DTMF) */
#define BK4829_REG_DTMF_TIM    0x71  /* Timing control (unverified - may be CTCSS) */
#define BK4829_REG_STATUS      0x0D  /* OEM @ 0x0801B2B4: movs r0,#13 - DTMF status */
#define BK4829_REG_DTMF_DEC    0x0E  /* OEM @ 0x0801B2CA: movs r0,#14 - decoded digit */

/* Status register bit for DTMF detection */
#define STATUS_DTMF_DETECTED   (1U << 15)

/* Default DTMF timing */
#define DTMF_DEFAULT_TONE_MS   100
#define DTMF_DEFAULT_GAP_MS     50

/* Character <-> index conversion tables ---------------------------------- */

static const char index_to_char[DTMF_CHAR_COUNT] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', '*', '#'
};

uint8_t dtmf_char_to_index(char ch)
{
    if (ch >= '0' && ch <= '9')
        return (uint8_t)(ch - '0');
    switch (ch) {
    case 'A': case 'a': return 10;
    case 'B': case 'b': return 11;
    case 'C': case 'c': return 12;
    case 'D': case 'd': return 13;
    case '*':           return 14;
    case '#':           return 15;
    default:            return 0xFF;
    }
}

char dtmf_index_to_char(uint8_t index)
{
    if (index < DTMF_CHAR_COUNT)
        return index_to_char[index];
    return '?';
}

/* Tone generation ------------------------------------------------------ */

void dtmf_send_tone(uint8_t chip, char ch, uint16_t duration_ms)
{
    uint8_t idx = dtmf_char_to_index(ch);
    if (idx == 0xFF)
        return;

    /* Start tone: index in bits [7:4], start bit 0 */
    bk4829_write_reg(chip, BK4829_REG_DTMF_CTL,
                     ((uint16_t)idx << 4) | 0x01);

    delay_ms(duration_ms);

    /* Stop tone */
    bk4829_write_reg(chip, BK4829_REG_DTMF_CTL, 0x0000);
}

void dtmf_send_string(uint8_t chip, const char *str,
                       uint16_t tone_ms, uint16_t gap_ms)
{
    if (!str)
        return;

    for (unsigned i = 0; str[i] != '\0'; i++) {
        if (i > 0)
            delay_ms(gap_ms);
        dtmf_send_tone(chip, str[i], tone_ms);
    }
}

/* Decode monitoring ---------------------------------------------------- */

void dtmf_decode_start(uint8_t chip)
{
    /* Enable DTMF decode via reg 0x70 bit 1 */
    uint16_t ctl = bk4829_read_reg(chip, BK4829_REG_DTMF_CTL);
    bk4829_write_reg(chip, BK4829_REG_DTMF_CTL, ctl | 0x02);
}

void dtmf_decode_stop(uint8_t chip)
{
    /* Disable DTMF decode - clear control register */
    bk4829_write_reg(chip, BK4829_REG_DTMF_CTL, 0x0000);
}

char dtmf_decode_poll(uint8_t chip)
{
    /*
     * OEM pattern (bk4829_check_dtmf_active @ 0x0801B2B0):
     *   REG 0x0D bit15 = DTMF detected (lsls r1,r0,#16; bpl = not set)
     *   REG 0x0D bits[10:0] = tone status (ubfx r0,r0,#0,#11)
     *   REG 0x0E = extended data / digit info
     */
    uint16_t status = bk4829_read_reg(chip, BK4829_REG_STATUS);

    if (status & STATUS_DTMF_DETECTED) {
        /* Bit 15 set means NOT detected in OEM (bpl = branch if positive).
         * OEM returns 0 when bit15 is set. When bit15 clear, tone present. */
        return '\0';
    }

    /* Tone detected - extract digit from REG 0x0E */
    uint16_t dec = bk4829_read_reg(chip, BK4829_REG_DTMF_DEC);
    uint8_t idx = (uint8_t)(dec & 0x0F);

    return dtmf_index_to_char(idx);
}

/* Contact dialing ------------------------------------------------------ */

void dtmf_dial_contact(uint8_t chip, const dtmf_contact_t *contact)
{
    if (!contact)
        return;
    dtmf_send_string(chip, contact->digits,
                     DTMF_DEFAULT_TONE_MS, DTMF_DEFAULT_GAP_MS);
}

/* PTT-ID configuration from flash ------------------------------------- */

/*
 * Flash 0x0C000 layout (256 bytes, "Block 5: DTMF + modulation"):
 *   0x00:     Current ID string (16 bytes, ASCII digits, 0xFF-padded)
 *   0x10:     DTMF speed - on-time index (1 byte: 0=100ms..9=1000ms)
 *   0x11:     DTMF speed - off-time index (1 byte: same encoding)
 *   0x12-0x1F: reserved
 *   0x20:     Group 1 code (8 bytes, ASCII digits, 0xFF-padded)
 *   0x28:     Group 2 code
 *   ...
 *   0x90:     Group 15 code
 *
 * CPS capture shows: Current ID="123", On=300ms (idx 2), Off=300ms (idx 2)
 */

#include "drivers/spi.h"
#include "drivers/flash_layout.h"

#define DTMF_FLASH_BASE   FLASH_ADDR_DTMF   /* 0x0C000 */
#define DTMF_ID_OFFSET    0x00
#define DTMF_SPEED_OFFSET 0x10

/* Speed index to milliseconds: 100, 200, 300, ... 1000 */
static uint16_t speed_idx_to_ms(uint8_t idx)
{
    if (idx > 9) idx = 2; /* default 300ms for invalid values */
    return (uint16_t)(idx + 1) * 100;
}

static dtmf_config_t dtmf_cfg;

void dtmf_load_config(void)
{
    uint8_t buf[18]; /* 16 bytes ID + 2 bytes speed */

    spi_flash_read(DTMF_FLASH_BASE + DTMF_ID_OFFSET, buf, 18);

    /* Parse current ID - copy valid DTMF chars, stop at 0xFF or padding */
    uint8_t len = 0;
    for (uint8_t i = 0; i < DTMF_ID_MAX_LEN && len < DTMF_ID_MAX_LEN; i++) {
        char ch = (char)buf[i];
        if (ch == (char)0xFF || ch == '\0')
            break;
        if (dtmf_char_to_index(ch) != 0xFF)
            dtmf_cfg.current_id[len++] = ch;
    }
    dtmf_cfg.current_id[len] = '\0';

    /* Parse speed indices */
    dtmf_cfg.tone_ms = speed_idx_to_ms(buf[16]);
    dtmf_cfg.gap_ms  = speed_idx_to_ms(buf[17]);
}

const dtmf_config_t *dtmf_get_config(void)
{
    return &dtmf_cfg;
}

void dtmf_send_ptt_id(uint8_t chip)
{
    if (dtmf_cfg.current_id[0] == '\0')
        return;
    dtmf_send_string(chip, dtmf_cfg.current_id,
                     dtmf_cfg.tone_ms, dtmf_cfg.gap_ms);
}
