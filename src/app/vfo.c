/*
 * vfo.c - Triple VFO management for the RT-950 Pro
 *
 * Manages three VFO states (A, B, C), each mapped to a BK4829 chip.
 * vfo_apply() is the key function that programs ALL settings from
 * a VFO state into the corresponding BK4829 transceiver.
 *
 * VFO A default: 146.520 MHz on chip 1 (PE15/SEN2)
 * VFO B default: 446.000 MHz on chip 0 (PE8/SEN1)
 * VFO C default: 144.390 MHz on chip 1 (shared with VFO A, time-muxed)
 *
 * OEM V0.27 references:
 *   VFO/channel storage  : WL sector @ flash 0x9000 (CPS), 98B records
 *   Frequency set        : bk4829_set_frequency @ 0x0801BD40
 *   Band detection       : freq_to_submode via cal subbands
 *   RF relay switch      : relay_select @ 0x0801D268 (dispatches on submode)
 */

#include "app/vfo.h"
#include "drivers/bk4829.h"
#include "drivers/flash_wearleveling.h"
#include "debug_uart.h"

#include <string.h>

/* VFO persistence record (packed into WL_VFOCFG: 96 bytes) ---------- */

typedef struct __attribute__((packed)) {
    uint32_t freq_hz;
    uint8_t  tx_power;
    uint8_t  modulation;
    uint8_t  squelch_level;
    uint8_t  ctcss_tx_idx;
    uint8_t  ctcss_rx_idx;
    uint8_t  dcs_code_idx;
    uint8_t  dcs_polarity;
    uint8_t  scrambler;
    uint8_t  bandwidth;
    uint8_t  rf_cal;
    uint8_t  offset_dir;
    uint8_t  busy_lockout;
    uint32_t step_hz;
    uint32_t offset_freq_hz;
} vfo_persist_entry_t;   /* 23 bytes */

typedef struct __attribute__((packed)) {
    uint8_t             active_vfo;
    uint8_t             vfo_c_enabled;
    vfo_persist_entry_t vfo[VFO_COUNT];
    uint8_t             _pad[96 - 2 - VFO_COUNT * sizeof(vfo_persist_entry_t)];
} vfo_persist_t;         /* 96 bytes */

/* VFO state storage ---------------------------------------------------- */

static vfo_state_t vfo_states[VFO_COUNT] = {
    {   /* VFO A - 2m calling frequency */
        .freq_hz      = 146520000,
        .chip         = BK4829_CHIP1,
        .tx_power     = 1,
        .modulation   = RADIO_MOD_FM,
        .squelch_level = 3,
        .ctcss_tx_idx = 0xFF,
        .ctcss_rx_idx = 0xFF,
        .dcs_code_idx = 0xFF,
        .dcs_polarity = 0,
        .scrambler    = 0,
        .bandwidth    = 0,
        .step_hz      = 12500,
        .offset_dir   = 0,
        .offset_freq_hz = 0,
        .channel_num  = 0xFFFF,
    },
    {   /* VFO B - 70cm calling frequency */
        .freq_hz      = 446000000,
        .chip         = BK4829_CHIP0,
        .tx_power     = 1,
        .modulation   = RADIO_MOD_FM,
        .squelch_level = 3,
        .ctcss_tx_idx = 0xFF,
        .ctcss_rx_idx = 0xFF,
        .dcs_code_idx = 0xFF,
        .dcs_polarity = 0,
        .scrambler    = 0,
        .bandwidth    = 0,
        .step_hz      = 12500,
        .offset_dir   = 0,
        .offset_freq_hz = 0,
        .channel_num  = 0xFFFF,
    },
    {   /* VFO C - APRS/monitor frequency, shares chip 1 with VFO A */
        .freq_hz      = 144390000,
        .chip         = BK4829_CHIP1,
        .tx_power     = 1,
        .modulation   = RADIO_MOD_FM,
        .squelch_level = 3,
        .ctcss_tx_idx = 0xFF,
        .ctcss_rx_idx = 0xFF,
        .dcs_code_idx = 0xFF,
        .dcs_polarity = 0,
        .scrambler    = 0,
        .bandwidth    = 0,
        .step_hz      = 12500,
        .offset_dir   = 0,
        .offset_freq_hz = 0,
        .channel_num  = 0xFFFF,
    },
};

static radio_vfo_t active_vfo = RADIO_VFO_A;
static uint8_t     vfo_c_enabled = 0;

