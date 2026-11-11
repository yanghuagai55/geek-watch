#ifndef HAL_H
#define HAL_H

#include <stdint.h>

/* 硬件抽象。ui 目录只调用这里的函数。
   电脑实现在 host/hal_host.c。
   以后 STM32、AT32 各写一份同名函数，界面代码不用改。 */

#ifndef HAL_LCD_W
#define HAL_LCD_W 128
#endif
#ifndef HAL_LCD_H
#define HAL_LCD_H 160
#endif
#define HAL_KEY1 (1u << 0) /* 上一项，应用内是返回 */
#define HAL_KEY2 (1u << 1) /* 确定：放大进入，或切换当前项 */
#define HAL_KEY3 (1u << 2) /* 下一项 */

typedef struct hal_clock {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} hal_clock_t;

uint32_t hal_tick_ms(void);
uint32_t hal_tick_us(void);
void hal_idle(void);
void hal_clock_get(hal_clock_t *out);
/* 手机校时后写入。端口自己负责让时间继续走。 */
void hal_clock_set(const hal_clock_t *in);

/* 蓝牙模块当成串口。读到一字节返回 1。写出的字节发给手机。 */
int hal_link_read(uint8_t *out);
void hal_link_write(const uint8_t *data, int len);

/* 当前按着的键，按位或。端口负责消抖，界面只看这一帧的电平。 */
uint8_t hal_key_level(void);

/* RGB565 buffer stays borrowed until hal_display_busy() returns zero. */
void hal_display_flush(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint16_t *pixels);
int hal_display_busy(void);

/* 包住一帧里真正干活的时间。等待下一帧不计入。 */
void hal_meter_begin(void);
void hal_meter_end(void);

/* 0..1000 表示 0.0%..100.0%，分母是 16.7ms 的帧预算。 */
uint16_t hal_cpu_tenths(void);

void hal_frame_pace(void);

#endif
