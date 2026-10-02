/*
 * freq_entry.h - Digit-by-digit frequency entry for the RT-950 Pro
 *
 * Allows the user to type a frequency in MHz format (XXX.XXX) using
 * the numeric keypad.  The decimal point is implied at position 3.
 */

#ifndef APP_FREQ_ENTRY_H
#define APP_FREQ_ENTRY_H

#include <stdint.h>

/* Start frequency entry mode */
void freq_entry_start(void);

/* Cancel and return to normal mode */
void freq_entry_cancel(void);

/* Handle a digit key press (0-9). Returns 1 if entry is complete. */
uint8_t freq_entry_digit(uint8_t digit);

/* Handle confirm (# key) - apply entered frequency */
void freq_entry_confirm(void);

/* Handle backspace/delete last digit */
void freq_entry_backspace(void);

/* Is frequency entry mode active? */
uint8_t freq_entry_is_active(void);

/* Get display string of current entry (for rendering) */
const char *freq_entry_get_display(void);

/* Get the entered frequency in Hz (0 if incomplete/invalid) */
uint32_t freq_entry_get_freq_hz(void);

#endif /* APP_FREQ_ENTRY_H */
