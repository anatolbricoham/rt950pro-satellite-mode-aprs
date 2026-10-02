/*
 * event.h - Lightweight event queue for the RT-950 Pro
 *
 * Decouples producers (ISRs, input drivers) from consumers (app logic).
 * Fixed-size ring buffer, no dynamic allocation, ISR-safe for single
 * producer (post from ISR) + single consumer (poll from main loop).
 *
 * Usage:
 *   event_post(EVT_KEY_PRESS, KEY_1);
 *   ...
 *   event_t ev;
 *   while (event_poll(&ev)) {
 *       switch (ev.type) { ... }
 *   }
 */

#ifndef KERNEL_EVENT_H
#define KERNEL_EVENT_H

#include <stdint.h>

/* Event types - extend as needed */
typedef enum {
    EVT_NONE = 0,

    /* Input */
    EVT_KEY_PRESS,          /* param = key code */
    EVT_KEY_RELEASE,        /* param = key code */
    EVT_KEY_LONG_PRESS,     /* param = key code */
    EVT_ENCODER_CW,         /* param = step count */
    EVT_ENCODER_CCW,        /* param = step count */

    /* PTT / side keys */
    EVT_PTT_PRESS,          /* param = 0=main, 1=side, 2=external */
    EVT_PTT_RELEASE,        /* param = same */
    EVT_SIDEKEY_PRESS,      /* param = key number (1-4) */
    EVT_SIDEKEY_RELEASE,    /* param = key number */

    /* Power */
    EVT_POWER_BUTTON,       /* param = 0=short, 1=long */
    EVT_BATTERY_LOW,        /* param = battery_level_t */
    EVT_BATTERY_CRITICAL,   /* param = 0 */

    /* Radio */
    EVT_SQUELCH_OPEN,       /* param = chip (0 or 1) */
    EVT_SQUELCH_CLOSE,      /* param = chip */
    EVT_CTCSS_MATCH,        /* param = chip */
    EVT_TX_TIMEOUT,         /* param = 0 */

    /* GPS */
    EVT_GPS_FIX,            /* param = sat count */
    EVT_GPS_LOST,           /* param = 0 */

    /* System */
    EVT_TIMER_EXPIRE,       /* param = timer id */
    EVT_CPS_CONNECT,        /* param = 0 */
    EVT_BT_CONNECT,         /* param = 0 */

    EVT_COUNT               /* sentinel */
} event_type_t;

typedef struct {
    uint8_t  type;          /* event_type_t */
    uint16_t param;
} event_t;

/* Initialize event queue (call once at startup) */
void event_init(void);

/* Post an event. Returns 0 on success, -1 if queue full (oldest dropped).
 * Safe to call from ISR context (single-producer assumption). */
int event_post(event_type_t type, uint16_t param);

/* Poll for next event. Returns 1 if event retrieved, 0 if empty.
 * Call from main loop only. */
int event_poll(event_t *out);

/* Check if queue has pending events (non-destructive) */
int event_pending(void);

/* Flush all pending events */
void event_flush(void);

#endif /* KERNEL_EVENT_H */
