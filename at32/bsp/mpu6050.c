#include "mpu6050.h"
#include "i2c_bus.h"

#define ADDR       0x68
#define REG_WHO    0x75
#define REG_PWR1   0x6B
#define REG_PWR2   0x6C
#define REG_SMPLRT 0x19
#define REG_CONFIG 0x1A
#define REG_GYRO   0x1B
#define REG_ACCEL  0x1C
#define REG_DATA   0x3B

static uint8_t ok;
static uint8_t err_cnt;

int mpu6050_present(void) {
    uint8_t id = 0;
    return i2c_read(ADDR, REG_WHO, &id, 1) && id == 0x68u;
}

int mpu6050_init(void) {
    static const uint8_t cfg[] = {
        REG_PWR1,   0x00, /* 唤醒，内部 8MHz */
        REG_PWR2,   0x00,
        REG_SMPLRT, 0x07, /* 1kHz / (1+7) = 125Hz */
        REG_CONFIG, 0x03, /* DLPF 44Hz/42Hz */
        REG_GYRO,   0x18, /* ±2000dps */
        REG_ACCEL,  0x18  /* ±16g */
    };
    uint8_t v;
    int i;

    ok = 0u;
    err_cnt = 0u;
    if (!mpu6050_present()) return 0;
    /* 不写 DEVICE_RESET 或 SIGNAL_PATH_RESET，避免扰动现场模块。 */
    for (i = 0; i < (int)sizeof cfg; i += 2) {
        if (!i2c_write(ADDR, cfg[i], &cfg[i + 1], 1)) return 0;
    }
    for (i = 0; i < (int)sizeof cfg; i += 2) {
        v = 0xFFu;
        if (!i2c_read(ADDR, cfg[i], &v, 1) || v != cfg[i + 1]) return 0;
    }
    ok = 1u;
    return 1;
}

int mpu6050_read(hal_imu_t *out) {
    uint8_t raw[14];
    int16_t ax, ay, az, gx, gy, gz, tp;
    int32_t t100;

    if (!out || !ok) return 0;
    if (!i2c_read(ADDR, REG_DATA, raw, 14)) {
        if (++err_cnt >= 10u) ok = 0u;
        return 0;
    }
    ax = (int16_t)((raw[0] << 8) | raw[1]);
    ay = (int16_t)((raw[2] << 8) | raw[3]);
    az = (int16_t)((raw[4] << 8) | raw[5]);
    tp = (int16_t)((raw[6] << 8) | raw[7]);
    gx = (int16_t)((raw[8] << 8) | raw[9]);
    gy = (int16_t)((raw[10] << 8) | raw[11]);
    gz = (int16_t)((raw[12] << 8) | raw[13]);
    /* ±16g：2048 LSB/g -> mg = raw * 1000 / 2048 = raw * 125 / 256 */
    out->ax = (int16_t)((int32_t)ax * 125 / 256);
    out->ay = (int16_t)((int32_t)ay * 125 / 256);
    out->az = (int16_t)((int32_t)az * 125 / 256);
    if ((out->ax | out->ay | out->az) == 0) {
        if (++err_cnt >= 25u) ok = 0u;
        return 0;
    }
    err_cnt = 0u;
    /* ±2000dps：16.4 LSB/dps -> 0.1dps = raw * 10 / 16.4 = raw * 25 / 41 */
    out->gx = (int16_t)((int32_t)gx * 25 / 41);
    out->gy = (int16_t)((int32_t)gy * 25 / 41);
    out->gz = (int16_t)((int32_t)gz * 25 / 41);
    t100 = (int32_t)tp * 100 / 340 + 3653;
    out->temp = (int16_t)((t100 + (t100 >= 0 ? 5 : -5)) / 10);
    return 1;
}
