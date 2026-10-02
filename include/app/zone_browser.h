/*
 * zone_browser.h - Zone selection browser for the RT-950 Pro
 *
 * 10 zones at flash 0x00C000 (16 bytes per name).
 * Zone selection filters which channels are visible in memory mode.
 */

#ifndef APP_ZONE_BROWSER_H
#define APP_ZONE_BROWSER_H

#include <stdint.h>

/* Open zone selection browser */
void zone_browser_open(void);
void zone_browser_close(void);
uint8_t zone_browser_is_active(void);

/* Input handlers */
void zone_browser_handle_key(uint8_t key);
void zone_browser_handle_encoder(int8_t direction);

/* Draw zone list */
void zone_browser_draw(void);

/* Get currently selected zone (0-9), or 0xFF if none */
uint8_t zone_browser_get_selected(void);

/* Read zone name from flash (0-9) into buf (16 bytes) */
void zone_read_name(uint8_t index, char *buf);

#endif /* APP_ZONE_BROWSER_H */
