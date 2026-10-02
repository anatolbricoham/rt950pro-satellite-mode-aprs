/*
 * dac.h - DAC driver for AT32F403A on the RT-950 Pro
 *
 * DAC channel 1 output on PA4 - used for audio TX.
 */

#ifndef DRIVERS_DAC_H
#define DRIVERS_DAC_H

#include "at32f403a.h"

void dac_init(void);
void dac_write(uint16_t value);

#endif /* DRIVERS_DAC_H */