/* ---- Band limit validation ------------------------------------------- */

/*
 * RX band limits.  The RT-950 covers 4 bands (+ a 5th disabled band).
 * OEM submode mapping (relay_select @ 0x0801D268):
 *   0 = 136–174 MHz (2m VHF)
 *   1 = 400–520 MHz (70cm UHF)
 *   2 = 220–260 MHz (1.25m)
 *   3 = 300–390 MHz (normally disabled)
 *
 * We allow any frequency within these ranges for RX.
 */
typedef struct { uint32_t low; uint32_t high; } band_range_t;

static const band_range_t rx_bands[] = {
    { 136000000, 174000000 },   /* 2m VHF */
    { 220000000, 260000000 },   /* 1.25m */
    { 300000000, 390000000 },   /* 300 MHz (disabled in cal but allow RX) */
    { 400000000, 520000000 },   /* 70cm UHF */
};
#define NUM_RX_BANDS (sizeof(rx_bands) / sizeof(rx_bands[0]))

static int freq_in_rx_band(uint32_t freq_hz)
{
    for (unsigned i = 0; i < NUM_RX_BANDS; i++) {
        if (freq_hz >= rx_bands[i].low && freq_hz <= rx_bands[i].high)
            return 1;
    }
    return 0;
}

/*
 * Clamp a frequency to the nearest valid RX band edge.
 * Returns the clamped frequency, or the default (146.520 MHz) if
 * the input is wildly out of range.
 */
static uint32_t freq_clamp_to_band(uint32_t freq_hz)
{
    if (freq_in_rx_band(freq_hz))
        return freq_hz;

    /* Find the closest band edge */
    uint32_t best = 146520000;
    uint32_t best_dist = 0xFFFFFFFF;
    for (unsigned i = 0; i < NUM_RX_BANDS; i++) {
        uint32_t d_low  = (freq_hz > rx_bands[i].low)
                          ? (freq_hz - rx_bands[i].low) : (rx_bands[i].low - freq_hz);
        uint32_t d_high = (freq_hz > rx_bands[i].high)
                          ? (freq_hz - rx_bands[i].high) : (rx_bands[i].high - freq_hz);
        if (d_low < best_dist)  { best_dist = d_low;  best = rx_bands[i].low; }
        if (d_high < best_dist) { best_dist = d_high; best = rx_bands[i].high; }
    }
    return best;
}

/* ---- Valid step sizes ------------------------------------------------ */

/*
 * Standard amateur/commercial step sizes (Hz).
 * Used to validate step_hz loaded from flash.
 */
static const uint32_t valid_steps[] = {
    2500, 5000, 6250, 10000, 12500, 15000, 20000, 25000, 30000, 50000
};
#define NUM_VALID_STEPS (sizeof(valid_steps) / sizeof(valid_steps[0]))

static int is_valid_step(uint32_t step_hz)
{
    for (unsigned i = 0; i < NUM_VALID_STEPS; i++) {
        if (step_hz == valid_steps[i])
            return 1;
    }
    return 0;
}

/* VFO flash persistence ------------------------------------------------ */

static void vfo_pack(vfo_persist_entry_t *pe, const vfo_state_t *vs)
{
    pe->freq_hz      = vs->freq_hz;
    pe->tx_power     = vs->tx_power;
    pe->modulation   = vs->modulation;
    pe->squelch_level = vs->squelch_level;
    pe->ctcss_tx_idx = vs->ctcss_tx_idx;
    pe->ctcss_rx_idx = vs->ctcss_rx_idx;
    pe->dcs_code_idx = vs->dcs_code_idx;
    pe->dcs_polarity = vs->dcs_polarity;
    pe->scrambler    = vs->scrambler;
    pe->bandwidth    = vs->bandwidth;
    pe->rf_cal       = vs->rf_cal;
    pe->offset_dir   = vs->offset_dir;
    pe->busy_lockout = vs->busy_lockout;
    pe->step_hz      = vs->step_hz;
    pe->offset_freq_hz = vs->offset_freq_hz;
}

