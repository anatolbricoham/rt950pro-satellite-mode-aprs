/*
 * splash.c - Boot splash display for the RT-950 Pro
 *
 * Reads a raw RGB565 image (240x320, 153,600 bytes) from SPI flash
 * starting at FLASH_ADDR_SPLASH and streams it to the LCD one row
 * at a time to keep stack usage low.
 *
 * RE reference: 0x0800E928 reads 10 blocks of 32 rows (15,360 B each),
 * pixel-by-pixel write to ST7789V GRAM, no compression, no DMA.
 */

#include "app/splash.h"
#include "drivers/lcd.h"
#include "drivers/spi.h"
#include "drivers/flash_layout.h"

#ifndef BH_FW_VERSION
#define BH_FW_VERSION "0.3.0"
#endif
#define BH_ORANGE  0xF343u   /* BricoHams orange #F26B1D in RGB565 */
#define BH_DARK    0x2125u   /* BricoHams dark   #23272B in RGB565 */

extern void delay_ms(uint32_t ms);

#define SPLASH_DEFAULT_DELAY_MS  1000
#define SPLASH_ROW_BYTES         (LCD_WIDTH * 2)   /* 240 px x 2 B = 480 B */

/* Static row buffer - avoids large stack allocation */
static uint8_t row_buf[SPLASH_ROW_BYTES];

void splash_show(void)
{
    splash_show_with_delay(SPLASH_DEFAULT_DELAY_MS);
}

void splash_show_with_delay(uint16_t ms)
{
    uint32_t flash_addr = FLASH_ADDR_SPLASH;

    /* Set LCD window to full screen and begin GRAM write */
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    lcd_gram_write();

    /* Stream image data row-by-row from SPI flash to LCD */
    for (uint16_t row = 0; row < LCD_HEIGHT; row++) {
        spi_flash_read(flash_addr, row_buf, SPLASH_ROW_BYTES);
        flash_addr += SPLASH_ROW_BYTES;

        for (uint16_t i = 0; i < SPLASH_ROW_BYTES; i++) {
            lcd_write_data(row_buf[i]);
        }
    }

    /* BricoHams firmware banner over the bottom of the boot image */
    lcd_fill_rect(0, LCD_HEIGHT - 14, LCD_WIDTH, 14, BH_DARK);
    lcd_fill_rect(0, LCD_HEIGHT - 15, LCD_WIDTH, 1, BH_ORANGE);
    lcd_draw_string(8, LCD_HEIGHT - 11, "BricoHams", BH_ORANGE, BH_DARK);
    lcd_draw_string(LCD_WIDTH - 8 - 8 * (sizeof("FW " BH_FW_VERSION) - 1), LCD_HEIGHT - 11,
                    "FW " BH_FW_VERSION, 0xFFFFu, BH_DARK);

    /* Hold splash on screen for the requested duration */
    uint16_t hold = (ms == 0) ? SPLASH_DEFAULT_DELAY_MS : ms;
    delay_ms(hold);
}
