#ifndef BEEP_H
#define BEEP_H

#include <stdint.h>

void beep_init(void);
void beep_set(uint16_t freq_hz, uint16_t ms);
void beep_tick(uint32_t now_ms);

#endif