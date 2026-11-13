#include "mpu6050.h"
#include "i2c_bus.h"

#define ADDR 0x68
#define REG_WHO 0x75
#define REG_PWR1 0x6B
#define REG_SMPLRT 0x19
#define REG_CONFIG 0x1A
#define REG_GYRO 0x1B
#define REG_ACCEL 0x1C
#define REG_DATA 0x3B

static int ok;

static int wr(uint8_t reg, uint8_t v) {
    return i2c_write(ADDR, reg, &v, 1);
}

int mpu6050_present(void) {
    uint8_t id = 0;
    if (!i2c_read(ADDR, REG_WHO, &id, 1)) return 0;
    return id == 0x68;
}

int mpu6050_init(void) {
    uint8_t v;
    uint32_t t0;
    ok = 0;
    if (!mpu6050_present()) return 0;
    if (!wr(REG_PWR1, 0x80)) return 0;      /* DEVICE_RESET：清掉上次运行残留配置 */
    t0 = hal_tick_ms();
    do {
        v = 0x80;
        (void)i2c_read(ADDR, REG_PWR1, &v, 1);   /* 复位期间可能 NACK，视为未完成 */
    } while ((v & 0x80) && (hal_tick_ms() - t0 < 100u));
    if (!mpu6050_present()) return 0;       /* bit7 复位完成自动清零；超时读不回 ID 即失败 */
    if (!wr(REG_PWR1, 0x01)) return 0;      /* 清 SLEEP 唤醒 + CLKSEL=1：PLL/X 陀螺，手册强烈推荐 */
    if (!wr(REG_SMPLRT, 0x07)) return 0;    /* DLPF 开启时陀螺输出 1kHz：1kHz/(1+7)=125Hz */
    if (!wr(REG_CONFIG, 0x03)) return 0;    /* DLPF_CFG=3：加速度 44Hz / 陀螺 42Hz */
    if (!wr(REG_GYRO, 0x18)) return 0;      /* FS_SEL=3：±2000dps */
    if (!wr(REG_ACCEL, 0x18)) return 0;     /* AFS_SEL=3：±16g */
    ok = 1;
    return 1;
}

int mpu6050_read(hal_imu_t *out) {
    uint8_t b[14];
    int16_t ax, ay, az, gx, gy, gz, tp;
    int32_t t100;
    if (!out || !ok) return 0;
    if (!i2c_read(ADDR, REG_DATA, b, 14)) return 0;   /* 突发读保证 14 字节同一采样时刻 */
    ax = (int16_t)((b[0] << 8) | b[1]);
    ay = (int16_t)((b[2] << 8) | b[3]);
    az = (int16_t)((b[4] << 8) | b[5]);
    tp = (int16_t)((b[6] << 8) | b[7]);
    gx = (int16_t)((b[8] << 8) | b[9]);
    gy = (int16_t)((b[10] << 8) | b[11]);
    gz = (int16_t)((b[12] << 8) | b[13]);
    out->ax = (int16_t)((int32_t)ax * 1000 / 2048);   /* ±16g：2048 LSB/g -> mg */
    out->ay = (int16_t)((int32_t)ay * 1000 / 2048);
    out->az = (int16_t)((int32_t)az * 1000 / 2048);
    out->gx = (int16_t)((int32_t)gx * 25 / 41);       /* ±2000dps：16.4 LSB/dps -> 0.1dps = *10/16.4 */
    out->gy = (int16_t)((int32_t)gy * 25 / 41);
    out->gz = (int16_t)((int32_t)gz * 25 / 41);
    t100 = (int32_t)tp * 100 / 340 + 3653;            /* ℃ = raw/340 + 36.53，先算 0.01℃ 再舍入 */
    out->temp = (int16_t)((t100 + (t100 >= 0 ? 5 : -5)) / 10);
    return 1;
}