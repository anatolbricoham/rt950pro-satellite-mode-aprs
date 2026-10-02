/*
 * keypad.h - Keypad scanner for the RT-950 Pro
 *
 * 4x4 matrix: columns PC0-PC3, rows PD4-PD7.
 * Side buttons PE5 (TOP_PROG) and PA12 (BOT_PROG) read directly
 * before matrix scan, matching OEM gpio_output_control @ 0x08012FF8.
 *
 * CORRECTION (Phase 12): PC5/PA7 were incorrectly used as keypad
 * scan-enable/latch. OEM keypad scan does not reference these pins.
 * PC5/PA7 are BK4829 RF scan control (SET/CLR @ 0x800DB00/0x800DB08).
 */

#ifndef APP_KEYPAD_H
#define APP_KEYPAD_H

#include <stdint.h>

/* --- Key codes (column * 4 + row for matrix, fixed codes for GPIO) ------ */
#define KEY_1           0       /* C0 R0 */
#define KEY_2           1       /* C0 R1 */
#define KEY_3           2       /* C0 R2 */
#define KEY_A_VFO       3       /* C0 R3 */
#define KEY_4           4       /* C1 R0 */
#define KEY_5           5       /* C1 R1 */
#define KEY_6           6       /* C1 R2 */
#define KEY_B_SCAN      7       /* C1 R3 */
#define KEY_7           8       /* C2 R0 */
#define KEY_8           9       /* C2 R1 */
#define KEY_9           10      /* C2 R2 */
#define KEY_C_MENU      11      /* C2 R3 */
#define KEY_STAR        12      /* C3 R0 */
#define KEY_0           13      /* C3 R1 */
#define KEY_HASH        14      /* C3 R2 */
#define KEY_D_BAND      15      /* C3 R3 */
#define KEY_C4R0        16      /* C4 R0 - matrix col 4 (if wired) */
#define KEY_C4R1        17      /* C4 R1 */
#define KEY_C4R2        18      /* C4 R2 */
#define KEY_C4R3        19      /* C4 R3 */
/* Direct-GPIO side buttons (OEM returns 0/1 for these; we use 20/21) */
#define KEY_SIDE1       20      /* PE5  TOP_PROG - direct GPIO read */
#define KEY_SIDE4       21      /* PA12 BOT_PROG - direct GPIO read */
#define KEY_COUNT       22
#define KEY_NONE        0xFF

/* Legacy aliases: PTT is NOT on the keypad matrix - it's a standalone
 * GPIO (PE3) monitored by task_buttons(). These defines allow app code
 * to reference side button functions by their original names. */
#define KEY_PTT         0xE3    /* PE3 PTT - standalone GPIO, not matrix */
#define KEY_SIDE2       KEY_C4R2  /* legacy alias for matrix col4 row2 */
#define KEY_SIDE3       KEY_C4R3  /* legacy alias for matrix col4 row3 */

/* --- Key event types ---------------------------------------------------- */
typedef enum {
    KEY_EVT_NONE = 0,
    KEY_EVT_PRESS,
    KEY_EVT_REPEAT,
    KEY_EVT_RELEASE
} key_event_type_t;

typedef struct {
    key_event_type_t type;
    uint8_t          key;       /* KEY_* code */
} key_event_t;

/* --- Timing (in units of keypad_get_event() call intervals) ------------- */
#define KEY_DEBOUNCE_COUNT  3   /* calls before accepting a press */
#define KEY_REPEAT_DELAY   30   /* calls before first repeat */
#define KEY_REPEAT_RATE     6   /* calls between subsequent repeats */

/* --- API ---------------------------------------------------------------- */

/* Configure GPIO for keypad matrix + side button inputs. Call once. */
void keypad_init(void);

/*
 * Perform a single scan: side button direct reads + matrix scan.
 * Returns KEY_* code (0-21) or KEY_NONE (0xFF) if no key pressed.
 * OEM reference: gpio_output_control @ 0x08012FF8.
 */
uint8_t keypad_scan(void);

/*
 * Debounced key event detector.
 * Call periodically (e.g. every 20 ms).
 * Fills *evt with press / repeat / release events.
 * Returns 1 if an event is available, 0 otherwise.
 */
uint8_t keypad_get_event(key_event_t *evt);

#endif /* APP_KEYPAD_H */
