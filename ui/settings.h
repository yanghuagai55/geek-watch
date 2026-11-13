#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

typedef struct settings {
    uint8_t debug;        /* 调试角标 0/1 */
    uint8_t brightness;   /* 0低 1中 2高 */
    uint8_t raise_wrist;  /* 抬腕亮屏 0/1 */
    uint8_t key_beep;     /* 按键音 0/1 */
    uint8_t bluetooth;    /* 蓝牙开关 0/1 */
} settings_t;

void settings_init(void);
settings_t *settings_get(void);

typedef struct about_info {
    const char *version;
    const char *build;
    const char *mcu;
    uint32_t sysclk_mhz;
    const char *clock_src;
    uint16_t battery_mv;
    uint8_t battery_pct;   /* 0xFF 未知 */
} about_info_t;
void about_fill(about_info_t *out);

#endif