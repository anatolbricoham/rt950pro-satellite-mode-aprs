/*
 * dtmf_contacts.h - DTMF contact list browser for the RT-950 Pro
 *
 * Reads 100 DTMF contacts from SPI flash (0x013000, 25 sectors x 4 contacts).
 * Each contact: 205-byte record with name + DTMF digit string.
 *
 * Provides scrollable list UI, select-to-dial, and PTT-ID contact selection.
 */

#ifndef APP_DTMF_CONTACTS_H
#define APP_DTMF_CONTACTS_H

#include <stdint.h>

/* Parsed contact entry */
typedef struct {
    char name[16];      /* Contact name (null-terminated) */
    char digits[24];    /* DTMF digit string (null-terminated) */
    uint8_t valid;      /* 1 if entry is non-empty */
} dtmf_contact_entry_t;

/* Open the contact list browser */
void dtmf_contacts_open(void);

/* Close the browser */
void dtmf_contacts_close(void);

/* Returns 1 while browser is active */
uint8_t dtmf_contacts_is_active(void);

/* Input handlers */
void dtmf_contacts_handle_key(uint8_t key);
void dtmf_contacts_handle_encoder(int8_t direction);

/* Draw the contact list (call from display refresh) */
void dtmf_contacts_draw(void);

/* Get the selected contact's digits (valid after user confirms) */
const char *dtmf_contacts_get_selected(void);

#endif /* APP_DTMF_CONTACTS_H */
