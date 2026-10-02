/*
 * vox.h - Voice-Operated Switch for the RT-950 Pro
 *
 * Uses BK4829 hardware VOX via REG 0x31 bit 1.
 * 9 sensitivity levels; threshold written to REG 0x71.
 * Anti-VOX suppresses triggering during active speaker output.
 *
 * Derived from RE analysis of V0.27 binary:
 *   VOX threshold table at 0x0802B7FC (9 entries -> REG 0x71)
 *   PTT transitions REG 0x30 between RX (0xBFF1) and TX (0xC3FA)
 */

#ifndef APP_VOX_H
#define APP_VOX_H

#include <stdint.h>

/* VOX sensitivity levels 0=off, 1-9=active (1=most sensitive, 9=least) */
void vox_init(void);
void vox_set_level(uint8_t level);   /* 0-9 */
uint8_t vox_get_level(void);
void vox_poll(void);                 /* Call from main loop, checks trigger */
uint8_t vox_is_triggered(void);

#endif /* APP_VOX_H */
