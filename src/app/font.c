/*
 * font.c - Flash font rendering for the RT-950 Pro
 *
 * Reads 1bpp monospaced bitmap fonts from SPI flash and renders
 * them to the LCD as RGB565 pixel data.  Supports multiple font
 * sizes for different UI contexts.
 *
 * Font data is 1bpp, MSB-first, tightly packed (no per-row byte
 * padding).  Bit N of a glyph maps to pixel (N % width, N / width).
 */

#include "app/font.h"
#include "drivers/lcd.h"
#include "drivers/spi.h"
#include "drivers/flash_layout.h"

/*
 * Static glyph buffer - sized to hold the largest glyph (display font:
 * 192 bytes per glyph verified from V0.27 @ 0x080146FC).
 */
#define GLYPH_BUF_SIZE 256

static uint8_t glyph_buf[GLYPH_BUF_SIZE];

/*
 * Font metrics table.
 *
 * V0.27 verified glyph sizes (bytes_per_glyph):
 *   LARGE:   39 B  - rsb+add.w calc @ 0x08025832, reads 0x27 @ 0x0802583E
 *   MEDIUM:  12 B  - add.w calc @ 0x0802591C, reads 0x0C @ 0x08025924
 *   DISPLAY: 192 B - add.w calc @ 0x080146F4, reads 0xC0 @ 0x080146FC
 *
 * Flash base addresses verified via literal pools and mov.w instructions.
 * Pixel dimensions are estimated from byte counts (need visual verification).
 */
static const font_info_t font_table[FONT_COUNT] = {
    /* V0.27 @ 0x08025826: mov.w r0, 0x15c000; 39 B/glyph (13x24 1bpp) */
    [FONT_LARGE]   = { FLASH_ADDR_FONT_ASCII_L, 13, 24, 39,  0x20, 0x7E },
    /* V0.27 @ 0x0802592C: ldr literal 0x1C3C40; 12 B/glyph (8x12 1bpp) */
    [FONT_MEDIUM]  = { FLASH_ADDR_FONT_ASCII_M, 8, 12, 12,   0x20, 0x7E },
    /* Derived: same flash region offset, same glyph size */
    [FONT_SMALL]   = { FLASH_ADDR_FONT_ASCII_M + 0x900, 8, 12, 12, 0x20, 0x7E },
    /* V0.27 @ 0x080146CC: ldr literal 0x1D35C8; 192 B/glyph, digits only */
    [FONT_DISPLAY] = { 0x1D35C8, 32, 48, 192, 0x30, 0x39 },
};

/* ========================================================================
 *  font_get_info - Return pointer to font metrics for the given font ID.
 * ======================================================================== */

const font_info_t *font_get_info(font_id_t font)
{
    if (font >= FONT_COUNT)
        return &font_table[FONT_LARGE];
    return &font_table[font];
}

/* ========================================================================
 *  font_draw_char - Render one glyph from SPI flash to the LCD.
 *
 *  1. Read the 1bpp glyph bitmap from flash into glyph_buf.
 *  2. Set an LCD window covering the glyph bounding box.
 *  3. Stream RGB565 pixels: set bits -> fg_color, clear bits -> bg_color.
 *
 *  Bit addressing is tightly packed (no per-row byte alignment):
 *    bit_pos = row * char_width + col
 *    byte    = bit_pos / 8,  mask = 0x80 >> (bit_pos % 8)
 * ======================================================================== */

void font_draw_char(font_id_t font, uint16_t x, uint16_t y,
                    char ch, uint16_t fg_color, uint16_t bg_color)
{
    const font_info_t *info = font_get_info(font);
    uint8_t c = (uint8_t)ch;

    if (c < info->first_char || c > info->last_char)
        return;

    /* Flash address for this glyph */
    uint32_t glyph_offset = (uint32_t)(c - info->first_char) * info->bytes_per_glyph;
    uint32_t addr = info->flash_base + glyph_offset;

    /* Read glyph bitmap from SPI flash */
    uint16_t read_len = info->bytes_per_glyph;
    if (read_len > GLYPH_BUF_SIZE)
        read_len = GLYPH_BUF_SIZE;

    spi_flash_read(addr, glyph_buf, read_len);

    /* Zero-fill any bytes beyond read_len needed for rendering.
     * Handles fonts where total pixel bits exceed stored bytes
     * (e.g., display font: 88x90 = 990 bytes rendered, 988 stored). */
    uint16_t total_bits = (uint16_t)info->char_width * info->char_height;
    uint16_t render_bytes = (total_bits + 7) >> 3;
    for (uint16_t i = read_len; i < render_bytes && i < GLYPH_BUF_SIZE; i++)
        glyph_buf[i] = 0;

    /* Set LCD drawing window */
    lcd_set_window(x, y,
                   (uint16_t)(x + info->char_width - 1),
                   (uint16_t)(y + info->char_height - 1));
    lcd_gram_write();

    /* Stream pixels: 1bpp tightly packed, MSB-first, row-major */
    for (uint8_t row = 0; row < info->char_height; row++) {
        uint16_t row_bit = (uint16_t)row * info->char_width;
        for (uint8_t col = 0; col < info->char_width; col++) {
            uint16_t bit_pos = row_bit + col;
            uint16_t byte_idx = bit_pos >> 3;
            uint8_t bit_mask = (uint8_t)(0x80U >> (bit_pos & 7));
            uint16_t px = (glyph_buf[byte_idx] & bit_mask) ? fg_color : bg_color;
            lcd_write_data((uint8_t)(px >> 8));
            lcd_write_data((uint8_t)(px & 0xFF));
        }
    }
}

/* ========================================================================
 *  font_draw_string - Render a null-terminated string at (x, y).
 *
 *  Characters advance by char_width pixels (no inter-character gap --
 *  the flash fonts include built-in spacing).
 * ======================================================================== */

void font_draw_string(font_id_t font, uint16_t x, uint16_t y,
                      const char *str, uint16_t fg_color, uint16_t bg_color)
{
    const font_info_t *info = font_get_info(font);

    while (*str) {
        if (x + info->char_width > LCD_WIDTH)
            break;
        font_draw_char(font, x, y, *str, fg_color, bg_color);
        x += info->char_width;
        str++;
    }
}

/* ========================================================================
 *  font_string_width - Return the total pixel width of a string.
 * ======================================================================== */

uint16_t font_string_width(font_id_t font, const char *str)
{
    const font_info_t *info = font_get_info(font);
    uint16_t count = 0;

    while (*str) {
        count++;
        str++;
    }
    return count * info->char_width;
}