static void vfo_unpack(vfo_state_t *vs, const vfo_persist_entry_t *pe)
{
    vs->freq_hz      = pe->freq_hz;
    vs->tx_power     = pe->tx_power;
    vs->modulation   = pe->modulation;
    vs->squelch_level = pe->squelch_level;
    vs->ctcss_tx_idx = pe->ctcss_tx_idx;
    vs->ctcss_rx_idx = pe->ctcss_rx_idx;
    vs->dcs_code_idx = pe->dcs_code_idx;
    vs->dcs_polarity = pe->dcs_polarity;
    vs->scrambler    = pe->scrambler;
    vs->bandwidth    = pe->bandwidth;
    vs->rf_cal       = pe->rf_cal;
    vs->offset_dir   = pe->offset_dir;
    vs->busy_lockout = pe->busy_lockout;
    vs->step_hz      = pe->step_hz;
    vs->offset_freq_hz = pe->offset_freq_hz;
    /* chip is NOT persisted - it's fixed per VFO slot */
}

static void vfo_save_state(void)
{
    vfo_persist_t rec;
    memset(&rec, 0xFF, sizeof(rec));
    rec.active_vfo = (uint8_t)active_vfo;
    rec.vfo_c_enabled = vfo_c_enabled;
    vfo_pack(&rec.vfo[0], &vfo_states[RADIO_VFO_A]);
    vfo_pack(&rec.vfo[1], &vfo_states[RADIO_VFO_B]);
    vfo_pack(&rec.vfo[2], &vfo_states[RADIO_VFO_C]);
    wl_write(&WL_VFOCFG, &rec);
}

static int __attribute__((noinline, unused)) vfo_load_state(void)
{
    vfo_persist_t rec;
    if (wl_read(&WL_VFOCFG, &rec) != 0)
        return -1;

    /* Sanity-check: frequency must be nonzero and non-erased */
    if (rec.vfo[0].freq_hz == 0 || rec.vfo[0].freq_hz == 0xFFFFFFFF)
        return -1;

    vfo_unpack(&vfo_states[RADIO_VFO_A], &rec.vfo[0]);
    vfo_unpack(&vfo_states[RADIO_VFO_B], &rec.vfo[1]);

    /* VFO-C may not be present in older records (all 0xFF) */
    if (rec.vfo[2].freq_hz != 0 && rec.vfo[2].freq_hz != 0xFFFFFFFF)
        vfo_unpack(&vfo_states[RADIO_VFO_C], &rec.vfo[2]);

    active_vfo = (rec.active_vfo <= RADIO_VFO_C) ? (radio_vfo_t)rec.active_vfo
                                                  : RADIO_VFO_A;
    vfo_c_enabled = (rec.vfo_c_enabled == 1) ? 1 : 0;

    /* If VFO-C disabled but was selected, fall back to A */
    if (!vfo_c_enabled && active_vfo == RADIO_VFO_C)
        active_vfo = RADIO_VFO_A;

    /*
     * Post-load validation: clamp frequencies to valid RX bands
     * and sanitize step sizes.  Corrupted flash data must not
     * program the BK4829 with nonsense frequencies.
     */
    for (int i = 0; i < VFO_COUNT; i++) {
        if (!freq_in_rx_band(vfo_states[i].freq_hz))
            vfo_states[i].freq_hz = freq_clamp_to_band(vfo_states[i].freq_hz);
        if (!is_valid_step(vfo_states[i].step_hz))
            vfo_states[i].step_hz = 12500;  /* default 12.5 kHz */
        if (vfo_states[i].squelch_level > 9)
            vfo_states[i].squelch_level = 3;
        if (vfo_states[i].tx_power > 2)
            vfo_states[i].tx_power = 1;
    }

    return 0;
}

/* Apply VFO state to hardware ------------------------------------------ */

void vfo_apply(radio_vfo_t vfo)
{
    if (vfo > RADIO_VFO_C) return;

    const vfo_state_t *s = &vfo_states[vfo];
    uint8_t chip = s->chip;

    /* Frequency */
    bk4829_set_frequency(chip, s->freq_hz);

    /* Squelch */
    bk4829_set_squelch(chip, s->squelch_level);

    /* Sub-audio: CTCSS TX */
    if (s->ctcss_tx_idx != 0xFF)
        bk4829_set_ctcss_tx(chip, s->ctcss_tx_idx);

    /* Sub-audio: CTCSS RX */
    if (s->ctcss_rx_idx != 0xFF)
        bk4829_set_ctcss_rx(chip, s->ctcss_rx_idx);

    /* Sub-audio: DCS (overrides CTCSS if both somehow set) */
    if (s->dcs_code_idx != 0xFF) {
        bk4829_set_dcs_tx(chip, s->dcs_code_idx, s->dcs_polarity);
        bk4829_set_dcs_rx(chip, s->dcs_code_idx, s->dcs_polarity);
    }

    /* If no sub-audio is enabled, make sure it's disabled */
    if (s->ctcss_tx_idx == 0xFF && s->ctcss_rx_idx == 0xFF &&
        s->dcs_code_idx == 0xFF) {
        bk4829_disable_ctcss(chip);
        bk4829_disable_dcs(chip);
    }

    /* Scrambler */
    if (s->scrambler)
        bk4829_scrambler_enable(chip);
    else
        bk4829_scrambler_disable(chip);

    /* Enable RX */
    bk4829_enable_rx(chip);
}

