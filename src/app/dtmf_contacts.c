/*
 * dtmf_contacts.c - DTMF contact list browser for the RT-950 Pro
 *
 * Reads contacts from SPI flash at 0x013000 (25 sectors x 4 contacts).
 * Each sector: header area at 0x00-0x9F, data at 0xA0+n*0xCD.
 *
 * Per-record (205 bytes / 0xCD):
 *   [0x00-0x0F]  Name (16 bytes, 0xFF-padded)
 *   [0x10-0x27]  DTMF digits (24 bytes, 0xFF-padded)
 *   [0x28-0xCC]  Extended fields (group, timing, etc. - not used here)
 */

#include "app/dtmf_contacts.h"
#include "app/dtmf.h"
#include "app/keypad.h"
#include "app/display.h"
#include "app/font.h"
#include "drivers/lcd.h"
#include "drivers/spi_flash.h"
#include "drivers/spi.h"

/* Flash reading -------------------------------------------------------- */

/*
 * Read a DTMF contact from flash by index (0-99).
 * Returns 1 if the entry is valid (name not all 0xFF).
 */
static uint8_t read_contact(uint8_t index, dtmf_contact_entry_t *out)
{
    if (index >= FLASH_DTMF_TOTAL_CONTACTS) {
        out->valid = 0;
        return 0;
    }

    uint8_t sector = index / FLASH_DTMF_CONTACTS_PER_SECTOR;
    uint8_t slot   = index % FLASH_DTMF_CONTACTS_PER_SECTOR;

    uint32_t addr = FLASH_DTMF_EXT_BASE
                  + (uint32_t)sector * 0x1000
                  + FLASH_DTMF_DATA_OFFSET
                  + (uint32_t)slot * FLASH_DTMF_RECORD_SIZE;

    uint8_t raw[40];  /* only need first 40 bytes for name+digits */
    spi_flash_read(addr, raw, sizeof(raw));

    /* Check if entry is blank (all 0xFF) */
    uint8_t blank = 1;
    for (uint8_t i = 0; i < 16; i++) {
        if (raw[i] != 0xFF) { blank = 0; break; }
    }
    if (blank) {
        out->valid = 0;
        out->name[0] = '\0';
        out->digits[0] = '\0';
        return 0;
    }

    /* Copy name (0x00-0x0F), strip 0xFF padding */
    for (uint8_t i = 0; i < 16; i++) {
        out->name[i] = (raw[i] == 0xFF) ? '\0' : (char)raw[i];
        if (raw[i] == 0xFF) {
            out->name[i] = '\0';
            break;
        }
    }
    out->name[15] = '\0';

    /* Copy digits (0x10-0x27), strip 0xFF padding */
    for (uint8_t i = 0; i < 24; i++) {
        uint8_t b = raw[16 + i];
        out->digits[i] = (b == 0xFF) ? '\0' : (char)b;
        if (b == 0xFF) {
            out->digits[i] = '\0';
            break;
        }
    }
    out->digits[23] = '\0';

    out->valid = 1;
    return 1;
}

/* Browser state -------------------------------------------------------- */

static uint8_t active;
static uint8_t cursor;           /* index 0-99 */
static uint8_t scroll_top;       /* first visible index */
static char    selected_digits[24];

#define VISIBLE_ROWS  5  /* contacts visible on screen at once */

void dtmf_contacts_open(void)
{
    active = 1;
    cursor = 0;
    scroll_top = 0;
    selected_digits[0] = '\0';
}

void dtmf_contacts_close(void)
{
    active = 0;
}

uint8_t dtmf_contacts_is_active(void)
{
    return active;
}

const char *dtmf_contacts_get_selected(void)
{
    return selected_digits;
}

/* Input ---------------------------------------------------------------- */

void dtmf_contacts_handle_key(uint8_t key)
{
    if (!active) return;

    if (key == KEY_C_MENU) {
        /* Confirm selection */
        dtmf_contact_entry_t entry;
        if (read_contact(cursor, &entry) && entry.valid) {
            uint8_t i;
            for (i = 0; i < 23 && entry.digits[i]; i++)
                selected_digits[i] = entry.digits[i];
            selected_digits[i] = '\0';
        }
        active = 0;
        return;
    }

    if (key == KEY_D_BAND || key == KEY_HASH) {
        selected_digits[0] = '\0';
        active = 0;
        return;
    }
}

void dtmf_contacts_handle_encoder(int8_t direction)
{
    if (!active) return;

    if (direction > 0 && cursor < FLASH_DTMF_TOTAL_CONTACTS - 1)
        cursor++;
    else if (direction < 0 && cursor > 0)
        cursor--;

    /* Keep cursor visible */
    if (cursor < scroll_top)
        scroll_top = cursor;
    if (cursor >= scroll_top + VISIBLE_ROWS)
        scroll_top = cursor - VISIBLE_ROWS + 1;
}

/* Drawing -------------------------------------------------------------- */

void dtmf_contacts_draw(void)
{
    if (!active) return;

    lcd_fill_rect(0, 0, LCD_WIDTH, 200, COLOR_BLACK);

    /* Header */
    font_draw_string(FONT_SMALL, 4, 4, "DTMF Contacts", COLOR_YELLOW, COLOR_BLACK);
    lcd_fill_rect(0, 18, LCD_WIDTH, 1, COLOR_DARK_GRAY);

    /* List entries */
    dtmf_contact_entry_t entry;
    uint16_t y = 24;

    for (uint8_t row = 0; row < VISIBLE_ROWS; row++) {
        uint8_t idx = scroll_top + row;
        if (idx >= FLASH_DTMF_TOTAL_CONTACTS) break;

        uint8_t is_sel = (idx == cursor);
        uint16_t fg = is_sel ? COLOR_WHITE : COLOR_GRAY;
        uint16_t bg = is_sel ? COLOR_DARK_GRAY : COLOR_BLACK;

        /* Row background for selection */
        if (is_sel)
            lcd_fill_rect(0, y, LCD_WIDTH, 18, bg);

        /* Index number */
        char num[4];
        num[0] = (char)('0' + (idx / 10));
        num[1] = (char)('0' + (idx % 10));
        num[2] = '.';
        num[3] = '\0';
        font_draw_string(FONT_SMALL, 4, y + 2, num, fg, bg);

        /* Name + digits */
        if (read_contact(idx, &entry) && entry.valid) {
            font_draw_string(FONT_SMALL, 30, y + 2, entry.name, fg, bg);
            font_draw_string(FONT_SMALL, 130, y + 2, entry.digits,
                             COLOR_CYAN, bg);
        } else {
            font_draw_string(FONT_SMALL, 30, y + 2, "(empty)",
                             COLOR_DARK_GRAY, bg);
        }

        y += 20;
    }

    /* Footer */
    lcd_fill_rect(0, 130, LCD_WIDTH, 1, COLOR_DARK_GRAY);
    font_draw_string(FONT_SMALL, 4, 134, "MENU=Select  EXIT=Back",
                     COLOR_GRAY, COLOR_BLACK);
}
