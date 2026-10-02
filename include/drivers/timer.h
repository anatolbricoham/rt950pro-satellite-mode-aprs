/*
 * timer.h - Timer driver for AT32F403A on the RT-950 Pro
 *
 * Timer allocation:
 *
 *   TIM2 - RESERVED for OEM ADC1 triggering at 4.8 kHz (PSC=0, ARR=12500).
 *           OEM peripheral_init @ 0x080033D8 configures TIM2→ADC1 trigger.
 *           NOT used for CTCSS (moved to TIM4 to match OEM TIM2 role).
 *
 *   TIM3 - System fast tick (1000 Hz / 1 ms)
 *           OEM uses SysTick for periodic timing; our TIM3 is custom.
 *
 *   TIM4 - CTCSS tone generation/detection tick (1200 Hz)
 *           Moved from TIM2 to avoid conflict with OEM ADC trigger role.
 *
 *   TIM6 - DAC audio sample clock (owned by dac_audio.c, not managed here)
 *           OEM uses TIM6 as TRGO trigger for DAC (no ISR needed).
 *
 * Timer clock: APB1 timers run at 120 MHz
 *   (APB1 bus = 60 MHz, prescaler > 1 -> timer clock is doubled)
 */

#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include "at32f403a.h"
#include <stdint.h>

/* Timer clock ------------------------------------------------------- */
#define TIM_APB1_CLOCK_HZ   120000000UL

/* TIM4: CTCSS tick @ 1200 Hz (moved from TIM2 to avoid OEM ADC conflict) */
#define TIM4_FREQ_HZ        1200U
#define TIM4_PSC             (120U - 1U)        /* 120 MHz / 120 = 1 MHz tick */
#define TIM4_ARR             (1000000U / TIM4_FREQ_HZ - 1U)  /* 833 counts */

/* TIM3: fast tick @ 1000 Hz ----------------------------------------- */
#define TIM3_FREQ_HZ        1000U
#define TIM3_PSC             (120U - 1U)        /* 1 MHz tick */
#define TIM3_ARR             (1000000U / TIM3_FREQ_HZ - 1U)  /* 999 counts */

/* Callback registration --------------------------------------------- */
#define TIMER_MAX_CALLBACKS  4

typedef void (*timer_callback_t)(void);

/*
 * timer_init - Enable TIM2 + TIM3 clocks, configure prescaler/ARR,
 *              enable update interrupts, start counters.
 */
void timer_init(void);

/*
 * timer_register_ctcss_cb - Register a callback for the 1200 Hz CTCSS tick.
 * timer_register_fast_cb  - Register a callback for the 1000 Hz fast tick.
 *
 * Returns 0 on success, -1 if callback table is full.
 */
int timer_register_ctcss_cb(timer_callback_t cb);
int timer_register_fast_cb(timer_callback_t cb);

/*
 * timer_get_fast_tick - Returns the number of TIM3 ticks since init.
 *                       Wraps at 2^32 (~49.7 days at 1 kHz).
 */
uint32_t timer_get_fast_tick(void);

#endif /* DRIVERS_TIMER_H */