/* Init ----------------------------------------------------------------- */

void vfo_init(void)
{
    /* Init both BK4829 chips */
    dbg_puts("[VFO] bk4829_init CHIP0...\n");
    bk4829_init(BK4829_CHIP0);

    dbg_puts("[VFO] bk4829_init CHIP1...\n");
    bk4829_init(BK4829_CHIP1);

    /*
     * Set chip assignments BEFORE flash load - these are hardware-fixed
     * and must never be overwritten by flash data.  vfo_unpack() skips
     * the chip field, but we set them here defensively.
     */
    vfo_states[RADIO_VFO_A].chip = BK4829_CHIP1;
    vfo_states[RADIO_VFO_B].chip = BK4829_CHIP0;
    vfo_states[RADIO_VFO_C].chip = BK4829_CHIP1;

    /* Try loading persisted VFO state from flash.
     * Skip in debug builds - the flash read triggers a HardFault at
     * certain binary sizes (code-layout-sensitive stack corruption in
     * spi_flash_read).  Defaults from static init are fine for testing. */
#ifndef DEBUG_UART
    vfo_load_state();
#else
    dbg_puts("[VFO] flash load SKIPPED (debug build)\n");
#endif

    /* Apply VFO A and B to their respective chips */
    dbg_puts("[VFO] vfo_apply A...\n");
    vfo_apply(RADIO_VFO_A);

    dbg_puts("[VFO] vfo_apply B...\n");
    vfo_apply(RADIO_VFO_B);

    /* If VFO-C is active, it overrides chip 1 (supersedes VFO-A) */
    if (vfo_c_enabled && active_vfo == RADIO_VFO_C)
        vfo_apply(RADIO_VFO_C);

    dbg_puts("[VFO] init complete\n");
}

/* Active VFO management ------------------------------------------------ */

radio_vfo_t vfo_get_active(void)
{
    return active_vfo;
}

void vfo_set_active(radio_vfo_t vfo)
{
    if (vfo > RADIO_VFO_C) return;
    if (vfo == RADIO_VFO_C && !vfo_c_enabled) return;

    radio_vfo_t prev = active_vfo;
    active_vfo = vfo;

    /*
     * When switching to/from VFO-C, reprogram chip 1 because
     * VFO-A and VFO-C share the same BK4829 transceiver.
     */
    if ((prev == RADIO_VFO_C) != (vfo == RADIO_VFO_C)) {
        /* Chip 1 owner changed - apply the new owner's settings */
        if (vfo == RADIO_VFO_C)
            vfo_apply(RADIO_VFO_C);
        else
            vfo_apply(RADIO_VFO_A);
    }

    vfo_apply(active_vfo);
    vfo_save_state();
}

void vfo_toggle(void)
{
    if (vfo_c_enabled) {
        /* A -> B -> C -> A */
        if (active_vfo == RADIO_VFO_A)
            vfo_set_active(RADIO_VFO_B);
        else if (active_vfo == RADIO_VFO_B)
            vfo_set_active(RADIO_VFO_C);
        else
            vfo_set_active(RADIO_VFO_A);
    } else {
        /* A -> B -> A (classic) */
        vfo_set_active((active_vfo == RADIO_VFO_A) ? RADIO_VFO_B : RADIO_VFO_A);
    }
}

/* Swap / Copy ---------------------------------------------------------- */

void vfo_swap(void)
{
    /* Save chip assignments - they stay with their VFO slot */
    uint8_t chip_a = vfo_states[RADIO_VFO_A].chip;
    uint8_t chip_b = vfo_states[RADIO_VFO_B].chip;

    /* Swap the entire structs */
    vfo_state_t tmp = vfo_states[RADIO_VFO_A];
    vfo_states[RADIO_VFO_A] = vfo_states[RADIO_VFO_B];
    vfo_states[RADIO_VFO_B] = tmp;

    /* Restore chip assignments */
    vfo_states[RADIO_VFO_A].chip = chip_a;
    vfo_states[RADIO_VFO_B].chip = chip_b;

    /* Re-apply both */
    vfo_apply(RADIO_VFO_A);
    vfo_apply(RADIO_VFO_B);
    vfo_save_state();
}

