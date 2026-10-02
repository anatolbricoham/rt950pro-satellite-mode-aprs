/*
 * crossband.h - Cross-band repeat for the RT-950 Pro
 *
 * Uses the two independent BK4829 chips to relay audio between VFO A and
 * VFO B.  When enabled, a signal detected on VFO-B (CHIP0) triggers TX
 * on VFO-A (CHIP1) and vice versa, relaying received audio across bands.
 *
 * Requires relay mode 2 (audio path) to route RX audio from one chip
 * to the TX modulation input of the other.  The exact audio routing
 * depends on hardware relay board wiring (needs hardware validation).
 */

#ifndef APP_CROSSBAND_H
#define APP_CROSSBAND_H

#include <stdint.h>

typedef enum {
    XBAND_OFF = 0,
    XBAND_A_TO_B,     /* RX on A -> TX on B */
    XBAND_B_TO_A,     /* RX on B -> TX on A */
    XBAND_DUPLEX       /* Both directions */
} xband_mode_t;

/* Enable/disable cross-band repeat */
void crossband_set_mode(xband_mode_t mode);
xband_mode_t crossband_get_mode(void);

/*
 * Poll function (call at ~5 Hz from main loop).
 * Monitors squelch on the receive VFO and keys up the transmit VFO
 * when signal is detected.  Includes carrier hang time before drop.
 */
void crossband_poll(void);

/* Returns 1 if cross-band repeat is actively relaying */
int crossband_is_relaying(void);

#endif /* APP_CROSSBAND_H */
