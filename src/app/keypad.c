/*
 * keypad.c - Keypad scanner for the RT-950 Pro
 *
 * Matches OEM gpio_output_control @ 0x08012FF8 (184B, V0.27).
 *
 * OEM scan sequence (verified against assembly lines 29718-29791):
 *   1. Read PE5 (TOP_PROG) directly - if LOW, return side button code
 *   2. Read PA12 (BOT_PROG) directly - if LOW, return side button code
 *   3. Clear PC0-PC3, delay ~10 iterations, quick-check PD4-7
 *   4. 5-column scan loop: drive one column LOW, read rows
 *   5. Row decode: 0x70=row0, 0xB0=row1, 0xD0=row2, 0xE0=row3
 *   6. Key codes from flash lookup table at 0x0802F706 (OEM uses data-driven
 *      mapping; we use equivalent hardcoded col*4+row)
 *
 * CORRECTION (Phase 12): PC5 and PA7 were previously used as scan-enable
 * and latch gates. The OEM keypad scan at 0x08012FF8 does NOT reference
 * these pins. They are BK4829 RF scan control pins (SET/CLR @ 0x800DB00).
 */

#include "app/keypad.h"
#include "drivers/gpio.h"
#include "rt950_pinmap.h"

/* ---- Column pin lookup tables ----------------------------------------- */

/* Pin to drive LOW for each column scan iteration.
 * Column 4 drives all low (handled separately). */
static const uint16_t col_clear[] = {
    GPIO_PIN_0,                         /* col 0 */
    GPIO_PIN_1,                         /* col 1 */
    GPIO_PIN_2,                         /* col 2 */
    GPIO_PIN_3,                         /* col 3 */
    GPIO_PIN_0 | GPIO_PIN_1 |           /* col 4 - all low */
        GPIO_PIN_2 | GPIO_PIN_3
};

/* Pins to drive HIGH for each column (complement within PC0-PC3).
 * OEM loads these from flash at 0x0802F6F2 (5 x uint16_t). */
static const uint16_t col_set[] = {
    GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3,   /* col 0 */
    GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3,   /* col 1 */
    GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3,   /* col 2 */
    GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2,   /* col 3 */
    0                                        /* col 4 - none high */
};

/* Row patterns read from GPIOD IDR bits 4-7 (active-low, masked 0xF0).
 * OEM pattern compare at 0x08013064-0x08013072. */
#define ROW0_PATTERN    0x70    /* 0111 - PD7 low */
#define ROW1_PATTERN    0xB0    /* 1011 - PD6 low */
#define ROW2_PATTERN    0xD0    /* 1101 - PD5 low */
#define ROW3_PATTERN    0xE0    /* 1110 - PD4 low */

/* ---- Internal helpers -------------------------------------------------- */

/* Short software delay (~10 loop iterations at 120 MHz).
 * OEM: delay_short(10) @ 0x08013020, 0x08013056. */
static void scan_delay(void)
{
    volatile uint32_t n = 10;
    while (n--)
        ;
}

/* Read row lines (PD4-PD7) and return masked value (bits 4-7). */
static inline uint8_t read_rows(void)
{
    return (uint8_t)(KBD_ROW_PORT->IDR & KBD_ROW_MASK);
}

/* Decode a row pattern to row index (0-3) or 0xFF if none/multiple. */
static uint8_t decode_row(uint8_t pattern)
{
    switch (pattern) {
    case ROW0_PATTERN: return 0;
    case ROW1_PATTERN: return 1;
    case ROW2_PATTERN: return 2;
    case ROW3_PATTERN: return 3;
    default:           return 0xFF;
    }
}

/* ---- Public API -------------------------------------------------------- */

