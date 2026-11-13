#ifndef LED_BL_H
#define LED_BL_H

#include <stdint.h>

enum { LED_MODE_OFF = 0, LED_MODE_ON = 1, LED_MODE_BLINK = 2 };

void led_init(void);
void led_set(int on);
/* 0 关 / 1 常亮 / 2 闪烁（500ms 亮、500ms 灭）。 */
void led_mode_set(int mode);
void led_tick(uint32_t now_ms);
void backlight_init(void);
void backlight_set(uint8_t level);
int ble_state_read(void);

#endif