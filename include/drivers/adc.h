/*
 * adc.h - ADC driver for AT32F403A on the RT-950 Pro
 *
 * Uses ADC2, matching V0.27 OEM firmware:
 *   ADC_Read_PA1 @ fw 0x08013820 - audio level, channel 1, 8-bit result
 *   ADC_Read_PA0 @ fw 0x0801385C - battery voltage, channel 0, 8-bit result
 *   ADC_Init     @ fw 0x080227AC - clock enable, prescaler, calibration
 *
 * OEM configuration (V0.27 verified):
 *   - ADC prescaler = /6  (CRM_CFG |= 0x8000 @ fw 0x080227B4, 10 MHz)
 *   - PA0 sample time = 239.5 cycles (SMP=7 @ fw 0x08013826)
 *   - PA1 returns upper 8 bits via UBFX(DR, 4, 8) @ fw 0x08013852
 *   - Clock enables ADC1+ADC2 (APB2EN |= 0x600 @ fw 0x080227BC)
 *   - Single-shot software-triggered (SWSTART in CR2)
 *   - ADC2 base: 0x40012800 (literal pool @ 0x08016858, 0x08016894)
 *
 * Pin assignments:
 *   PA0 / ADC12_IN0 - Battery voltage sense
 *   PA1 / ADC12_IN1 - Audio input level
 *   PA2 / ADC12_IN2 - RSSI (signal strength)
 */

#ifndef DRIVERS_ADC_H
#define DRIVERS_ADC_H

#include "at32f403a.h"

/* ADC Status Register (SR) bits -------------------------------------- */
#define ADC_SR_AWD          (1UL << 0)      /* Analog watchdog flag */
#define ADC_SR_EOC          (1UL << 1)      /* End of conversion */
#define ADC_SR_JEOC         (1UL << 2)      /* Injected end of conversion */
#define ADC_SR_JSTRT        (1UL << 3)      /* Injected channel start */
#define ADC_SR_STRT         (1UL << 4)      /* Regular channel start */

/* ADC Control Register 1 (CR1) bits ---------------------------------- */
#define ADC_CR1_AWDCH_MASK  (0x1FUL << 0)   /* Analog watchdog channel */
#define ADC_CR1_EOCIE       (1UL << 5)      /* EOC interrupt enable */
#define ADC_CR1_AWDIE       (1UL << 6)      /* AWD interrupt enable */
#define ADC_CR1_JEOCIE      (1UL << 7)      /* JEOC interrupt enable */
#define ADC_CR1_SCAN        (1UL << 8)      /* Scan mode */
#define ADC_CR1_AWDSGL      (1UL << 9)      /* AWD on single channel */
#define ADC_CR1_JAUTO       (1UL << 10)     /* Auto injected group */
#define ADC_CR1_DISCEN      (1UL << 11)     /* Discontinuous on regular */
#define ADC_CR1_JDISCEN     (1UL << 12)     /* Discontinuous on injected */
#define ADC_CR1_JAWDEN      (1UL << 22)     /* AWD on injected channels */
#define ADC_CR1_AWDEN       (1UL << 23)     /* AWD on regular channels */

/* ADC Control Register 2 (CR2) bits ---------------------------------- */
#define ADC_CR2_ADON        (1UL << 0)      /* A/D converter ON */
#define ADC_CR2_CONT        (1UL << 1)      /* Continuous conversion */
#define ADC_CR2_CAL         (1UL << 2)      /* A/D calibration */
#define ADC_CR2_RSTCAL      (1UL << 3)      /* Reset calibration */
#define ADC_CR2_DMA         (1UL << 8)      /* DMA mode */
#define ADC_CR2_ALIGN       (1UL << 11)     /* Data alignment (1=left) */
#define ADC_CR2_JEXTSEL_MASK (7UL << 12)    /* Injected trigger select */
#define ADC_CR2_JEXTTRIG    (1UL << 15)     /* Injected external trigger */
#define ADC_CR2_EXTSEL_MASK (7UL << 17)     /* Regular trigger select */
#define ADC_CR2_EXTSEL_SWSTART (7UL << 17)  /* SWSTART as trigger */
#define ADC_CR2_EXTTRIG     (1UL << 20)     /* External trigger enable */
#define ADC_CR2_JSWSTART    (1UL << 21)     /* Injected SW start */
#define ADC_CR2_SWSTART     (1UL << 22)     /* Regular SW start */

/* ADC sample time constants (SMPR1/SMPR2 - 3 bits per channel) ------ */
#define ADC_SMP_1_5         0x0U    /*   1.5 cycles */
#define ADC_SMP_7_5         0x1U    /*   7.5 cycles */
#define ADC_SMP_13_5        0x2U    /*  13.5 cycles */
#define ADC_SMP_28_5        0x3U    /*  28.5 cycles */
#define ADC_SMP_41_5        0x4U    /*  41.5 cycles */
#define ADC_SMP_55_5        0x5U    /*  55.5 cycles */
#define ADC_SMP_71_5        0x6U    /*  71.5 cycles */
#define ADC_SMP_239_5       0x7U    /* 239.5 cycles */

/* ADC prescaler (in CRM->CFGR bits [15:14]) ------------------------- */
#define CRM_CFGR_ADCPRE_MASK    (3UL << 14)
#define CRM_CFGR_ADCPRE_DIV2    (0UL << 14)
#define CRM_CFGR_ADCPRE_DIV4    (1UL << 14)
#define CRM_CFGR_ADCPRE_DIV6    (2UL << 14)
#define CRM_CFGR_ADCPRE_DIV8    (3UL << 14)

/* Functions ---------------------------------------------------------- */

/*
 * adc_init - Enable ADC2 clock, configure PA0/PA1 as analog,
 *            set prescaler, power on, and calibrate.
 */
void adc_init(void);

/*
 * adc_read_channel - Single-shot conversion on ADC2.
 * Returns 12-bit result (0-4095).
 */
uint16_t adc_read_channel(uint8_t channel);

/*
 * adc_read_battery - Read PA0 (ADC12_IN0) battery voltage.
 * Returns 8-bit value (bits [11:4] of 12-bit result), matching OEM.
 */
uint8_t adc_read_battery(void);

/*
 * adc_read_audio_level - Read PA1 (ADC12_IN1) audio level.
 * Returns 8-bit value (12-bit >> 4), matching stock firmware scaling.
 */
uint8_t adc_read_audio_level(void);

/*
 * adc_read_rssi - Read PA2 (ADC12_IN2) signal strength.
 * Returns 8-bit value (12-bit >> 4), matching OEM scaling.
 */
uint8_t adc_read_rssi(void);

#endif /* DRIVERS_ADC_H */
