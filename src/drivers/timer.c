/*
 * timer.c - Timer ISR architecture for the RT-950 Pro
 *
 * TIM4 @ 1200 Hz - CTCSS tone detection/generation timing
 * TIM3 @ 1000 Hz - Fast system tick (VOX sampling, scanner dwell, etc.)
 *
 * TIM2 is RESERVED for OEM-style ADC1 triggering (PSC=0, ARR=12500,
 * 4.8 kHz). OEM peripheral_init @ 0x080033D8 configures TIM2 for this
 * purpose. CTCSS was moved from TIM2 to TIM4 to avoid this conflict.
 *
 * Both timers use APB1 timer clock at 120 MHz (APB1 bus = 60 MHz, doubled
 * when prescaler > 1) with PSC=119 -> 1 MHz counter tick.
 * ISR handlers dispatch to registered callbacks (max 4 each).
 */

#include "drivers/timer.h"
#include "cortex_m4.h"

/* TIM CR1 / DIER / SR / EGR bits ----------------------------------- */
#define TIM_CR1_CEN     (1UL << 0)
#define TIM_DIER_UIE    (1UL << 0)      /* Update interrupt enable */
#define TIM_SR_UIF      (1UL << 0)      /* Update interrupt flag */
#define TIM_EGR_UG      (1UL << 0)      /* Force update generation */

/* Callback tables --------------------------------------------------- */

static timer_callback_t ctcss_cbs[TIMER_MAX_CALLBACKS];
static uint8_t          ctcss_cb_count;

static timer_callback_t fast_cbs[TIMER_MAX_CALLBACKS];
static uint8_t          fast_cb_count;

static volatile uint32_t fast_tick_counter;

/* ISR: TIM4 - CTCSS tick @ 1200 Hz --------------------------------- */

void TIM4_IRQHandler(void)
{
    if (TIM4->SR & TIM_SR_UIF) {
        TIM4->SR = ~TIM_SR_UIF;        /* Clear flag (rc_w0) */

        for (uint8_t i = 0; i < ctcss_cb_count; i++)
            ctcss_cbs[i]();
    }
}

/* ISR: TIM3 - Fast tick @ 1000 Hz ---------------------------------- */

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF) {
        TIM3->SR = ~TIM_SR_UIF;

        fast_tick_counter++;

        for (uint8_t i = 0; i < fast_cb_count; i++)
            fast_cbs[i]();
    }
}

/* Public API -------------------------------------------------------- */

void timer_init(void)
{
    /* Enable APB1 clocks for TIM4 and TIM3 (TIM2 reserved for ADC trigger) */
    CRM->APB1EN |= CRM_APB1EN_TIM4EN | CRM_APB1EN_TIM3EN;

    /* TIM4: CTCSS @ 1200 Hz (moved from TIM2) ----------------------- */
    TIM4->CR1 = 0;                      /* Stop */
    TIM4->PSC = TIM4_PSC;
    TIM4->ARR = TIM4_ARR;
    TIM4->CNT = 0;
    TIM4->EGR = TIM_EGR_UG;            /* Load shadow registers */
    TIM4->SR  = 0;                      /* Clear all flags */
    TIM4->DIER = TIM_DIER_UIE;         /* Enable update IRQ */

    NVIC_SetPriority(TIM4_IRQn, 2);    /* OEM-aligned priority */
    NVIC_EnableIRQ(TIM4_IRQn);

    TIM4->CR1 = TIM_CR1_CEN;           /* Start */

    /* TIM3: Fast tick @ 1000 Hz ----------------------------------- */
    TIM3->CR1 = 0;
    TIM3->PSC = TIM3_PSC;
    TIM3->ARR = TIM3_ARR;
    TIM3->CNT = 0;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR  = 0;
    TIM3->DIER = TIM_DIER_UIE;

    NVIC_SetPriority(TIM3_IRQn, 3);    /* Below CTCSS and UARTs */
    NVIC_EnableIRQ(TIM3_IRQn);

    TIM3->CR1 = TIM_CR1_CEN;

    /* Init callback tables */
    ctcss_cb_count = 0;
    fast_cb_count = 0;
    fast_tick_counter = 0;
}

int timer_register_ctcss_cb(timer_callback_t cb)
{
    if (ctcss_cb_count >= TIMER_MAX_CALLBACKS || !cb) return -1;
    ctcss_cbs[ctcss_cb_count++] = cb;
    return 0;
}

int timer_register_fast_cb(timer_callback_t cb)
{
    if (fast_cb_count >= TIMER_MAX_CALLBACKS || !cb) return -1;
    fast_cbs[fast_cb_count++] = cb;
    return 0;
}

uint32_t timer_get_fast_tick(void)
{
    return fast_tick_counter;
}
