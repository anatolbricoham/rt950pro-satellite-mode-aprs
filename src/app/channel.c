/*
 * channel.c - Memory channel management for the RT-950 Pro
 *
 * 990 channels x 32 bytes stored linearly at SPI flash address 0x000000.
 * Channel N lives at address N x 32.  Empty slots are all 0xFF.
 *
 * 32-byte record layout (from V0.27 RE analysis):
 *   [0..3]   RX frequency in BCD (4 bytes, MSB first, 10 Hz resolution)
 *   [4..7]   TX frequency in BCD (same format)
 *   [8]      RX CTCSS/DCS tone index
 *   [9]      TX CTCSS/DCS tone index
 *   [10]     RX tone type (0=off, 1=CTCSS, 2=DCS)
 *   [11]     TX tone type (0=off, 1=CTCSS, 2=DCS)
 *   [12]     Flags: bits[1:0]=power, bit2=bandwidth, bit3=scrambler
 *   [13]     Step size / group
 *   [14]     Modulation in upper nibble
 *   [15]     Scan/options: bit 2 = scan-add
 *   [16]     Squelch level (lower nibble)
 *   [17..19] Reserved
 *   [20..31] Channel name (12 bytes, ASCII, null-padded)
 */

#include "app/channel.h"
#include "drivers/spi.h"
#include "app/vfo.h"

#include <string.h>

/* Flash parameters ----------------------------------------------------- */

#define CH_FLASH_BASE       0x000000
#define CH_SECTOR_SIZE      4096U
#define CH_PAGE_SIZE        256U

/* Byte offsets within a 32-byte channel record ------------------------- */

#define OFF_RX_FREQ         0
#define OFF_TX_FREQ         4
#define OFF_RX_TONE_IDX     8
#define OFF_TX_TONE_IDX     9
#define OFF_RX_TONE_TYPE    10
#define OFF_TX_TONE_TYPE    11
#define OFF_FLAGS1          12
#define OFF_MOD_FLAGS       14
#define OFF_SCAN_FLAGS      15
#define OFF_SQUELCH         16
#define OFF_NAME            20

#define TONE_TYPE_OFF       0
#define TONE_TYPE_CTCSS     1
#define TONE_TYPE_DCS       2

#define CH_EMPTY_BYTE       0xFF

/* Shared sector buffer for read-modify-write (matches flash_layout.c pattern) */
static uint8_t sector_buf[CH_SECTOR_SIZE];

/* BCD <-> Hz conversion ------------------------------------------------- */

static uint32_t bcd_to_hz(const uint8_t bcd[4])
{
    uint32_t freq = 0;
    for (int i = 0; i < 4; i++)
        freq = freq * 100 + (uint32_t)((bcd[i] >> 4) * 10 + (bcd[i] & 0x0F));
    return freq * 10;
}

static void hz_to_bcd(uint32_t freq_hz, uint8_t bcd[4])
{
    uint32_t f = freq_hz / 10;
    for (int i = 3; i >= 0; i--) {
        bcd[i] = (uint8_t)(((f / 10) % 10) << 4 | (f % 10));
        f /= 100;
    }
}

/* Flash address helper ------------------------------------------------ */

static uint32_t ch_flash_addr(uint16_t ch_num)
{
    return CH_FLASH_BASE + (uint32_t)ch_num * CHANNEL_RECORD_SIZE;
}

/* Write a raw 32-byte record into flash using sector read-modify-write */
static void ch_write_raw(uint16_t ch_num, const uint8_t raw[CHANNEL_RECORD_SIZE])
{
    uint32_t addr            = ch_flash_addr(ch_num);
    uint32_t sect_base       = addr & ~(CH_SECTOR_SIZE - 1);
    uint32_t offset_in_sect  = addr - sect_base;

    spi_flash_read(sect_base, sector_buf, (uint16_t)CH_SECTOR_SIZE);
    memcpy(&sector_buf[offset_in_sect], raw, CHANNEL_RECORD_SIZE);

    spi_flash_erase_4k(sect_base);
    for (uint32_t off = 0; off < CH_SECTOR_SIZE; off += CH_PAGE_SIZE)
        spi_flash_write_page(sect_base + off,
                             &sector_buf[off], (uint16_t)CH_PAGE_SIZE);
}

/* ==========================================================================
 *  Public API
 * ========================================================================== */

void channel_init(void)
{
    /* Flash is accessible after SPI2 init - nothing extra needed */
}

/* Load ----------------------------------------------------------------- */

