/*
 * dtmf.h - DTMF tone encode/decode for the RT-950 Pro
 *
 * Uses the BK4829 built-in DTMF generator and detector.
 * Tone mapping: 0-9='0'-'9', 10='A', 11='B', 12='C', 13='D', 14='*', 15='#'
 */

#ifndef APP_DTMF_H
#define APP_DTMF_H

#include <stdint.h>

/* DTMF character set: 0-9, A-D, *, # */
#define DTMF_CHAR_COUNT 16

/* Convert a DTMF character ('0'-'9','A'-'D','*','#') to tone index (0-15).
 * Returns 0xFF if invalid. */
uint8_t dtmf_char_to_index(char ch);

/* Convert tone index to DTMF character.
 * Returns '?' if index is out of range. */
char dtmf_index_to_char(uint8_t index);

/* Send a single DTMF tone on the specified chip.
 * Duration in ms. Blocks until tone is complete. */
void dtmf_send_tone(uint8_t chip, char ch, uint16_t duration_ms);

/* Send a DTMF string (e.g., "1234ABCD*#").
 * tone_ms = per-tone duration, gap_ms = inter-tone gap. */
void dtmf_send_string(uint8_t chip, const char *str,
                       uint16_t tone_ms, uint16_t gap_ms);

/* Start DTMF decode monitoring on a chip */
void dtmf_decode_start(uint8_t chip);

/* Stop DTMF decode monitoring */
void dtmf_decode_stop(uint8_t chip);

/* Poll for decoded DTMF digit. Returns character or 0 if none. */
char dtmf_decode_poll(uint8_t chip);

/* DTMF auto-dial: store and send a contact string */
#define DTMF_MAX_CONTACTS   16
#define DTMF_MAX_DIGITS     16

typedef struct {
    char digits[DTMF_MAX_DIGITS + 1];  /* Null-terminated DTMF string */
    char name[8];                        /* Short name */
} dtmf_contact_t;

/* Send a stored contact's DTMF sequence */
void dtmf_dial_contact(uint8_t chip, const dtmf_contact_t *contact);

/* PTT-ID configuration (loaded from flash 0x0C000) ------------------ */

#define DTMF_ID_MAX_LEN    16  /* Max digits in PTT-ID string */
#define DTMF_GROUP_COUNT    15  /* DTMF group codes */
#define DTMF_GROUP_MAX_LEN   8  /* Max digits per group code */

/* Runtime DTMF configuration - loaded once at boot */
typedef struct {
    char    current_id[DTMF_ID_MAX_LEN + 1];  /* PTT-ID string (e.g. "123") */
    uint16_t tone_ms;                           /* DTMF on-time (ms) */
    uint16_t gap_ms;                            /* DTMF off-time (ms) */
} dtmf_config_t;

/* Load DTMF config from flash 0x0C000. Call once at boot. */
void dtmf_load_config(void);

/* Get pointer to the live DTMF config (read-only) */
const dtmf_config_t *dtmf_get_config(void);

/* Send the PTT-ID string on the given chip using configured timing */
void dtmf_send_ptt_id(uint8_t chip);

#endif /* APP_DTMF_H */
