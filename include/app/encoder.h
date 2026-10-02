/*
 * encoder.h - Rotary encoder driver for the RT-950 Pro
 *
 * Quadrature encoder on PB4 (channel A) and PB5 (channel B).
 * State machine reverse-engineered from V0.27 binary @ fw 0x0800D710.
 */

#ifndef APP_ENCODER_H
#define APP_ENCODER_H

#include <stdint.h>

/* Event codes matching OEM firmware convention (V0.27 @ 0x08010710).
 * OEM: CW = 0x14 (20), CCW = 0x16 (22), debounce holdoff = 0xC8 (200). */
#define ENC_EVT_NONE    0x00
#define ENC_EVT_CW      0x14    /* clockwise detent (OEM verified) */
#define ENC_EVT_CCW     0x16    /* counter-clockwise detent (OEM verified) */

/* --- API ---------------------------------------------------------------- */

/* Configure PB4 and PB5 as floating inputs. Call once at startup. */
void encoder_init(void);

/*
 * Poll the encoder and return direction.
 * Call frequently (e.g. every 1-2 ms from a timer ISR or main loop).
 * Returns: +1 = CW, -1 = CCW, 0 = no movement.
 */
int8_t encoder_poll(void);

#endif /* APP_ENCODER_H */
