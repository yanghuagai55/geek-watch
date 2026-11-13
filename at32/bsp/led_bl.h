#ifndef LED_BL_H
#define LED_BL_H

#include <stdint.h>

void led_init(void);
void led_set(int on);
void backlight_init(void);
void backlight_set(uint8_t level);
int ble_state_read(void);

#endif