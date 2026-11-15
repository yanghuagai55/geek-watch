#ifndef IMU9_H
#define IMU9_H

#include "hal.h"
#include <stdint.h>

/* 统一 9 轴融合模块（纯定点整型，零浮点开销）。
 *
 * 统一表体坐标系 Body Frame B（面向屏幕正面，右手系）：
 *   +X: 屏幕右侧
 *   +Y: 屏幕顶部（12 点钟方向 / 传感器模块排针侧）
 *   +Z: 垂直屏幕朝外（平放桌面时指天）
 * 世界坐标系 World ENU：
 *   +X: 东 East，+Y: 北 North，+Z: 天 Up
 */
typedef struct imu9_state {
    int16_t ax, ay, az;      /* 对齐后加速度 mg */
    int16_t gx, gy, gz;      /* 去零偏角速度 0.1 dps */
    int16_t mx, my, mz;      /* 对齐并减硬铁偏置后磁场 mG */
    int16_t temp;            /* IMU 温度 0.1 C */

    int16_t gbx, gby, gbz;   /* 学习到的陀螺零偏 0.1 dps */
    int16_t mox, moy, moz;   /* 磁力计硬铁偏置 mG */

    int16_t roll_d10;        /* -1800..+1800 (0.1 deg，右倾/右边下沉为正) */
    int16_t pitch_d10;       /*  -900..+900  (0.1 deg，顶边朝天抬起为正) */
    int16_t yaw_d10;         /* -1800..+1800 (0.1 deg，ENU 逆时针绕天轴) */
    uint16_t heading_deg;    /*     0..359   (deg，顺时针磁航向 0=N 90=E) */

    uint8_t imu_ok;          /* 1000ms 内收到有效 IMU */
    uint8_t mag_ok;          /* 1000ms 内收到有效磁力计 */
    uint8_t fused_axes;      /* 参与融合轴数: 0, 3, 6, 9 */
    uint8_t gyro_cal;        /* 0=学习中, 1=零偏已稳定 */
    uint8_t mag_cal;         /* 0=未校准, 1=采集中(晃动360度), 2=已校准 */
} imu9_state_t;

void imu9_init(void);
void imu9_reset(void);
void imu9_cal_start(void);
void imu9_feed(const hal_imu_t *imu, const hal_mag_t *mag, uint32_t now_ms);
void imu9_get(imu9_state_t *out);
int imu9_is_stale(uint32_t now_ms, uint32_t max_age_ms);

#endif
