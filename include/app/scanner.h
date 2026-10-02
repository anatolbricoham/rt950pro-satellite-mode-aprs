/*
 * scanner.h - VFO and memory channel scanning for the RT-950 Pro
 *
 * State machine:
 *   SCAN_OFF -> scanner_start() -> SCANNING
 *   SCANNING: step to next freq/channel each poll tick
 *     -> squelch opens (signal found) -> PAUSED
 *   PAUSED:
 *     CARRIER resume: resume when squelch closes (carrier drops)
 *     TIME resume:    resume after countdown expires
 *     STOP resume:    wait for manual scanner_start() or scanner_stop()
 */

#ifndef APP_SCANNER_H
#define APP_SCANNER_H

#include <stdint.h>

/* Scan modes */
typedef enum {
    SCAN_OFF = 0,
    SCAN_VFO,       /* Sweep VFO frequency range by step */
    SCAN_MEMORY,    /* Step through memory channels with scan-add flag */
} scan_mode_t;

/* Scan resume modes */
typedef enum {
    SCAN_RESUME_CARRIER = 0, /* Resume when carrier drops (squelch closes) */
    SCAN_RESUME_TIME,        /* Resume after timeout (e.g., 5 seconds) */
    SCAN_RESUME_STOP,        /* Stop on signal, manual resume */
} scan_resume_t;

/* Initialize scanner */
void scanner_init(void);

/* Start scanning in given mode */
void scanner_start(scan_mode_t mode);

/* Stop scanning */
void scanner_stop(void);

/* Get current scan state */
scan_mode_t scanner_get_mode(void);

/* Is scanner currently paused on a signal? */
uint8_t scanner_is_paused(void);

/* Set scan resume behavior */
void scanner_set_resume(scan_resume_t resume);

/* Set scan speed (ms per step, default 100) */
void scanner_set_speed(uint16_t ms_per_step);

/* Set VFO scan range (0 = use full band) */
void scanner_set_vfo_range(uint32_t start_hz, uint32_t stop_hz);

/* Poll function - call from main loop at ~100Hz.
 * Handles stepping, squelch checking, resume timing. */
void scanner_poll(void);

/* Get currently scanning channel number (memory mode) or 0xFFFF */
uint16_t scanner_get_channel(void);

#endif /* APP_SCANNER_H */
