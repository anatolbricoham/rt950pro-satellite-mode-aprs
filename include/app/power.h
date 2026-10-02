/*
 * power.h - Power management for the RT-950 Pro
 *
 * Battery monitoring via ADC2 channel 0 (PA0), 12-bit -> 8-bit.
 * Seven thresholds divide ADC range into 8 battery levels.
 * Voltage formula: mV = ADC_8bit x 482 / 10  (verified: 0xBB -> 9.01V).
 * Thresholds loaded from flash calibration at 0xF200.
 *
 * Power button: PE0 monitored for long-press (1.5s) to trigger
 * hardware power-off via PB9 latch release.
 *
 * Auto power-off: countdown timer triggers same hardware power-off.
 * Backlight: GPIO toggle on PC6 + PB3 (no PWM).
 *
 * RE references:
 *   - Battery state machine at 0x2000AF70
 *   - Calibration/power analysis, ISR/timer analysis
 */

#ifndef APP_POWER_H
#define APP_POWER_H

#include <stdint.h>

/* Battery level (0=critical, 7=full) */
typedef enum {
    BATT_CRITICAL = 0,
    BATT_VERY_LOW,
    BATT_LOW,
    BATT_MED_LOW,
    BATT_MEDIUM,
    BATT_MED_HIGH,
    BATT_HIGH,
    BATT_FULL,
} battery_level_t;

/* Initialize power subsystem (PE0 input, default thresholds) */
void power_init(void);

/* Read battery voltage (raw 8-bit ADC value, 12-bit >> 4) */
uint8_t power_read_battery_raw(void);

/* Get battery level enum from current reading */
battery_level_t power_get_battery_level(void);

/* Get battery voltage in millivolts */
uint16_t power_get_battery_mv(void);

/* Set battery thresholds from calibration data (7 values, ascending) */
void power_set_thresholds(const uint8_t thresholds[7]);

/* Auto power-off management */
void power_set_auto_off(uint16_t minutes);  /* 0 = disabled */
void power_reset_idle_timer(void);          /* Call on any user activity */
void power_poll(void);                      /* Call from main loop ~1Hz */

/* Power button (PE0) long-press monitor. Call at ~50Hz.
 * After 1.5 seconds of continuous press, triggers power_off(). */
void power_button_poll(void);

/* Hardware power-off: saves state, disables peripherals, releases
 * PB9 latch. Hardware regulator cuts power. Does not return. */
void power_off(void);

/* MCU reset via NVIC SYSRESETREQ. Use for error recovery. */
void power_reset(void);

/* Backlight control (PC6 + PB3 GPIO toggle) */
void power_backlight_on(void);
void power_backlight_off(void);
void power_backlight_toggle(void);

/*
 * Breathing light - software PWM on backlight GPIO.
 * Call at ~200 Hz from super_loop.  When settings.breathing_light is
 * enabled AND the backlight is in "auto" mode (idle), the backlight
 * fades up and down in a slow triangle wave (~2 s cycle).
 */
void power_breathing_poll(void);

/*
 * Low-battery alert state - true when battery <= BATT_LOW.
 * Used by display to flash battery icon red.
 */
uint8_t power_is_low_batt_alert(void);

/* Backward-compatible shutdown (calls power_off internally) */
void power_shutdown(void);

#endif /* APP_POWER_H */
