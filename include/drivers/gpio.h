/*
 * gpio.h - GPIO driver for AT32F403A on the RT-950 Pro
 *
 * Pin set/clear verified from V0.27 binary:
 *   GPIO_SetPin   @ fw 0x080125B2: str r1,[r0,#0x10]  (SCR +0x10, sets HIGH)
 *   GPIO_ClearPin @ fw 0x080125AE: str r1,[r0,#0x14]  (CLR +0x14, sets LOW)
 */

#ifndef DRIVERS_GPIO_H
#define DRIVERS_GPIO_H

#include "at32f403a.h"

/*
 * gpio_set_pin - Set one or more GPIO pins HIGH.
 * Writes pin mask to GPIO SCR (+0x10), matching OEM 0x080125B2.
 */
void gpio_set_pin(GPIO_TypeDef *port, uint16_t pin);

/*
 * gpio_clear_pin - Set one or more GPIO pins LOW.
 * Writes pin mask to GPIO CLR (+0x14), matching OEM 0x080125AE.
 */
void gpio_clear_pin(GPIO_TypeDef *port, uint16_t pin);

/*
 * gpio_read_pin - Read the state of a single GPIO pin.
 * Returns 1 if pin is HIGH, 0 if LOW.  Reads IDR (+0x08).
 */
uint8_t gpio_read_pin(GPIO_TypeDef *port, uint16_t pin);

/*
 * gpio_config_pin - Configure a single GPIO pin's mode and function.
 *
 * @param port  GPIO port (GPIOA..GPIOE)
 * @param pin   Pin mask (GPIO_PIN_0..GPIO_PIN_15) - must be single pin
 * @param mode  GPIO_MODE_INPUT / GPIO_MODE_OUT_10MHZ / _2MHZ / _50MHZ
 * @param cnf   GPIO_CNF_PP / _OD / _AF_PP / _AF_OD / _ANALOG / _FLOATING / _PULL
 *
 * Modifies CRL (pins 0-7) or CRH (pins 8-15) as appropriate.
 */
void gpio_config_pin(GPIO_TypeDef *port, uint16_t pin, uint8_t mode, uint8_t cnf);

/*
 * gpio_enable_clock - Enable the APB2 clock for the given GPIO port.
 */
void gpio_enable_clock(GPIO_TypeDef *port);

#endif /* DRIVERS_GPIO_H */
