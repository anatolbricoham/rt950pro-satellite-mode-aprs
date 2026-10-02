/*
 * gpio.c - GPIO driver for AT32F403A on the RT-950 Pro
 *
 * Register access verified from V0.27 OEM binary:
 *   GPIO_SetPin   @ 0x080151B2: str r1,[r0,#0x10]  -> SCR (+0x10) = SET
 *   GPIO_ClearPin @ 0x080151AE: str r1,[r0,#0x14]  -> CLR (+0x14) = CLEAR
 *   GPIO_ReadPin  @ 0x080151A8: reads IDR at +0x08
 *
 * OEM backlight function at 0x08017840 confirms:
 *   ON:  BL 0x080151B2 with GPIOC+0x40(PC6) and GPIOB+0x08(PB3)
 *   OFF: BL 0x080151AE with GPIOC+0x40(PC6) and GPIOB+0x08(PB3)
 */

#include "drivers/gpio.h"

/* ========================================================================
 *  gpio_set_pin - Writes pin mask to SCR (+0x10) to set pins HIGH.
 *  OEM: 0x080151B2: str r1,[r0,#0x10]
 * ======================================================================== */

void gpio_set_pin(GPIO_TypeDef *port, uint16_t pin)
{
    port->SCR = pin;
}

/* ========================================================================
 *  gpio_clear_pin - Writes pin mask to CLR (+0x14) to set pins LOW.
 *  OEM: 0x080151AE: str r1,[r0,#0x14]
 * ======================================================================== */

void gpio_clear_pin(GPIO_TypeDef *port, uint16_t pin)
{
    port->CLR = pin;
}

/* ========================================================================
 *  gpio_read_pin - Read a single pin from IDR (+0x08).
 *  Returns 1 if pin is HIGH, 0 if LOW.
 * ======================================================================== */

uint8_t gpio_read_pin(GPIO_TypeDef *port, uint16_t pin)
{
    return (port->IDR & pin) ? 1 : 0;
}

/* ========================================================================
 *  gpio_config_pin - Configure a single pin's mode and function.
 *
 *  CRL controls pins 0-7,  CRH controls pins 8-15.
 *  Each pin uses 4 bits: [CNF1 CNF0 MODE1 MODE0]
 *
 *  @param port  GPIO port
 *  @param pin   Pin mask (must be a single pin: GPIO_PIN_0 .. GPIO_PIN_15)
 *  @param mode  GPIO_MODE_INPUT / _OUT_10MHZ / _OUT_2MHZ / _OUT_50MHZ
 *  @param cnf   GPIO_CNF_PP / _OD / _AF_PP / _AF_OD / _ANALOG / _FLOATING / _PULL
 * ======================================================================== */

void gpio_config_pin(GPIO_TypeDef *port, uint16_t pin, uint8_t mode, uint8_t cnf)
{
    /* Find which pin number (0-15) this mask corresponds to */
    uint8_t pin_num = 0;
    uint16_t tmp = pin;
    while (tmp >>= 1) {
        pin_num++;
    }

    /* Build the 4-bit config value: [CNF1:CNF0:MODE1:MODE0] */
    uint32_t cfg = ((uint32_t)(cnf & 0x03) << 2) | (mode & 0x03);

    if (pin_num < 8) {
        /* CRL - pins 0-7 */
        uint32_t shift = pin_num * 4;
        uint32_t reg = port->CRL;
        reg &= ~(0xFUL << shift);
        reg |= (cfg << shift);
        port->CRL = reg;
    } else {
        /* CRH - pins 8-15 */
        uint32_t shift = (pin_num - 8) * 4;
        uint32_t reg = port->CRH;
        reg &= ~(0xFUL << shift);
        reg |= (cfg << shift);
        port->CRH = reg;
    }

    /* For pull-up input: set ODR bit; for pull-down: clear ODR bit */
    if (mode == GPIO_MODE_INPUT && cnf == GPIO_CNF_PULL) {
        port->ODR |= pin;   /* default to pull-up; caller can clear for pull-down */
    }
}

/* ========================================================================
 *  gpio_enable_clock - Enable APB2 clock for the specified GPIO port.
 * ======================================================================== */

void gpio_enable_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA)      CRM->APB2EN |= CRM_APB2EN_IOPAEN;
    else if (port == GPIOB) CRM->APB2EN |= CRM_APB2EN_IOPBEN;
    else if (port == GPIOC) CRM->APB2EN |= CRM_APB2EN_IOPCEN;
    else if (port == GPIOD) CRM->APB2EN |= CRM_APB2EN_IOPDEN;
    else if (port == GPIOE) CRM->APB2EN |= CRM_APB2EN_IOPEEN;
}
