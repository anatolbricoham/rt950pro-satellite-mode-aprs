/*
 * font.h - Flash font rendering for the RT-950 Pro
 *
 * Reads 1bpp monospaced bitmap fonts stored in SPI flash and renders
 * them to the LCD as RGB565 pixel data.  Multiple font sizes are
 * available for different UI contexts (menus, status bar, frequency).
 *
 * Font format: 1bpp, MSB-first, tightly packed (no per-row byte padding).
 * Glyph data is stored row-major: bit N maps to pixel (N % width, N / width).
 *
 * V0.27 font tables (verified via literal pools and glyph calculations):
 *   ASCII Large  @ SPI 0x15C000, 39 B/glyph (rsb+add @ fw 0x08025832)
 *   ASCII Medium @ SPI 0x1C3C40, 12 B/glyph (add.w  @ fw 0x0802591C)
 *   ASCII Small  @ SPI +0x900 from Medium, 12 B/glyph
 *   Display      @ SPI 0x1D35C8, 192 B/glyph (digits, @ fw 0x080146F4)
 *   Display Alt  @ SPI 0x1D4090, 192 B/glyph (ldr @ fw 0x080147CC)
 *   CJK Small    @ SPI 0x15CF00, 32 B/glyph  (pool @ fw 0x080258A0)
 *   CJK Medium   @ SPI 0x199E80, 24 B/glyph  (pool @ fw 0x08025820)
 *   CJK Tiny     @ SPI 0x1986C0, 16 B/glyph  (pool @ fw 0x08025974)
 *
 * CJK index formula (GB2312): (byte1 - 0xA1) * 94 + (byte2 - 0xA1)
 * ASCII glyph addr: base + (char - 0x20) * bytes_per_glyph
 *   Large example: r2*7 + r2*32 = r2*39 (rsb r0,r2,r2,lsl#3; add.w)
 */

#ifndef APP_FONT_H
#define APP_FONT_H

#include <stdint.h>

/* Font IDs */
typedef enum {
    FONT_LARGE = 0,     /* ASCII large - menu text */
    FONT_MEDIUM,        /* ASCII medium - general text */
    FONT_SMALL,         /* ASCII small - status bar */
    FONT_DISPLAY,       /* Large display font - frequency readout */
    FONT_COUNT,
} font_id_t;

/* Font metrics */
typedef struct {
    uint32_t flash_base;      /* SPI flash start address */
    uint8_t  char_width;      /* Glyph width in pixels */
    uint8_t  char_height;     /* Glyph height in pixels */
    uint16_t bytes_per_glyph; /* Total bytes per character */
    uint8_t  first_char;      /* First ASCII character in font (usually 0x20 = space) */
    uint8_t  last_char;       /* Last character */
} font_info_t;

/* Get font metrics */
const font_info_t *font_get_info(font_id_t font);

/* Draw a single character at (x, y) with foreground and background colors (RGB565) */
void font_draw_char(font_id_t font, uint16_t x, uint16_t y,
                    char ch, uint16_t fg_color, uint16_t bg_color);

/* Draw a string at (x, y) */
void font_draw_string(font_id_t font, uint16_t x, uint16_t y,
                      const char *str, uint16_t fg_color, uint16_t bg_color);

/* Get the pixel width of a string */
uint16_t font_string_width(font_id_t font, const char *str);

#endif /* APP_FONT_H */