void keypad_init(void)
{
    /* Enable clocks for all ports involved */
    gpio_enable_clock(KBD_COL_PORT);
    gpio_enable_clock(KBD_ROW_PORT);
    gpio_enable_clock(TOP_PROG_PORT);   /* GPIOE for PE5 */
    gpio_enable_clock(BOT_PROG_PORT);   /* GPIOA for PA12 */

    /* Columns PC0-PC3: push-pull output, 50 MHz */
    gpio_config_pin(KBD_COL_PORT, GPIO_PIN_0, GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(KBD_COL_PORT, GPIO_PIN_1, GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(KBD_COL_PORT, GPIO_PIN_2, GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    gpio_config_pin(KBD_COL_PORT, GPIO_PIN_3, GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);

    /* Rows PD4-PD7: input with pull-up */
    gpio_config_pin(KBD_ROW_PORT, GPIO_PIN_4, GPIO_MODE_INPUT, GPIO_CNF_PULL);
    gpio_config_pin(KBD_ROW_PORT, GPIO_PIN_5, GPIO_MODE_INPUT, GPIO_CNF_PULL);
    gpio_config_pin(KBD_ROW_PORT, GPIO_PIN_6, GPIO_MODE_INPUT, GPIO_CNF_PULL);
    gpio_config_pin(KBD_ROW_PORT, GPIO_PIN_7, GPIO_MODE_INPUT, GPIO_CNF_PULL);
    /* Activate pull-ups by setting ODR bits high */
    gpio_set_pin(KBD_ROW_PORT, GPIO_PIN_4 | GPIO_PIN_5 |
                                GPIO_PIN_6 | GPIO_PIN_7);

    /* Side buttons as inputs with pull-up (active-low).
     * OEM reads these directly at start of scan @ 0x08012FFE/0x0801300C. */
    gpio_config_pin(TOP_PROG_PORT, TOP_PROG_PIN, GPIO_MODE_INPUT, GPIO_CNF_PULL);
    gpio_set_pin(TOP_PROG_PORT, TOP_PROG_PIN);
    gpio_config_pin(BOT_PROG_PORT, BOT_PROG_PIN, GPIO_MODE_INPUT, GPIO_CNF_PULL);
    gpio_set_pin(BOT_PROG_PORT, BOT_PROG_PIN);

    /* Idle state: all columns high */
    gpio_set_pin(KBD_COL_PORT, KBD_COL_MASK);
}

uint8_t keypad_scan(void)
{
    /* --- Side button fast-path (OEM @ 0x08012FFE-0x08013012) ---
     * OEM reads PE5 then PA12 before matrix scan.
     * If LOW (pressed, active-low): return immediately. */
    if (!gpio_read_pin(TOP_PROG_PORT, TOP_PROG_PIN))
        return KEY_SIDE1;

    if (!gpio_read_pin(BOT_PROG_PORT, BOT_PROG_PIN))
        return KEY_SIDE4;

    /* --- Matrix scan (OEM @ 0x08013014-0x0801307C) --- */

    /* Clear all columns, wait for settle (OEM: gpio_bits_reset PC0-3, delay_short(10)) */
    gpio_clear_pin(KBD_COL_PORT, KBD_COL_MASK);
    scan_delay();

    /* Quick check: if all rows are high, nobody is pressing anything.
     * OEM: ubfx r0,r0,#4,#4; cmp r0,#15; beq return_0xFF @ 0x0801302E */
    if (read_rows() == KBD_ROW_MASK) {
        gpio_set_pin(KBD_COL_PORT, KBD_COL_MASK);
        return KEY_NONE;
    }

    /* Scan each of 5 columns (OEM loop: r4=0..4 @ 0x08013040-0x0801307A) */
    for (uint8_t col = 0; col < 5; col++) {
        /* Drive the selected column low, others high.
         * OEM: ldrh col_set[col], gpio_bits_set; ldrh col_clear[col], gpio_bits_reset */
        if (col_set[col])
            gpio_set_pin(KBD_COL_PORT, col_set[col]);
        gpio_clear_pin(KBD_COL_PORT, col_clear[col]);
        scan_delay();

        uint8_t rows = read_rows();
        if (rows == KBD_ROW_MASK)
            continue;   /* no key in this column */

        uint8_t row = decode_row(rows);
        if (row == 0xFF)
            continue;   /* multiple keys or noise */

        /* Restore idle state before returning */
        gpio_set_pin(KBD_COL_PORT, KBD_COL_MASK);
        return (uint8_t)(col * 4 + row);
    }

    /* No key found - restore idle */
    gpio_set_pin(KBD_COL_PORT, KBD_COL_MASK);
    return KEY_NONE;
}

/* ---- Debounced key-event state machine --------------------------------- */

static uint8_t  prev_key   = KEY_NONE;  /* last accepted key */
static uint8_t  db_count   = 0;         /* debounce counter */
static uint16_t hold_count = 0;         /* repeat timer */

uint8_t keypad_get_event(key_event_t *evt)
{
    uint8_t raw = keypad_scan();
    evt->type = KEY_EVT_NONE;
    evt->key  = KEY_NONE;

    if (raw != KEY_NONE && raw == prev_key) {
        /* Same key still held - handle repeat */
        hold_count++;
        if (hold_count == KEY_REPEAT_DELAY) {
            evt->type = KEY_EVT_REPEAT;
            evt->key  = prev_key;
            return 1;
        }
        if (hold_count > KEY_REPEAT_DELAY &&
            ((hold_count - KEY_REPEAT_DELAY) % KEY_REPEAT_RATE) == 0) {
            evt->type = KEY_EVT_REPEAT;
            evt->key  = prev_key;
            return 1;
        }
        return 0;
    }

    if (raw != KEY_NONE && raw != prev_key) {
        /* New key candidate - debounce */
        db_count++;
        if (db_count >= KEY_DEBOUNCE_COUNT) {
            /* Generate release for previous key if one was held */
            if (prev_key != KEY_NONE) {
                evt->type = KEY_EVT_RELEASE;
                evt->key  = prev_key;
                prev_key  = raw;
                db_count  = 0;
                hold_count = 0;
                return 1;
            }
            /* Accept new press */
            prev_key   = raw;
            db_count   = 0;
            hold_count = 0;
            evt->type  = KEY_EVT_PRESS;
            evt->key   = raw;
            return 1;
        }
        return 0;
    }

    /* raw == KEY_NONE */
    if (prev_key != KEY_NONE) {
        db_count++;
        if (db_count >= KEY_DEBOUNCE_COUNT) {
            evt->type  = KEY_EVT_RELEASE;
            evt->key   = prev_key;
            prev_key   = KEY_NONE;
            db_count   = 0;
            hold_count = 0;
            return 1;
        }
        return 0;
    }

    db_count = 0;
    return 0;
}