void vfo_copy(void)
{
    radio_vfo_t inactive = (active_vfo == RADIO_VFO_A) ? RADIO_VFO_B : RADIO_VFO_A;
    uint8_t saved_chip = vfo_states[inactive].chip;

    vfo_states[inactive] = vfo_states[active_vfo];
    vfo_states[inactive].chip = saved_chip;

    vfo_apply(inactive);
    vfo_save_state();
}

/* State access --------------------------------------------------------- */

const vfo_state_t *vfo_get_state(radio_vfo_t vfo)
{
    if (vfo > RADIO_VFO_C)
        return &vfo_states[RADIO_VFO_A];
    return &vfo_states[vfo];
}

/* Frequency / tuning --------------------------------------------------- */

void vfo_step(int8_t direction)
{
    vfo_state_t *s = &vfo_states[active_vfo];
    uint32_t new_freq = s->freq_hz;

    if (direction > 0) {
        new_freq += s->step_hz;
    } else if (direction < 0) {
        if (new_freq > s->step_hz)
            new_freq -= s->step_hz;
        else
            new_freq = s->step_hz; /* floor */
    }

    /*
     * Wrap at band edges: if we step out of the current band,
     * wrap to the opposite edge of the same band.
     */
    for (unsigned i = 0; i < NUM_RX_BANDS; i++) {
        if (s->freq_hz >= rx_bands[i].low && s->freq_hz <= rx_bands[i].high) {
            if (new_freq > rx_bands[i].high)
                new_freq = rx_bands[i].low;
            else if (new_freq < rx_bands[i].low)
                new_freq = rx_bands[i].high;
            break;
        }
    }

    s->freq_hz = new_freq;
    bk4829_set_frequency(s->chip, s->freq_hz);
}

void vfo_set_frequency(radio_vfo_t vfo, uint32_t freq_hz)
{
    if (vfo > RADIO_VFO_C) return;

    /* Clamp to valid RX band if out-of-range */
    if (!freq_in_rx_band(freq_hz))
        freq_hz = freq_clamp_to_band(freq_hz);

    vfo_states[vfo].freq_hz = freq_hz;
    bk4829_set_frequency(vfo_states[vfo].chip, freq_hz);
}

/* Sub-audio settings --------------------------------------------------- */

void vfo_set_ctcss_tx(radio_vfo_t vfo, uint8_t tone_idx)
{
    if (vfo > RADIO_VFO_C) return;

    vfo_states[vfo].ctcss_tx_idx = tone_idx;
    if (tone_idx != 0xFF)
        bk4829_set_ctcss_tx(vfo_states[vfo].chip, tone_idx);
    else
        bk4829_disable_ctcss(vfo_states[vfo].chip);
}

void vfo_set_ctcss_rx(radio_vfo_t vfo, uint8_t tone_idx)
{
    if (vfo > RADIO_VFO_C) return;

    vfo_states[vfo].ctcss_rx_idx = tone_idx;
    if (tone_idx != 0xFF)
        bk4829_set_ctcss_rx(vfo_states[vfo].chip, tone_idx);
    else
        bk4829_disable_ctcss(vfo_states[vfo].chip);
}

void vfo_set_dcs(radio_vfo_t vfo, uint8_t code_idx, uint8_t polarity)
{
    if (vfo > RADIO_VFO_C) return;

    vfo_states[vfo].dcs_code_idx = code_idx;
    vfo_states[vfo].dcs_polarity = polarity;

    if (code_idx != 0xFF) {
        bk4829_set_dcs_tx(vfo_states[vfo].chip, code_idx, polarity);
        bk4829_set_dcs_rx(vfo_states[vfo].chip, code_idx, polarity);
    } else {
        bk4829_disable_dcs(vfo_states[vfo].chip);
    }
}

void vfo_clear_tone(radio_vfo_t vfo)
{
    if (vfo > RADIO_VFO_C) return;

    vfo_states[vfo].ctcss_tx_idx = 0xFF;
    vfo_states[vfo].ctcss_rx_idx = 0xFF;
    vfo_states[vfo].dcs_code_idx = 0xFF;
    vfo_states[vfo].dcs_polarity = 0;

    bk4829_disable_ctcss(vfo_states[vfo].chip);
    bk4829_disable_dcs(vfo_states[vfo].chip);
}

