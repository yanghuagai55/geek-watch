#ifndef ADC_BAT_H
#define ADC_BAT_H

#include <stdint.h>

void adc_init(void);
uint16_t adc_read_mv(uint8_t channel);
uint16_t battery_mv(void);
uint8_t battery_pct(void);

#endif