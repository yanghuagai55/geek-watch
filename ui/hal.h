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

/* ---- sensors & actuators ----
   板级端口实现这些函数；电脑仿真在 host/hal_host.c 里给假数据。
   ui 核心只通过这些接口拿传感器、控蜂鸣器/灯/背光，不碰芯片寄存器。
   读函数返回 1 表示有有效数据，0 表示该器件不在或没读到。 */

/* 加速度 mg（1g=1000，±16g），陀螺 0.1dps，温度 0.1℃。 */
typedef struct hal_imu {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    int16_t temp;
} hal_imu_t;
int hal_imu_read(hal_imu_t *out);

/* 磁力计 milligauss（±8G）。 */
typedef struct hal_mag {
    int16_t x, y, z;
} hal_mag_t;
int hal_mag_read(hal_mag_t *out);

/* 气压 Pa，温度 0.1℃。 */
typedef struct hal_baro {
    int32_t pressure;
    int16_t temp;
} hal_baro_t;
int hal_baro_read(hal_baro_t *out);

/* 电池电压 mV；0 表示未知。百分比 0..100，0xFF 表示未知。 */
uint16_t hal_battery_mv(void);
uint8_t hal_battery_pct(void);

/* 调试快照：一行 ASCII，含 I2C 扫描、MPU/QMC 状态、QMC 寄存器和电池 mV。
   蓝牙 0x13 命令使用；返回写入长度。 */
int hal_debug_snapshot(char *out, int cap);

/* 扫 I2C 总线，把找到的 7 位地址写入 out，返回个数（最多 max 个）。 */
int hal_i2c_scan(uint8_t *out, int max);

/* 磁力计型号名：QMC5883L / QMC5883P / --。 */
const char *hal_mag_name(void);

/* 蓝牙连接状态：1 已连，0 未连，-1 未知。 */
int hal_ble_connected(void);

/* 蜂鸣器。freq_hz=0 立即停；有源蜂鸣器忽略频率只开关。ms 为持续时间。 */
void hal_beep(uint16_t freq_hz, uint16_t ms);
/* 状态灯。 */
void hal_led_set(int on);
/* 状态灯模式：0 关，1 常亮，2 闪烁（约 1Hz）。 */
void hal_led_mode(int mode);
/* 背光 0..255。 */
void hal_backlight_set(uint8_t level);

/* 整片 SRAM 使用量/总量（字节）。含当前栈，调试角标用。 */
void hal_ram_get(uint32_t *used, uint32_t *total);

/* 固件标识，关于本机用。返回静态字符串。 */
const char *hal_fw_version(void);
const char *hal_fw_build(void);
const char *hal_mcu_name(void);
/* 主频 MHz，时钟来源描述（如 "120MHz HEXT"）。 */
uint32_t hal_sysclk_mhz(void);
const char *hal_clock_src(void);

#endif
