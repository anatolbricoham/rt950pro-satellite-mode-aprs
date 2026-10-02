/*
 * freq_entry.c - Digit-by-digit frequency entry for the RT-950 Pro
 *
 * Accumulates up to 6 keypad digits representing a frequency in MHz
 * format (XXX.XXX).  The decimal point is implied after the third
 * digit.  On the sixth digit the entry auto-confirms; the user may
 * also press # to confirm early or * to backspace.
 *
 * Integration point: radio_handle_key() should call into this module
 * when digit keys are pressed outside of menu mode.
 */

#include "app/freq_entry.h"
#include "app/vfo.h"

/* State ---------------------------------------------------------------- */

static uint8_t active;          /* 1 = entry mode active */
static uint8_t digits[6];      /* Entered digits */
static uint8_t digit_count;    /* How many digits entered so far */
static char display_buf[16];   /* "X X X . X X X" + NUL */

/* Band limits (Hz) ----------------------------------------------------- */

#define VHF_LOW   130000000U
#define VHF_HIGH  180000000U
#define UHF_LOW   400000000U
#define UHF_HIGH  520000000U

/* Internal helpers ----------------------------------------------------- */

/*
 * Digit-slot positions within display_buf:
 *   "X X X . X X X"
 *    0 2 4 6 8 0 2      (indices 0,2,4,8,10,12)
 *          ^dot at 6, spaces at 1,3,5,7,9,11
 */
static const uint8_t dpos[6] = { 0, 2, 4, 8, 10, 12 };

static void update_display(void)
{
    display_buf[1]  = ' ';
    display_buf[3]  = ' ';
    display_buf[5]  = ' ';
    display_buf[6]  = '.';
    display_buf[7]  = ' ';
    display_buf[9]  = ' ';
    display_buf[11] = ' ';
    display_buf[13] = '\0';

    for (uint8_t i = 0; i < 6; i++) {
        if (i < digit_count)
            display_buf[dpos[i]] = (char)('0' + digits[i]);
        else
            display_buf[dpos[i]] = '_';
    }
}

static uint8_t freq_in_band(uint32_t hz)
{
    if (hz >= VHF_LOW && hz <= VHF_HIGH)
        return 1;
    if (hz >= UHF_LOW && hz <= UHF_HIGH)
        return 1;
    return 0;
}

/* Public API ----------------------------------------------------------- */

void freq_entry_start(void)
{
    active = 1;
    digit_count = 0;
    for (uint8_t i = 0; i < 6; i++)
        digits[i] = 0;
    update_display();
}

void freq_entry_cancel(void)
{
    active = 0;
    digit_count = 0;
}

uint8_t freq_entry_digit(uint8_t digit)
{
    if (!active || digit > 9 || digit_count >= 6)
        return 0;

    digits[digit_count++] = digit;
    update_display();

    if (digit_count == 6) {
        freq_entry_confirm();
        return 1;
    }
    return 0;
}

void freq_entry_confirm(void)
{
    if (!active)
        return;

    uint32_t freq_hz = freq_entry_get_freq_hz();
    if (freq_hz == 0)
        return;

    if (freq_in_band(freq_hz)) {
        vfo_set_frequency(vfo_get_active(), freq_hz);
        active = 0;
    }
    /* Invalid frequency: stay in entry mode for correction */
}

void freq_entry_backspace(void)
{
    if (!active || digit_count == 0)
        return;

    digit_count--;
    digits[digit_count] = 0;
    update_display();
}

uint8_t freq_entry_is_active(void)
{
    return active;
}

const char *freq_entry_get_display(void)
{
    return display_buf;
}

uint32_t freq_entry_get_freq_hz(void)
{
    if (digit_count < 6)
        return 0;

    uint32_t mhz = (uint32_t)digits[0] * 100U +
                    (uint32_t)digits[1] * 10U  +
                    (uint32_t)digits[2];
    uint32_t khz = (uint32_t)digits[3] * 100U +
                    (uint32_t)digits[4] * 10U  +
                    (uint32_t)digits[5];

    return mhz * 1000000U + khz * 1000U;
}
