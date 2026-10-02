/*
 * vox.c - Voice-Operated Switch for the RT-950 Pro
 *
 * Uses BK4829 hardware VOX detection:
 *   - REG 0x31 bit 1: VOX enable
 *   - REG 0x71: VOX threshold (from 9-level sensitivity table)
 *   - REG 0x0C: Status readback (bit 4 = VOX trigger)
 *   - REG 0x30: RX/TX path control
 *
 * Anti-VOX: suppresses triggering when speaker output is active
 * (prevents feedback loop in dual-VFO operation).
 *
 * Hang timer holds TX for ~1 second after voice audio stops,
 * preventing choppy transmission on speech pauses.
 */

#include "app/vox.h"
#include "drivers/bk4829.h"
#include "app/radio.h"

extern uint32_t get_tick(void);

/* BK4829 register definitions for VOX */
#define REG_VOX_ENABLE   0x31
#define REG_VOX_THRESH   0x71
#define REG_STATUS       0x0C
#define REG_GENERAL_EN   0x30

/* REG 0x31 bit assignments */
#define VOX_EN_BIT       (1U << 1)

/* REG 0x0C status bits */
#define STATUS_VOX_BIT   (1U << 4)

/* REG 0x30 values matching OEM firmware RE */
#define REG30_RX_MODE    0xBFF1U
#define REG30_TX_MODE    0xC3FAU

/* Hang timer duration in milliseconds */
#define VOX_HANG_MS      1000

/*
 * VOX threshold table - 9 sensitivity levels (index 0-8).
 * Level 1 (index 0) = most sensitive (lowest threshold).
 * Level 9 (index 8) = least sensitive (highest threshold).
 * Written to REG 0x71. Extracted from V0.27 firmware @ 0x0802B7FC.
 */
static const uint16_t vox_threshold_table[9] = {
    0xE0FE, 0x2B0C, 0x9A16, 0x4507,
    0x0577, 0x79F9, 0x163C, 0x4E62,
    0xDB27,
};

/* VOX operating on the primary chip (VFO A) */
#define VOX_CHIP  BK4829_CHIP1

/* Module state */
static uint8_t  vox_level;          /* 0=off, 1-9=active */
static uint8_t  vox_triggered;      /* 1 if VOX currently holding TX */
static uint8_t  vox_tx_active;      /* 1 if we initiated TX via VOX */
static uint32_t vox_hang_start;     /* Tick when audio last detected */

/* Anti-VOX: flag set externally when speaker is producing audio */
static uint8_t  speaker_active;

void vox_init(void)
{
    vox_level = 0;
    vox_triggered = 0;
    vox_tx_active = 0;
    vox_hang_start = 0;
    speaker_active = 0;

    /* Ensure VOX hardware is disabled on startup */
    uint16_t reg31 = bk4829_read_reg(VOX_CHIP, REG_VOX_ENABLE);
    reg31 &= ~VOX_EN_BIT;
    bk4829_write_reg(VOX_CHIP, REG_VOX_ENABLE, reg31);
}

/* Apply REG_40 FSK/VOX config: preserve upper nibble, set lower 12 bits */
static void vox_apply_reg40(void)
{
    uint16_t reg40 = bk4829_read_reg(VOX_CHIP, 0x40);
    bk4829_write_reg(VOX_CHIP, 0x40, (reg40 & 0xF000U) | 0x050AU);
}

void vox_set_level(uint8_t level)
{
    if (level > 9)
        level = 9;

    vox_level = level;

    if (level == 0) {
        /* Disable VOX hardware */
        uint16_t reg31 = bk4829_read_reg(VOX_CHIP, REG_VOX_ENABLE);
        reg31 &= ~VOX_EN_BIT;
        bk4829_write_reg(VOX_CHIP, REG_VOX_ENABLE, reg31);
        vox_apply_reg40();

        /* If we were holding TX via VOX, release it */
        if (vox_tx_active) {
            bk4829_write_reg(VOX_CHIP, REG_GENERAL_EN, REG30_RX_MODE);
            radio_ptt_off();
            vox_tx_active = 0;
        }
        vox_triggered = 0;
        return;
    }

    /* Enable VOX hardware and set threshold */
    uint16_t reg31 = bk4829_read_reg(VOX_CHIP, REG_VOX_ENABLE);
    reg31 |= VOX_EN_BIT;
    bk4829_write_reg(VOX_CHIP, REG_VOX_ENABLE, reg31);

    bk4829_write_reg(VOX_CHIP, REG_VOX_THRESH,
                     vox_threshold_table[level - 1]);
    vox_apply_reg40();
}

uint8_t vox_get_level(void)
{
    return vox_level;
}

void vox_poll(void)
{
    if (vox_level == 0)
        return;

    /* Anti-VOX: don't trigger during active speaker output */
    if (speaker_active)
        return;

    /* Don't initiate or manage VOX TX while manual PTT is active.
     * This prevents the conflict where releasing manual PTT would
     * kill a VOX-initiated TX or vice versa. */
    if (radio_is_transmitting() && !vox_tx_active)
        return;

    uint32_t now = get_tick();
    uint16_t status = bk4829_read_reg(VOX_CHIP, REG_STATUS);
    uint8_t audio_present = (status & STATUS_VOX_BIT) ? 1 : 0;

    if (audio_present) {
        /* Audio detected - start or continue TX */
        vox_hang_start = now;

        if (!vox_tx_active) {
            /* Transition to TX */
            vox_tx_active = 1;
            radio_ptt_on();
            bk4829_write_reg(VOX_CHIP, REG_GENERAL_EN, REG30_TX_MODE);
        }
        vox_triggered = 1;
    } else if (vox_tx_active) {
        /* No audio - check hang timer */
        if ((now - vox_hang_start) >= VOX_HANG_MS) {
            /* Hang timer expired - return to RX */
            bk4829_write_reg(VOX_CHIP, REG_GENERAL_EN, REG30_RX_MODE);
            radio_ptt_off();
            vox_tx_active = 0;
            vox_triggered = 0;
        }
        /* else: still within hang time, stay in TX */
    }
}

uint8_t vox_is_triggered(void)
{
    return vox_triggered;
}
