/*
 * radio.c - High-level radio control for the RT-950 Pro
 *
 * Manages dual BK4829 RF transceivers, band selection,
 * PTT control, and VFO state.
 *
 * BK4829 SPI: PE8=SEN1, PE15=SEN2, PE10=SCK, PE11=SDA (shared bus)
 *
 * V0.27 OEM radio app functions:
 *   Frequency set    @ 0x0801BD40 (REG_38/39 raw Hz, tail-call mode_set)
 *   Mode set (REG_30)@ 0x0801B9CC (two-step: VCO cal + final value)
 *   Mode select (REG_47) @ 0x0801BC2C (base 0x6042 | lut[mode])
 *   RF gain (REG_48) @ 0x0801BC50 (FM: 0xB00F|cal, AM: 0xB0A3 fixed)
 *   TX power set     @ 0x0801BD64 (table @ 0x0802B724: 0x8E79/0xA498/0xF016)
 *   Squelch          @ 0x0801BA4C (freq-dependent, per-band cal @ 0x200000E4)
 *   PLL settle delay @ 0x0800A962 (arg*31 iterations, ~39us for arg=50)
 *
 * VFO A/B/C state machine: active VFO drives BK4829 chip 0,
 *   secondary monitors on chip 1 (dual-watch).
 *
 * RX/TX mode control (REG_30 values, verified):
 *   STANDBY=0x0000, RX=0xBFF1, TX=0xC1FE, AM_RX=0x0302, SCAN=0xC3FA
 *
 * CTCSS/DCS: 50 CTCSS tones @ 0x08028C36, 104 DCS codes @ 0x08028B2A.
 *   Encode/decode integration via BK4829 hardware registers.
 *
 * PTT / Relay system (V0.27 @ 0x0801D268):
 *   6 relay GPIOs:  PE13=PTT primary, PE7=MIC, PB1=ACC1,
 *                   PE12=external, PB0=ACC0, PE14=PA gate
 *   All relays are ACTIVE-LOW.
 *   OEM relay_select(sub_mode, mode) uses 4 modes:
 *     Mode 0: Assert ALL for TX (PE13->PE7->PB1->PE12->PB0->PE14, no delays)
 *     Mode 1: Band/path selection (varies by sub-mode)
 *     Mode 2: Audio path selection (MIC/ACC routing)
 *     Mode 3: Antenna path configuration
 *   TX sequence: Relays FIRST, then BK4829 mode change (V0.27 @ 0x0801C2E2)
 *   TX-off:      Relays FIRST, then BK4829 RX enable  (V0.27 @ 0x0801C308)
 */

#include "app/radio.h"
#include "app/vfo.h"
#include "app/keypad.h"
#include "app/menu.h"
#include "app/freq_entry.h"
#include "app/fm_radio.h"
#include "app/am_radio.h"
#include "app/scanner.h"
#include "app/dtmf.h"
#include "app/power.h"
#include "app/audio.h"
#include "drivers/bk4829.h"
#include "drivers/gpio.h"
#include "drivers/calibration.h"
#include "app/settings.h"
#include "app/satellite.h"
#include "app/aprs_msg.h"
#include "rt950_pinmap.h"
#include "debug_uart.h"

extern void delay_ms(uint32_t ms);

static uint8_t transmitting;
static radio_vfo_t tx_vfo_used;   /* VFO whose chip is keyed (sat split) */
static uint16_t tot_seconds;      /* seconds elapsed since TX started */
static uint8_t  tot_warned;       /* 1 = pre-timeout warning already fired */

/*
 * TOT (Transmit Time-Out Timer) seconds lookup.
 * settings.tot index -> seconds.  0 = disabled.
 * Values: OFF, 30, 60, 90, 120, 150, 180, 210, 240
 */
static const uint16_t tot_table[9] = {
    0, 30, 60, 90, 120, 150, 180, 210, 240
};

#define DTMF_TONE_MS  120   /* per-digit DTMF tone duration */

/* Helpers -------------------------------------------------------------- */

/* Convert KEY_* code to digit 0-9, or 0xFF if not a digit key */
static uint8_t key_to_digit(uint8_t key)
{
    switch (key) {
    case KEY_0: return 0;
    case KEY_1: return 1;
    case KEY_2: return 2;
    case KEY_3: return 3;
    case KEY_4: return 4;
    case KEY_5: return 5;
    case KEY_6: return 6;
    case KEY_7: return 7;
    case KEY_8: return 8;
    case KEY_9: return 9;
    default:    return 0xFF;
    }
}