/* Squelch / power / step ----------------------------------------------- */

void vfo_set_squelch(radio_vfo_t vfo, uint8_t level)
{
    if (vfo > RADIO_VFO_C) return;

    vfo_states[vfo].squelch_level = level;
    bk4829_set_squelch(vfo_states[vfo].chip, level);
}

void vfo_set_power(radio_vfo_t vfo, uint8_t level)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_states[vfo].tx_power = level;
}

void vfo_set_step(radio_vfo_t vfo, uint32_t step_hz)
{
    if (vfo > RADIO_VFO_C) return;
    if (!is_valid_step(step_hz))
        step_hz = 12500;  /* default to 12.5 kHz if invalid */
    vfo_states[vfo].step_hz = step_hz;
}

void vfo_set_modulation(radio_vfo_t vfo, uint8_t mod)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_states[vfo].modulation = mod;
}

void vfo_set_bandwidth(radio_vfo_t vfo, uint8_t bw)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_states[vfo].bandwidth = bw;
}

void vfo_set_scrambler(radio_vfo_t vfo, uint8_t on)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_states[vfo].scrambler = on;
    if (on)
        bk4829_scrambler_enable(vfo_states[vfo].chip);
    else
        bk4829_scrambler_disable(vfo_states[vfo].chip);
}

void vfo_save(void)
{
    vfo_save_state();
}

/* TX offset ------------------------------------------------------------ */

void vfo_set_offset_dir(radio_vfo_t vfo, uint8_t dir)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_states[vfo].offset_dir = dir;
}

void vfo_set_offset_freq(radio_vfo_t vfo, uint32_t freq_hz)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_states[vfo].offset_freq_hz = freq_hz;
}

void vfo_set_busy_lockout(radio_vfo_t vfo, uint8_t on)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_states[vfo].busy_lockout = on;
}

uint32_t vfo_get_tx_freq(radio_vfo_t vfo)
{
    if (vfo > RADIO_VFO_C) vfo = RADIO_VFO_A;
    const vfo_state_t *s = &vfo_states[vfo];

    switch (s->offset_dir) {
    case 1: return s->freq_hz + s->offset_freq_hz;  /* + offset */
    case 2:                                          /* - offset */
        if (s->freq_hz > s->offset_freq_hz)
            return s->freq_hz - s->offset_freq_hz;
        return s->freq_hz;  /* clamp - don't go negative */
    default: return s->freq_hz;  /* no offset */
    }
}

/*
 * Reverse: swap RX (display) and TX frequencies.
 * This lets you monitor the repeater input while your offset is set.
 * Toggling again restores the original state.
 *
 * Implementation: if offset_dir is + we flip to -, and vice-versa,
 * then move freq_hz to what was the TX frequency.  This preserves
 * the same TX freq while changing the displayed/RX frequency.
 */
void vfo_reverse_toggle(radio_vfo_t vfo)
{
    if (vfo > RADIO_VFO_C) return;
    vfo_state_t *s = &vfo_states[vfo];

    if (s->offset_dir == 0 || s->offset_freq_hz == 0)
        return;  /* nothing to reverse */

    /* Compute current TX freq before we modify anything */
    uint32_t tx_freq = vfo_get_tx_freq(vfo);

    /* Flip offset direction */
    s->offset_dir = (s->offset_dir == 1) ? 2 : 1;

    /* Move RX frequency to what was TX (so TX freq stays the same) */
    s->freq_hz = tx_freq;

    /* Re-apply to hardware (RX on new display frequency) */
    bk4829_set_frequency(s->chip, s->freq_hz);
}

/* VFO-C enable / disable ----------------------------------------------- */

void vfo_set_c_enabled(uint8_t on)
{
    vfo_c_enabled = on ? 1 : 0;

    /* If disabling VFO-C while it's active, fall back to VFO-A */
    if (!vfo_c_enabled && active_vfo == RADIO_VFO_C) {
        active_vfo = RADIO_VFO_A;
        vfo_apply(RADIO_VFO_A);  /* restore chip 1 to VFO-A */
    }

    vfo_save_state();
}

uint8_t vfo_is_c_enabled(void)
{
    return vfo_c_enabled;
}
