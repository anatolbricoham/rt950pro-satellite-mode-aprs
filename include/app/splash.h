/*
 * splash.h - Boot splash display for the RT-950 Pro
 *
 * Reads a 240x320 RGB565 image (153,600 bytes) from SPI flash at
 * FLASH_ADDR_SPLASH (0x090000) and writes it to the ST7789V LCD.
 *
 * RE reference: function at 0x0800E928, 10 blocks of 32 rows each.
 */

#ifndef APP_SPLASH_H
#define APP_SPLASH_H

#include <stdint.h>

/* Display the boot splash image from SPI flash (default 1000ms delay) */
void splash_show(void);

/* Show splash with custom delay in ms (0 = use default 1000ms) */
void splash_show_with_delay(uint16_t ms);

#endif /* APP_SPLASH_H */