/* Send DTMF tone for a keypad press while transmitting */
static void handle_dtmf_key(uint8_t key)
{
    uint8_t chip = vfo_get_state(vfo_get_active())->chip;
    uint8_t d = key_to_digit(key);

    if (d != 0xFF)
        dtmf_send_tone(chip, (char)('0' + d), DTMF_TONE_MS);
    else if (key == KEY_STAR)
        dtmf_send_tone(chip, '*', DTMF_TONE_MS);
    else if (key == KEY_HASH)
        dtmf_send_tone(chip, '#', DTMF_TONE_MS);
}

/* Band / relay routing ------------------------------------------------- */

/*
 * Map operating frequency to sub-mode index (0-4).
 *
 * The OEM relay_select() dispatches on a sub-mode parameter that selects
 * which antenna/audio relay path to activate.  The 4 calibration subbands
 * map directly to sub-modes 0-3:
 *
 *   sub-mode 0 : 136-174 MHz  (2m VHF)
 *   sub-mode 1 : 400-520 MHz  (70cm UHF)
 *   sub-mode 2 : 220-260 MHz  (1.25m)
 *   sub-mode 3 : 350-390 MHz  (300 MHz, normally disabled)
 *   sub-mode 4 : fallback / special (PA off path in mode 2)
 *
 * Returns 0 (VHF) if frequency doesn't match any subband.
 */
static uint8_t freq_to_submode(uint32_t freq_hz)
{
    for (uint8_t i = 0; i < CAL_NUM_SUBBANDS; i++) {
        if (cal_data.subbands[i].enabled &&
            freq_hz >= cal_data.subbands[i].low_hz &&
            freq_hz <= cal_data.subbands[i].high_hz)
            return i;
    }
    if (freq_hz >= 400000000) return 1;  /* UHF fallback */
    if (freq_hz >= 220000000) return 2;  /* 1.25m fallback */
    return 0;                            /* VHF fallback */
}

/*
 * RF frontend enable/disable (PB8, PE4).
 *
 * OEM V0.27 analysis of fcn.08022E3C reveals PB8 and PE4 are NOT a 2-bit
 * band selector. They are ALWAYS set/cleared together as a ref-counted
 * RF frontend enable/disable. 6 independent "consumers" can request
 * RF-on; when any consumer is active both pins go HIGH, when all are
 * released both go LOW. PC9 is also cleared in the all-off path.
 *
 * Actual per-band filter selection is handled by BK4829 registers
 * (REG_30, REG_47, REG_48), not by MCU GPIOs.
 */
static void rf_frontend_enable(void)
{
    gpio_set_pin(RF_FRONTEND_EN0_PORT, RF_FRONTEND_EN0_PIN);
    gpio_set_pin(RF_FRONTEND_EN1_PORT, RF_FRONTEND_EN1_PIN);
}

static void rf_frontend_disable(void)
{
    gpio_clear_pin(RF_FRONTEND_EN0_PORT, RF_FRONTEND_EN0_PIN);
    gpio_clear_pin(RF_FRONTEND_EN1_PORT, RF_FRONTEND_EN1_PIN);
}

/*
 * Enable RF frontend for the operating frequency.
 * Band-specific configuration is handled by BK4829 registers;
 * the MCU GPIO pair (PB8+PE4) is only an enable/disable.
 */
static void select_band_for_freq(uint8_t chip, uint32_t freq_hz)
{
    (void)chip;
    (void)freq_hz;
    rf_frontend_enable();
}

/* PTT / Relay control -------------------------------------------------- */

/*
 * relay_select() - OEM V0.27 @ 0x0801D268
 *
 * 4 relay modes with per-submode dispatch.  All GPIOs are ACTIVE-LOW.
 * No delays between GPIO writes within any mode (matches OEM exactly).
 *
 * Antenna group: PE13 (primary), PE12 (external), PB0 (ACC0)
 * Audio group:   PE7 (MIC), PB1 (ACC1), PE14 (PA gate)
 */
