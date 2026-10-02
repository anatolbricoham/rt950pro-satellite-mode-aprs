/*
 * lcd.h - 8080-parallel LCD driver for the RT-950 Pro
 *
 * Controller: ST7789V (240x320 IPS, display inversion on)
 * 8-bit 8080 parallel: PD8-PD15=data, PD0=WR, PD1=CS, PD2=RST, PD3=DC
 * Pixel format: RGB565 (16-bit)
 * Orientation: MADCTL=0xC0 (180 deg rotation)
 *
 * Init sequence extracted from V0.27 firmware @ 0x08026954 (corrected -0x3000).
 */

#ifndef DRIVERS_LCD_H
#define DRIVERS_LCD_H

#include <stdint.h>

/* Display dimensions (ST7789V 240x320) */
#define LCD_WIDTH   240
#define LCD_HEIGHT  320

/*
 * lcd_init - Initialize the LCD controller.
 * Performs hardware reset, sends init sequence, clears screen.
 */
void lcd_init(void);

/*
 * lcd_write_command - Send a command byte via 8080 bus.
 * D/C = LOW, then pulses WR with data on PD8-PD15.
 */
void lcd_write_command(uint8_t cmd);

/*
 * lcd_write_data - Send a data byte via 8080 bus.
 * D/C = HIGH, then pulses WR with data on PD8-PD15.
 */
void lcd_write_data(uint8_t data);

/*
 * lcd_set_window - Define the active drawing rectangle.
 * Sends Column Address Set (0x2A) and Row Address Set (0x2B).
 */
void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/*
 * lcd_gram_write - Begin GRAM write (sends 0x2C command).
 * Follow with lcd_write_data() calls to push pixel data.
 */
void lcd_gram_write(void);

/*
 * lcd_panel_reset - Perform hardware reset via PD2 (BINARY VERIFIED).
 */
void lcd_panel_reset(void);

/*
 * lcd_backlight_on / lcd_backlight_off - Control PC6 backlight (BINARY VERIFIED).
 * Also toggles PB3 (secondary backlight driver enable).
 */
void lcd_backlight_on(void);
void lcd_backlight_off(void);

/*
 * lcd_fill_rect - Fill a rectangle with a solid 16-bit RGB565 color.
 */
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/*
 * lcd_draw_string - Draw text using embedded 8x8 bitmap font.
 * No SPI flash needed. fg/bg are RGB565 colors.
 */
void lcd_draw_string(uint16_t x, uint16_t y, const char *str,
                     uint16_t fg, uint16_t bg);

/*
 * lcd_draw_string_2x - Draw text at 2x scale (16px tall).
 */
void lcd_draw_string_2x(uint16_t x, uint16_t y, const char *str,
                         uint16_t fg, uint16_t bg);

#endif /* DRIVERS_LCD_H */