int channel_load(uint16_t ch_num, channel_t *ch)
{
    if (ch_num >= CHANNEL_COUNT)
        return -1;

    uint8_t raw[CHANNEL_RECORD_SIZE];
    spi_flash_read(ch_flash_addr(ch_num), raw, CHANNEL_RECORD_SIZE);

    /* Empty slot check (first byte 0xFF per firmware convention) */
    if (raw[0] == CH_EMPTY_BYTE)
        return -1;

    /* RX / TX frequency */
    ch->rx_freq_hz = bcd_to_hz(&raw[OFF_RX_FREQ]);
    if (ch->rx_freq_hz == 0)
        return -1;
    ch->tx_freq_hz = bcd_to_hz(&raw[OFF_TX_FREQ]);

    /* Tone indices - type byte selects CTCSS vs DCS */
    ch->ctcss_rx_idx = 0xFF;
    ch->ctcss_tx_idx = 0xFF;
    ch->dcs_rx_idx   = 0xFF;
    ch->dcs_tx_idx   = 0xFF;

    if (raw[OFF_RX_TONE_TYPE] == TONE_TYPE_CTCSS)
        ch->ctcss_rx_idx = raw[OFF_RX_TONE_IDX];
    else if (raw[OFF_RX_TONE_TYPE] == TONE_TYPE_DCS)
        ch->dcs_rx_idx = raw[OFF_RX_TONE_IDX];

    if (raw[OFF_TX_TONE_TYPE] == TONE_TYPE_CTCSS)
        ch->ctcss_tx_idx = raw[OFF_TX_TONE_IDX];
    else if (raw[OFF_TX_TONE_TYPE] == TONE_TYPE_DCS)
        ch->dcs_tx_idx = raw[OFF_TX_TONE_IDX];

    /* Flags byte 1: power, bandwidth, scrambler */
    ch->tx_power  = raw[OFF_FLAGS1] & 0x03;
    ch->bandwidth = (raw[OFF_FLAGS1] >> 2) & 0x01;
    ch->scrambler = (raw[OFF_FLAGS1] >> 3) & 0x01;

    /* Modulation (upper nibble of byte 14) */
    ch->modulation = (raw[OFF_MOD_FLAGS] >> 4) & 0x0F;

    /* Scan-add (bit 2 of byte 15) */
    ch->scan_add = (raw[OFF_SCAN_FLAGS] >> 2) & 0x01;

    /* Squelch (lower nibble of byte 16) */
    ch->squelch = raw[OFF_SQUELCH] & 0x0F;

    /* Channel name (12 bytes at offset 20, null-padded) */
    memcpy(ch->name, &raw[OFF_NAME], CHANNEL_NAME_LEN);
    ch->name[CHANNEL_NAME_LEN] = '\0';

    /* Trim trailing padding (spaces, nulls, erased bytes) */
    for (int i = CHANNEL_NAME_LEN - 1; i >= 0; i--) {
        uint8_t c = (uint8_t)ch->name[i];
        if (c == ' ' || c == '\0' || c == 0xFF)
            ch->name[i] = '\0';
        else
            break;
    }

    return 0;
}

/* Save ----------------------------------------------------------------- */

int channel_save(uint16_t ch_num, const channel_t *ch)
{
    if (ch_num >= CHANNEL_COUNT)
        return -1;

    uint8_t raw[CHANNEL_RECORD_SIZE];
    memset(raw, 0x00, sizeof(raw));

    /* Frequencies */
    hz_to_bcd(ch->rx_freq_hz, &raw[OFF_RX_FREQ]);
    hz_to_bcd(ch->tx_freq_hz, &raw[OFF_TX_FREQ]);

    /* RX tone */
    if (ch->ctcss_rx_idx != 0xFF) {
        raw[OFF_RX_TONE_IDX]  = ch->ctcss_rx_idx;
        raw[OFF_RX_TONE_TYPE] = TONE_TYPE_CTCSS;
    } else if (ch->dcs_rx_idx != 0xFF) {
        raw[OFF_RX_TONE_IDX]  = ch->dcs_rx_idx;
        raw[OFF_RX_TONE_TYPE] = TONE_TYPE_DCS;
    } else {
        raw[OFF_RX_TONE_IDX]  = 0xFF;
        raw[OFF_RX_TONE_TYPE] = TONE_TYPE_OFF;
    }

    /* TX tone */
    if (ch->ctcss_tx_idx != 0xFF) {
        raw[OFF_TX_TONE_IDX]  = ch->ctcss_tx_idx;
        raw[OFF_TX_TONE_TYPE] = TONE_TYPE_CTCSS;
    } else if (ch->dcs_tx_idx != 0xFF) {
        raw[OFF_TX_TONE_IDX]  = ch->dcs_tx_idx;
        raw[OFF_TX_TONE_TYPE] = TONE_TYPE_DCS;
    } else {
        raw[OFF_TX_TONE_IDX]  = 0xFF;
        raw[OFF_TX_TONE_TYPE] = TONE_TYPE_OFF;
    }

    /* Flags byte 1 */
    raw[OFF_FLAGS1] = (uint8_t)((ch->tx_power & 0x03)
                              | ((ch->bandwidth & 0x01) << 2)
                              | ((ch->scrambler & 0x01) << 3));

    /* Modulation in upper nibble */
    raw[OFF_MOD_FLAGS] = (uint8_t)((ch->modulation & 0x0F) << 4);

    /* Scan-add flag */
    raw[OFF_SCAN_FLAGS] = (uint8_t)((ch->scan_add & 0x01) << 2);

    /* Squelch */
    raw[OFF_SQUELCH] = ch->squelch & 0x0F;

    /* Name */
    memset(&raw[OFF_NAME], 0x00, CHANNEL_NAME_LEN);
    size_t nlen = strlen(ch->name);
    if (nlen > (size_t)CHANNEL_NAME_LEN)
        nlen = (size_t)CHANNEL_NAME_LEN;
    memcpy(&raw[OFF_NAME], ch->name, nlen);

    ch_write_raw(ch_num, raw);
    return 0;
}

