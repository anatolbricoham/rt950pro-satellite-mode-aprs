/*
 * adc.c - ADC driver for AT32F403A on the RT-950 Pro
 *
 * Uses ADC2 for single-shot conversions, matching V0.27 OEM firmware:
 *   ADC_Read_PA1 @ fw 0x08013820 - audio level, channel 1, 8-bit result
 *   ADC_Read_PA0 @ fw 0x0801385C - battery voltage, channel 0, 8-bit result
 *   ADC_Init     @ fw 0x080227AC - prescaler /6, cal, SW trigger enable
 *
 * Both OEM read functions follow the same pattern:
 *   1. adc_regular_channel_set(ADC2, ch, seq=1, sample_time=7)  [239.5 cycles]
 *   2. adc_ordinary_software_trigger_enable(ADC2, TRUE)
 *   3. Poll CCE flag (end of conversion)
 *   4. Clear CCE flag
 *   5. Read ODT register, extract bits [11:4] via UBFX -> 8-bit result
 */

#include "drivers/adc.h"
#include "drivers/gpio.h"

/* ========================================================================
 *  adc_init - Enable ADC2 clock, configure pins, power on, calibrate.
 *
 *  OEM ADC_Init @ fw 0x080227AC:
 *    1. CRM CFGR ADCPRE = /6 -> ADC clock = APB2 60 MHz / 6 = 10 MHz
 *    2. APB2EN |= 0x600 (enables both ADC1 + ADC2 clocks)
 *    3. adc_init(ADC2, default config)
 *    4. adc_regular_channel_set(ADC2, ch=1, seq=2, smp=7)
 *    5. adc_enable(ADC2, TRUE)  [CR2 |= ADON]
 *    6. Reset calibration (CR2 |= RSTCAL, wait)
 *    7. Start calibration (CR2 |= CAL, wait)
 *    8. adc_ordinary_software_trigger_enable(ADC2, TRUE)
 * ======================================================================== */

void adc_init(void)
{
    /* Set ADC prescaler to PCLK2/6 (60 MHz / 6 = 10 MHz)
     * OEM: CRM_CFGR &= 0xEFFF3FFF; CRM_CFGR |= 0x8000 @ fw 0x080227B4 */
    CRM->CFGR = (CRM->CFGR & ~CRM_CFGR_ADCPRE_MASK) | CRM_CFGR_ADCPRE_DIV6;

    /* Enable ADC1 + ADC2 peripheral clocks (OEM enables both @ fw 0x080227BC) */
    CRM->APB2EN |= CRM_APB2EN_ADC1EN | CRM_APB2EN_ADC2EN;

    /* Configure PA0, PA1, PA2 as analog input (GPIOA clock already enabled).
     * PA2 = ADC12_CH2 (RSSI) - OEM adc2_convert_read reads PA2 first. */
    gpio_config_pin(GPIOA, GPIO_PIN_0, GPIO_MODE_INPUT, GPIO_CNF_ANALOG);
    gpio_config_pin(GPIOA, GPIO_PIN_1, GPIO_MODE_INPUT, GPIO_CNF_ANALOG);
    gpio_config_pin(GPIOA, GPIO_PIN_2, GPIO_MODE_INPUT, GPIO_CNF_ANALOG);

    /* Configure ADC2: single conversion, right-aligned, SW trigger */
    ADC2->CR1 = 0;
    ADC2->CR2 = ADC_CR2_ADON | ADC_CR2_EXTTRIG | ADC_CR2_EXTSEL_SWSTART;

    /* Brief stabilization delay (per datasheet: tSTAB ~ 1 us) */
    for (volatile uint32_t i = 0; i < 72; i++) {
        /* ~1 us at 120 MHz with loop overhead */
    }

    /* Reset calibration (OEM: CR2 |= 8 @ fw 0x08022800) */
    ADC2->CR2 |= ADC_CR2_RSTCAL;
    while (ADC2->CR2 & ADC_CR2_RSTCAL) {
        /* wait for reset complete */
    }

    /* Start calibration (OEM: CR2 |= 4 @ fw 0x08022810) */
    ADC2->CR2 |= ADC_CR2_CAL;
    while (ADC2->CR2 & ADC_CR2_CAL) {
        /* wait for calibration complete */
    }
}

/* ========================================================================
 *  adc_read_channel - Single-shot conversion on the given channel.
 *
 *  OEM uses sample time = 7 (239.5 cycles, the maximum) for both channels.
 *  Verified from ADC_Read_PA1 @ fw 0x08013820: movs r3, 7 (SMP arg)
 *  and ADC_Read_PA0 @ fw 0x0801385C: movs r3, 7
 *
 *  Returns 12-bit right-aligned result (0-4095).
 * ======================================================================== */

uint16_t adc_read_channel(uint8_t channel)
{
    /* Set sample time for channel (239.5 cycles, matching OEM) */
    if (channel < 10) {
        /* SMPR2 covers channels 0-9, 3 bits each */
        uint32_t shift = (uint32_t)channel * 3;
        uint32_t reg = ADC2->SMPR2;
        reg &= ~(7UL << shift);
        reg |= ((uint32_t)ADC_SMP_239_5 << shift);
        ADC2->SMPR2 = reg;
    } else {
        /* SMPR1 covers channels 10-17, 3 bits each */
        uint32_t shift = ((uint32_t)channel - 10) * 3;
        uint32_t reg = ADC2->SMPR1;
        reg &= ~(7UL << shift);
        reg |= ((uint32_t)ADC_SMP_239_5 << shift);
        ADC2->SMPR1 = reg;
    }

    /* Set regular sequence: 1 conversion, channel in SQR3[4:0] */
    ADC2->SQR1 = 0;                        /* L[23:20] = 0 -> 1 conversion */
    ADC2->SQR3 = (uint32_t)channel & 0x1F; /* SQ1[4:0] = channel */

    /* Clear EOC flag */
    ADC2->SR = 0;

    /* Start conversion via SWSTART */
    ADC2->CR2 |= ADC_CR2_SWSTART;

    /* Wait for end of conversion */
    while (!(ADC2->SR & ADC_SR_EOC)) {
        /* spin */
    }

    /* Read and return 12-bit result */
    return (uint16_t)(ADC2->DR & 0x0FFF);
}

/* ========================================================================
 *  adc_read_battery - Read battery voltage on PA0 (channel 0).
 *
 *  OEM ADC_Read_PA0 @ fw 0x0801385C returns 8-bit (UBFX bits[11:4]).
 * ======================================================================== */

uint8_t adc_read_battery(void)
{
    return (uint8_t)(adc_read_channel(0) >> 4);
}

/* ========================================================================
 *  adc_read_audio_level - Read audio level on PA1 (channel 1).
 *
 *  OEM ADC_Read_PA1 @ fw 0x08013820 returns 8-bit (UBFX bits[11:4]).
 *  ubfx r0, r0, 4, 8 = (result >> 4) & 0xFF
 * ======================================================================== */

uint8_t adc_read_audio_level(void)
{
    return (uint8_t)(adc_read_channel(1) >> 4);
}

/* ========================================================================
 *  adc_read_rssi - Read RSSI on PA2 (channel 2).
 *
 *  OEM adc2_convert_read @ fw 0x080227AC reads PA2 as first ADC sample
 *  after init. Used for signal strength indication.
 *  Returns 8-bit value (12-bit >> 4), same scaling as other OEM ADC reads.
 * ======================================================================== */

uint8_t adc_read_rssi(void)
{
    return (uint8_t)(adc_read_channel(2) >> 4);
}