static void relay_select(uint8_t sub_mode, uint8_t mode)
{
    switch (mode) {

    /* Mode 0 (@ 0x0801D290): Full TX assert, all 6 pins LOW */
    case 0:
        gpio_clear_pin(PTT_PRIMARY_PORT,  PTT_PRIMARY_PIN);
        gpio_clear_pin(PTT_MIC_PORT,      PTT_MIC_PIN);
        gpio_clear_pin(PTT_ACC1_PORT,     PTT_ACC1_PIN);
        gpio_clear_pin(PTT_EXTERNAL_PORT, PTT_EXTERNAL_PIN);
        gpio_clear_pin(PTT_ACC0_PORT,     PTT_ACC0_PIN);
        gpio_clear_pin(PTT_PA_GATE_PORT,  PTT_PA_GATE_PIN);
        break;

    /* Mode 1 (@ 0x0801D2C4): Band/path selection (RX config) */
    case 1:
        switch (sub_mode) {
        case 0:  /* VHF: PE13 low, PE12 low, PB0 high */
            gpio_clear_pin(PTT_PRIMARY_PORT,  PTT_PRIMARY_PIN);
            gpio_clear_pin(PTT_EXTERNAL_PORT, PTT_EXTERNAL_PIN);
            gpio_set_pin(PTT_ACC0_PORT,       PTT_ACC0_PIN);
            break;
        case 4:  /* Special: PE12 low, PB0 low, PE13 HIGH */
            gpio_clear_pin(PTT_EXTERNAL_PORT, PTT_EXTERNAL_PIN);
            gpio_clear_pin(PTT_ACC0_PORT,     PTT_ACC0_PIN);
            gpio_set_pin(PTT_PRIMARY_PORT,    PTT_PRIMARY_PIN);
            break;
        default: /* Sub-modes 1,2,3: PE13 low, PB0 low, PE12 low */
            gpio_clear_pin(PTT_PRIMARY_PORT,  PTT_PRIMARY_PIN);
            gpio_clear_pin(PTT_ACC0_PORT,     PTT_ACC0_PIN);
            gpio_clear_pin(PTT_EXTERNAL_PORT, PTT_EXTERNAL_PIN);
            break;
        }
        /* Deassert audio group for RX */
        gpio_set_pin(PTT_MIC_PORT,     PTT_MIC_PIN);
        gpio_set_pin(PTT_ACC1_PORT,    PTT_ACC1_PIN);
        gpio_set_pin(PTT_PA_GATE_PORT, PTT_PA_GATE_PIN);
        break;

    /* Mode 2 (@ 0x0801D338): Audio path selection */
    case 2:
        switch (sub_mode) {
        case 0:  /* VHF */
        case 2:  /* 1.25m */
            gpio_clear_pin(PTT_ACC1_PORT,    PTT_ACC1_PIN);
            gpio_set_pin(PTT_MIC_PORT,       PTT_MIC_PIN);
            gpio_clear_pin(PTT_PA_GATE_PORT, PTT_PA_GATE_PIN);
            break;
        case 4:  /* Special: PA off */
            gpio_clear_pin(PTT_MIC_PORT,     PTT_MIC_PIN);
            gpio_clear_pin(PTT_ACC1_PORT,    PTT_ACC1_PIN);
            gpio_set_pin(PTT_PA_GATE_PORT,   PTT_PA_GATE_PIN);
            break;
        default: /* Sub-modes 1,3: UHF / 300MHz */
            gpio_clear_pin(PTT_MIC_PORT,     PTT_MIC_PIN);
            gpio_set_pin(PTT_ACC1_PORT,      PTT_ACC1_PIN);
            gpio_clear_pin(PTT_PA_GATE_PORT, PTT_PA_GATE_PIN);
            break;
        }
        break;

    /* Mode 3 (@ 0x0801D3C6): Antenna path config (fixed) */
    case 3:
        gpio_clear_pin(PTT_PRIMARY_PORT,  PTT_PRIMARY_PIN);
        gpio_clear_pin(PTT_EXTERNAL_PORT, PTT_EXTERNAL_PIN);
        gpio_clear_pin(PTT_ACC0_PORT,     PTT_ACC0_PIN);
        break;
    }
}

/* Full deassert: all relay pins HIGH (idle/RX) */
static void ptt_deassert(void)
{
    /* PA off first, then deassert all relay pins HIGH (reverse of assert) */
    gpio_set_pin(PTT_PA_GATE_PORT,  PTT_PA_GATE_PIN);
    gpio_set_pin(PTT_ACC0_PORT,     PTT_ACC0_PIN);
    gpio_set_pin(PTT_EXTERNAL_PORT, PTT_EXTERNAL_PIN);
    gpio_set_pin(PTT_ACC1_PORT,     PTT_ACC1_PIN);
    gpio_set_pin(PTT_MIC_PORT,      PTT_MIC_PIN);
    gpio_set_pin(PTT_PRIMARY_PORT,  PTT_PRIMARY_PIN);
}

/* Public API ----------------------------------------------------------- */