/* Delete --------------------------------------------------------------- */

int channel_delete(uint16_t ch_num)
{
    if (ch_num >= CHANNEL_COUNT)
        return -1;

    uint8_t empty[CHANNEL_RECORD_SIZE];
    memset(empty, 0xFF, sizeof(empty));

    ch_write_raw(ch_num, empty);
    return 0;
}

/* Validity check ------------------------------------------------------- */

uint8_t channel_is_valid(uint16_t ch_num)
{
    if (ch_num >= CHANNEL_COUNT)
        return 0;

    uint8_t first;
    spi_flash_read(ch_flash_addr(ch_num), &first, 1);
    return (first != CH_EMPTY_BYTE) ? 1 : 0;
}

/* VFO integration ------------------------------------------------------ */

void channel_to_vfo(const channel_t *ch, uint8_t vfo_idx)
{
    radio_vfo_t vfo = (radio_vfo_t)vfo_idx;

    vfo_set_frequency(vfo, ch->rx_freq_hz);
    vfo_set_power(vfo, ch->tx_power);
    vfo_set_squelch(vfo, ch->squelch);

    /* Clear existing tones, then apply channel tones */
    vfo_clear_tone(vfo);

    if (ch->ctcss_tx_idx != 0xFF)
        vfo_set_ctcss_tx(vfo, ch->ctcss_tx_idx);
    if (ch->ctcss_rx_idx != 0xFF)
        vfo_set_ctcss_rx(vfo, ch->ctcss_rx_idx);

    /* DCS: VFO API has a single code - use TX code if set, else RX */
    if (ch->dcs_tx_idx != 0xFF)
        vfo_set_dcs(vfo, ch->dcs_tx_idx, 0);
    else if (ch->dcs_rx_idx != 0xFF)
        vfo_set_dcs(vfo, ch->dcs_rx_idx, 0);
}

void channel_from_vfo(channel_t *ch, uint8_t vfo_idx)
{
    const vfo_state_t *s = vfo_get_state((radio_vfo_t)vfo_idx);

    ch->rx_freq_hz   = s->freq_hz;
    ch->tx_freq_hz   = s->freq_hz;
    ch->tx_power     = s->tx_power;
    ch->bandwidth    = s->bandwidth;
    ch->modulation   = s->modulation;
    ch->scrambler    = s->scrambler;
    ch->squelch      = s->squelch_level;
    ch->scan_add     = 1;

    ch->ctcss_tx_idx = s->ctcss_tx_idx;
    ch->ctcss_rx_idx = s->ctcss_rx_idx;

    if (s->dcs_code_idx != 0xFF) {
        ch->dcs_tx_idx = s->dcs_code_idx;
        ch->dcs_rx_idx = s->dcs_code_idx;
    } else {
        ch->dcs_tx_idx = 0xFF;
        ch->dcs_rx_idx = 0xFF;
    }

    ch->name[0] = '\0';
}

/* Enumeration helpers -------------------------------------------------- */

uint16_t channel_count_used(void)
{
    uint16_t count = 0;
    for (uint16_t i = 0; i < CHANNEL_COUNT; i++) {
        if (channel_is_valid(i))
            count++;
    }
    return count;
}

uint16_t channel_find_next(uint16_t start_ch, int8_t direction)
{
    if (direction == 0)
        return 0xFFFF;

    uint16_t ch = start_ch;
    for (uint16_t i = 0; i < CHANNEL_COUNT; i++) {
        if (direction > 0)
            ch = (uint16_t)((ch + 1) % CHANNEL_COUNT);
        else
            ch = (ch == 0) ? (uint16_t)(CHANNEL_COUNT - 1) : (uint16_t)(ch - 1);

        if (channel_is_valid(ch))
            return ch;
    }
    return 0xFFFF;
}

uint16_t channel_find_next_scannable(uint16_t start_ch, int8_t direction)
{
    if (direction == 0)
        return 0xFFFF;

    uint16_t ch = start_ch;
    for (uint16_t i = 0; i < CHANNEL_COUNT; i++) {
        if (direction > 0)
            ch = (uint16_t)((ch + 1) % CHANNEL_COUNT);
        else
            ch = (ch == 0) ? (uint16_t)(CHANNEL_COUNT - 1) : (uint16_t)(ch - 1);

        /* Quick empty check on first byte */
        uint8_t first;
        spi_flash_read(ch_flash_addr(ch), &first, 1);
        if (first == CH_EMPTY_BYTE)
            continue;

        /* Check scan-add: bit 2 of byte 15 */
        uint8_t flags;
        spi_flash_read(ch_flash_addr(ch) + OFF_SCAN_FLAGS, &flags, 1);
        if ((flags >> 2) & 0x01)
            return ch;
    }
    return 0xFFFF;
}