void radio_init(void)
{
    /*
     * Save the return address from LR register into a GLOBAL variable.
     * LR still holds the caller's return address after PUSH {LR} on
     * Cortex-M (PUSH stores but doesn't clear LR).  We save to a global
     * so it survives even if our stack frame is corrupted.
     */
    static volatile uint32_t saved_lr;
    __asm volatile ("mov %0, lr" : "=r"(saved_lr));

    /* Configure BK4829 SPI pins on GPIOE */
    gpio_config_pin(BK4829_SEN1_PORT, BK4829_SEN1_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(BK4829_SEN2_PORT, BK4829_SEN2_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(BK4829_SCK_PORT, BK4829_SCK_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(BK4829_SDA_PORT, BK4829_SDA_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);

    /* CS lines idle high */
    gpio_set_pin(BK4829_SEN1_PORT, BK4829_SEN1_PIN);
    gpio_set_pin(BK4829_SEN2_PORT, BK4829_SEN2_PIN);

    /*
     * Configure all 6 relay output pins (V0.27 @ 0x0801D268).
     * All are push-pull outputs, idle HIGH (relays off / RX mode).
     */
    gpio_config_pin(PTT_PRIMARY_PORT,  PTT_PRIMARY_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(PTT_PA_GATE_PORT,  PTT_PA_GATE_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(PTT_EXTERNAL_PORT, PTT_EXTERNAL_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(PTT_MIC_PORT,      PTT_MIC_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(PTT_ACC0_PORT,     PTT_ACC0_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(PTT_ACC1_PORT,     PTT_ACC1_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);

    /*
     * Configure RF frontend enable GPIOs (V0.27 @ 0x08022E3C).
     * PB8 and PE4 are always toggled together as a paired RF
     * frontend enable. Default to disabled (both LOW).
     */
    gpio_config_pin(RF_FRONTEND_EN0_PORT, RF_FRONTEND_EN0_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(RF_FRONTEND_EN1_PORT, RF_FRONTEND_EN1_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    rf_frontend_disable();

    /* Start in RX mode - all relays deasserted (HIGH) */
    ptt_deassert();
    transmitting = 0;

    /* Initialize dual VFO system (inits both BK4829 chips and applies settings) */
    vfo_init();

    /*
     * Stack corruption detection: compare the return address currently
     * on the stack with what we captured at entry.  If they differ,
     * something inside vfo_init (or an ISR) corrupted our stack frame.
     */
    {
        uint32_t ra_now = (uint32_t)__builtin_return_address(0);
        if (ra_now != saved_lr) {
            dbg_puts("[RI] *** STACK CORRUPTED ***\n");
            dbg_reg("[RI]   expected=0x", saved_lr);
            dbg_reg("[RI]   got=0x", ra_now);
            /* Dump 8 words from current SP to see damage pattern */
            uint32_t sp;
            __asm volatile ("mov %0, sp" : "=r"(sp));
            dbg_reg("[RI]   SP=0x", sp);
            for (int i = 0; i < 8; i++)
                dbg_reg("[RI]   stk=0x", ((volatile uint32_t *)sp)[i]);
            /* Attempt to fix the return address so we don't crash */
            /* (can't easily - just report and hope tail call saves us) */
        } else {
            dbg_puts("[RI] LR OK\n");
        }
    }

    dbg_puts("[DBG] radio_init done\n");
}

int radio_is_transmitting(void)
{
    return transmitting;
}

void radio_set_frequency(radio_vfo_t v, uint32_t freq_hz)
{
    if (v > RADIO_VFO_C) return;

    /* Ensure RF frontend is enabled */
    select_band_for_freq(vfo_get_state(v)->chip, freq_hz);

    /* Delegate to VFO module (updates state + programs BK4829) */
    vfo_set_frequency(v, freq_hz);
}

void radio_set_modulation(radio_vfo_t v, radio_mod_t mod)
{
    if (v > RADIO_VFO_C) return;

    const vfo_state_t *vs = vfo_get_state(v);
    uint8_t  chip  = vs->chip;
    uint16_t reg33 = bk4829_read_reg(chip, 0x33);

    /* Bits [15:14] of register 0x33 select demodulation mode */
    reg33 &= ~0xC000u;
    switch (mod) {
    case RADIO_MOD_FM:  /* 00 = FM */                    break;
    case RADIO_MOD_AM:  reg33 |= 0x4000u; /* 01 = AM */ break;
    case RADIO_MOD_USB: reg33 |= 0x8000u; /* 10 = USB */ break;
    case RADIO_MOD_LSB: reg33 |= 0xC000u; /* 11 = LSB */ break;
    }
    bk4829_write_reg(chip, 0x33, reg33);

    /* Set full RF mode: AM vs FM changes REG_47/48/70 */
    bk4829_mode_t rf_mode = (mod == RADIO_MOD_AM) ? BK4829_MODE_AM_RX
                                                   : BK4829_MODE_RX;
    bk4829_set_mode(chip, rf_mode, vs->rf_cal);

    /* Adjust IF filter bandwidth for the demodulation mode */
    /*
     * NOTE: BK4829 does NOT use simple single-value bandwidth registers.
     * The OEM binary writes a fixed value 0x07C0 and does not switch
     * per-channel. These values need verification against the OEM binary.
     *
     * Init default: REG_1C = 0x07C0 (confirmed in init table).
     * For now, leave the init default in place - it provides reasonable
     * receive audio bandwidth without risking unknown coefficient writes.
     */
    (void)mod; /* Bandwidth currently set by init table (0x07C0) */
}

void radio_ptt_on(void)
{
    if (transmitting) return;

    const vfo_state_t *vs = vfo_get_state(vfo_get_active());
    uint32_t tx_freq = vfo_get_tx_freq(vfo_get_active());
    radio_vfo_t txv = vfo_get_active();

    /* Satellite mode: Doppler-corrected uplink, possibly on the VFO B chip */
    {
        int sat = sat_ptt_prepare(&txv, &tx_freq);
        if (sat < 0)
            return;                     /* RX-only satellite: never TX */
        if (sat > 0)
            vs = vfo_get_state(txv);
    }

    /* BCL: refuse TX if channel is busy (squelch open = signal present) */
    if (vs->busy_lockout && bk4829_is_squelch_open(vs->chip)) {
        sat_ptt_release();
        return;
    }

    /* Refuse TX outside calibrated band edges */
    if (!calibration_tx_allowed(&cal_data, tx_freq)) {
        sat_ptt_release();
        return;
    }
    tx_vfo_used = txv;

    power_reset_idle_timer();
    transmitting = 1;
    tot_seconds = 0;
    tot_warned = 0;

    /* Apply TX offset frequency if different from RX */
    if (tx_freq != vs->freq_hz)
        bk4829_set_frequency(vs->chip, tx_freq);

    /* Set TX power before keying up (REG_36) */
    bk4829_set_tx_power(vs->chip, vs->tx_power);

    /*
     * V0.27 full TX setup path (@ 0x0801A5D0):
     *   1. relay_select(sub_mode, 0) - assert ALL relay GPIOs for TX
     *   2. BK4829 TX setup (REG_37, REG_47, tune, power, REG_30=0xC1FE)
     *   3. relay_select(sub_mode, 2) - configure audio path for band
     *   4. rf_frontend_enable() - PA power LAST (after relays settled)
     * Key: relays BEFORE RF enable. PA enable AFTER RF path is set.
     */
    uint8_t sub = freq_to_submode(tx_freq);
    relay_select(sub, 0);
    delay_ms(5);  /* relay mechanical settling (1-5 ms typical) */
    bk4829_set_mode(vs->chip, BK4829_MODE_TX, vs->rf_cal);
    relay_select(sub, 2);
    rf_frontend_enable();

    /* PTT-ID: send DTMF at Beginning Of Transmission */
    {
        const settings_t *s = settings_get();
        if (s->ptt_id == 1 || s->ptt_id == 3) { /* BOT or BOTH */
            uint16_t id_delay = ((uint16_t)s->send_id_delay + 1) * 100;
            delay_ms(id_delay);
            dtmf_send_ptt_id(vs->chip);
        }
    }
}

void radio_ptt_off(void)
{
    if (!transmitting) return;

    const vfo_state_t *vs = vfo_get_state(tx_vfo_used);
    const settings_t  *s  = settings_get();

    /* PTT-ID: send DTMF at End Of Transmission */
    if (s->ptt_id == 2 || s->ptt_id == 3) { /* EOT or BOTH */
        dtmf_send_ptt_id(vs->chip);
        uint16_t id_delay = ((uint16_t)s->send_id_delay + 1) * 100;
        delay_ms(id_delay);
    }

    /*
     * Roger beep (R-Tone) - brief tone over the air at end of TX.
     * Uses the BK4829 CTCSS TX path (REG_51/07) to generate the tone
     * through the PA so the receiving station hears it.
     */
    if (s->r_tone >= 1 && s->r_tone <= 4) {
        static const uint16_t roger_freq_x10[4] = { 10000, 14500, 17500, 21000 };
        uint16_t fx10 = roger_freq_x10[s->r_tone - 1];
        uint16_t fw = (uint16_t)((uint32_t)fx10 * 2065U / 10000U);
        bk4829_write_reg(vs->chip, 0x51, 0x904A);
        bk4829_write_reg(vs->chip, 0x07, fw & 0x1FFF);
        delay_ms(200);
        bk4829_write_reg(vs->chip, 0x51, 0x0000);
    }

    /*
     * STE (Squelch Tail Elimination) - if enabled and the VFO has a
     * sub-audio tone active (CTCSS or DCS), briefly send 134.4 Hz
     * before dropping the carrier.  This tells the receiver to mute
     * its speaker, preventing the squelch tail noise burst.
     */
    if (s->tail_elim) {
        uint8_t has_subtone = (vs->ctcss_tx_idx != 0xFF) ||
                              (vs->dcs_code_idx != 0xFF);
        if (has_subtone) {
            bk4829_send_tail_tone(vs->chip);
            delay_ms(55);
            bk4829_disable_ctcss(vs->chip);
        }
    }

    /*
     * V0.27 TX-off sequence (@ 0x0801C308):
     *   1. relay_select(sub_mode, 1) - RX band/path config
     *   2. Both chips: REG_37 = 0x9D1F (idle freq setting)
     *   3. Active chip: REG_30 = 0xBFF1 (RX enable)
     * Key: relays BEFORE RF mode change. No explicit delay.
     */
    uint8_t sub = freq_to_submode(vs->freq_hz);
    relay_select(sub, 1);
    bk4829_set_mode(vs->chip, BK4829_MODE_RX, vs->rf_cal);

    /* Restore RX frequency if TX offset was active */
    if (vs->offset_dir != 0 && vs->offset_freq_hz != 0)
        bk4829_set_frequency(vs->chip, vs->freq_hz);

    transmitting = 0;
    tot_seconds = 0;

    /* Satellite mode: restore Doppler-tracked RX on both chips */
    sat_ptt_release();
}

/* ========================================================================
 *  radio_tot_poll - Call at 1 Hz while transmitting.
 *
 *  Increments TX elapsed timer.  When within 10 s of the limit, sets
 *  tot_warned (caller can use for audible/visual warning).  When the
 *  limit is reached, forces PTT off.
 * ======================================================================== */

void radio_tot_poll(void)
{
    if (!transmitting)
        return;

    const settings_t *s = settings_get();
    uint8_t idx = s->tot;
    if (idx == 0 || idx > 8)
        return; /* TOT disabled */

    uint16_t limit = tot_table[idx];
    tot_seconds++;

    /* Warning zone: 10 seconds before cutoff */
    if (!tot_warned && limit >= 10 && tot_seconds >= (limit - 10))
        tot_warned = 1;

    /* Hard cutoff */
    if (tot_seconds >= limit)
        radio_ptt_off();
}

uint8_t radio_tot_warning_active(void)
{
    return transmitting && tot_warned;
}

/* ========================================================================
 *  Dual-watch - alternate RSSI monitoring on both VFOs.
 *
 *  When enabled and not transmitting, periodically reads RSSI on the
 *  inactive VFO.  If squelch opens (signal detected), temporarily
 *  switches RX focus so the user hears the incoming signal.  When
 *  the carrier drops, focus returns to the primary VFO.
 * ======================================================================== */

static radio_vfo_t dw_rx_vfo;         /* VFO currently receiving audio */
static uint8_t     dw_signal_hold;    /* countdown: hold focus after detect */

radio_vfo_t radio_get_rx_vfo(void)
{
    return dw_rx_vfo;
}

void radio_dual_watch_poll(void)
{
    if (transmitting) return;

    const settings_t *s = settings_get();
    if (!s->dual_watch) {
        dw_rx_vfo = vfo_get_active();
        return;
    }

    /* While holding on a detected signal, count down */
    if (dw_signal_hold > 0) {
        radio_vfo_t alt = (vfo_get_active() == RADIO_VFO_A)
                              ? RADIO_VFO_B : RADIO_VFO_A;
        const vfo_state_t *vs_alt = vfo_get_state(alt);
        if (bk4829_is_squelch_open(vs_alt->chip)) {
            dw_signal_hold = 10;  /* reset hold while signal persists */
        } else {
            dw_signal_hold--;
            if (dw_signal_hold == 0) {
                dw_rx_vfo = vfo_get_active();  /* return to primary */
            }
        }
        return;
    }

    /* Check alternate VFO for signal */
    radio_vfo_t alt = (vfo_get_active() == RADIO_VFO_A)
                          ? RADIO_VFO_B : RADIO_VFO_A;
    const vfo_state_t *vs_alt = vfo_get_state(alt);

    if (bk4829_is_squelch_open(vs_alt->chip)) {
        dw_rx_vfo = alt;
        dw_signal_hold = 10;  /* ~2 s hold at 5 Hz poll rate */
    } else {
        dw_rx_vfo = vfo_get_active();
    }
}

/* UI event handlers ------------------------------------------------- */

/* ===========================================================================
 *  Programmable function key dispatch
 *
 *  Function codes (from CPS PF1/PF2 settings):
 *    0 = None, 1 = Scan, 2 = FM Radio, 3 = Monitor (squelch open),
 *    4 = Flashlight, 5 = Reverse, 6 = Mem Scan, 7 = Alarm
 * ===========================================================================*/

static void action_dispatch(uint8_t func_code)
{
    switch (func_code) {
    case 1:  /* Scan */
        scanner_start(SCAN_VFO);
        break;
    case 2:  /* FM Radio */
        if (fm_radio_get_state() != FM_STATE_OFF)
            fm_radio_exit();
        else
            fm_radio_enter();
        break;
    case 3:  /* Monitor - momentary squelch open */
        bk4829_set_squelch(vfo_get_state(vfo_get_active())->chip, 0);
        break;
    case 4:  /* Flashlight (backlight toggle) */
        power_backlight_toggle();
        break;
    case 5:  /* Reverse */
        vfo_reverse_toggle(vfo_get_active());
        vfo_apply(vfo_get_active());
        break;
    case 6:  /* Memory Scan */
        scanner_start(SCAN_MEMORY);
        break;
    case 7:  /* AM/SW Radio */
        if (am_radio_get_state() != AM_STATE_OFF)
            am_radio_exit();
        else
            am_radio_enter(AM_BAND_MW);
        break;
    case 9:  /* APRS messages (custom firmware) */
        if (aprs_msg_ui_is_active())
            aprs_msg_ui_exit();
        else
            aprs_msg_ui_enter();
        break;
    case 8:  /* Satellite mode (custom firmware) */
        if (sat_ui_is_active())
            sat_ui_exit();
        else
            sat_ui_enter();
        break;
    default:
        break;
    }
}

/* Key handler ---------------------------------------------------------- */

void radio_handle_key(uint8_t key, uint8_t event_type)
{
    uint8_t digit;

    /* Priority 1: Menu active -> delegate all keys */
    if (menu_get_state() != MENU_STATE_CLOSED) {
        if (event_type == KEY_EVT_PRESS)
            menu_handle_key(key);
        return;
    }

    /* Priority 1a: APRS message screens -> all keys except PTT */
    if (aprs_msg_ui_is_active() && key != KEY_PTT) {
        if (event_type == KEY_EVT_PRESS)
            aprs_msg_ui_handle_key(key);
        return;
    }

    /* Priority 1b: Satellite screens -> all keys except PTT */
    if (sat_ui_is_active() && key != KEY_PTT) {
        if (event_type == KEY_EVT_PRESS)
            sat_ui_handle_key(key);
        return;
    }

    /* Priority 2: Frequency entry active -> digit / confirm / cancel */
    if (freq_entry_is_active()) {
        if (event_type != KEY_EVT_PRESS)
            return;
        digit = key_to_digit(key);
        if (digit != 0xFF) {
            if (freq_entry_digit(digit))
                freq_entry_confirm();
        } else if (key == KEY_HASH) {
            freq_entry_confirm();
        } else if (key == KEY_STAR) {
            freq_entry_backspace();
        } else if (key == KEY_D_BAND) {
            freq_entry_cancel();
        }
        return;
    }

    /* Priority 3: FM radio active -> delegate */
    if (fm_radio_get_state() != FM_STATE_OFF) {
        if (event_type == KEY_EVT_PRESS)
            fm_radio_handle_key(key);
        return;
    }

    /* Priority 4: Scanner active -> any key stops */
    if (scanner_get_mode() != SCAN_OFF) {
        if (event_type == KEY_EVT_PRESS)
            scanner_stop();
        return;
    }

    /* Priority 5: Normal mode ----------------------------------------- */

    /* PTT press / release */
    if (key == KEY_PTT) {
        if (event_type == KEY_EVT_PRESS)
            radio_ptt_on();
        else if (event_type == KEY_EVT_RELEASE)
            radio_ptt_off();
        return;
    }

    /* All remaining actions trigger on press only */
    if (event_type == KEY_EVT_REPEAT) {
        /* Long press on PF keys -> dispatch long function */
        const settings_t *s = settings_get();
        if (key == KEY_D_BAND && !sat_ui_is_active()) {
            sat_ui_enter();             /* long-press D: satellite mode */
            return;
        }
        if (key == KEY_SIDE1)
            action_dispatch(s->pf1_long);
        else if (key == KEY_SIDE2)
            action_dispatch(s->pf2_long);
        /* Long press on digit keys -> programmable function macro */
        digit = key_to_digit(key);
        if (digit != 0xFF && digit < 10)
            action_dispatch(s->key_long[digit]);
        return;
    }

    if (event_type != KEY_EVT_PRESS)
        return;

    /* DTMF digits while transmitting */
    if (transmitting) {
        handle_dtmf_key(key);
        return;
    }

    /* Digit keys 0-9 -> start frequency direct entry */
    digit = key_to_digit(key);
    if (digit != 0xFF) {
        freq_entry_start();
        freq_entry_digit(digit);
        return;
    }

    switch (key) {
    case KEY_C_MENU:                        /* Menu */
        menu_open();
        break;
    case KEY_A_VFO:                         /* VFO A<->B toggle */
        vfo_toggle();
        vfo_apply(vfo_get_active());
        break;
    case KEY_HASH:                          /* Up */
        vfo_step(+1);
        vfo_apply(vfo_get_active());
        break;
    case KEY_STAR:                          /* Down */
        vfo_step(-1);
        vfo_apply(vfo_get_active());
        break;
    case KEY_B_SCAN:                        /* Start VFO scan */
        scanner_start(SCAN_VFO);
        break;
    case KEY_SIDE1:                         /* PF1: configurable function */
    {
        const settings_t *s = settings_get();
        action_dispatch(s->pf1_short);
        break;
    }
    case KEY_SIDE2:                         /* PF2: configurable function */
    {
        const settings_t *s = settings_get();
        action_dispatch(s->pf2_short);
        break;
    }
    case KEY_D_BAND:                        /* Exit/Back - no-op */
    default:
        break;
    }
}

void radio_handle_encoder(int8_t direction)
{
    if (!direction)
        return;

    /* Menu active -> scroll / adjust */
    if (menu_get_state() != MENU_STATE_CLOSED) {
        menu_handle_encoder(direction);
        return;
    }

    /* APRS message screens */
    if (aprs_msg_ui_is_active()) {
        aprs_msg_ui_handle_encoder(direction);
        return;
    }

    /* Satellite screens: scroll / RX trim */
    if (sat_ui_is_active()) {
        sat_ui_handle_encoder(direction);
        return;
    }

    /* Ignore encoder during frequency entry */
    if (freq_entry_is_active())
        return;

    /* FM radio active -> tune FM band */
    if (fm_radio_get_state() != FM_STATE_OFF) {
        fm_radio_handle_encoder(direction);
        return;
    }

    /* Ignore encoder during scan */
    if (scanner_get_mode() != SCAN_OFF)
        return;

    /* Normal mode -> tune active VFO */
    vfo_step(direction);
    vfo_apply(vfo_get_active());
}

/* ===========================================================================
 *  DTMF RX decode - accumulate incoming digits during receive
 *
 *  Poll at ~50 Hz.  Reads BK4829 DTMF detector on the active RX VFO.
 *  Buffer clears on TX start or after 3 s with no new digits.
 * ===========================================================================*/

extern uint32_t get_tick(void);

#define DTMF_DEC_MAX    16   /* max decoded digits to display */
#define DTMF_DEC_TIMEOUT_MS 3000

static char     dtmf_dec_buf[DTMF_DEC_MAX + 1];
static uint8_t  dtmf_dec_len;
static uint32_t dtmf_dec_last_ms;   /* tick of last decoded digit */

void radio_dtmf_decode_poll(void)
{
    if (transmitting) {
        dtmf_dec_len = 0;
        dtmf_dec_buf[0] = '\0';
        return;
    }

    /* Timeout - clear buffer after inactivity */
    if (dtmf_dec_len > 0 &&
        (get_tick() - dtmf_dec_last_ms) >= DTMF_DEC_TIMEOUT_MS) {
        dtmf_dec_len = 0;
        dtmf_dec_buf[0] = '\0';
    }

    /* Poll active RX VFO's chip */
    radio_vfo_t rx = radio_get_rx_vfo();
    const vfo_state_t *vfo = vfo_get_state(rx);
    char ch = dtmf_decode_poll(vfo->chip);

    if (ch != '\0' && dtmf_dec_len < DTMF_DEC_MAX) {
        dtmf_dec_buf[dtmf_dec_len++] = ch;
        dtmf_dec_buf[dtmf_dec_len] = '\0';
        dtmf_dec_last_ms = get_tick();
    }
}

const char *radio_dtmf_decode_buf(void)
{
    return dtmf_dec_buf;
}
