#include "mpu6050.h"
#include "i2c_bus.h"

#define ADDR 0x68
#define REG_WHO 0x75
#define REG_PWR1 0x6B
#define REG_PWR2 0x6C
#define REG_SMPLRT 0x19
#define REG_CONFIG 0x1A
#define REG_GYRO 0x1B
#define REG_ACCEL 0x1C
#define REG_STATUS 0x3A
#define REG_DATA 0x3B

static int ok;
static uint8_t err_cnt;
volatile uint8_t g_mpu_diag[2];
uint8_t g_mpu_raw[14];

static int wr(uint8_t reg, uint8_t v) {
    return i2c_write(ADDR, reg, &v, 1);
}

int mpu6050_present(void) {
    uint8_t id = 0;
    if (!i2c_read(ADDR, REG_WHO, &id, 1)) return 0;
    return id == 0x68;
}

int mpu6050_init(void) {
    static const uint8_t cfg[] = {
        REG_PWR1,   0x00, /* 唤醒，内部 8MHz */
        REG_PWR2,   0x00,
        REG_SMPLRT, 0x07,
        REG_CONFIG, 0x03,
        REG_GYRO,   0x18,
        REG_ACCEL,  0x18
    };
    uint8_t v = 0;
    int i;

    ok = 0;
    err_cnt = 0;
    if (!mpu6050_present()) return 0;
    /* 不写 DEVICE_RESET 或 SIGNAL_PATH_RESET，避免再次扰动现场模块。 */
    for (i = 0; i < (int)sizeof cfg; i += 2) {
        if (!wr(cfg[i], cfg[i + 1])) return 0;
    }
    for (i = 0; i < (int)sizeof cfg; i += 2) {
        v = 0xFFu;
        if (!i2c_read(ADDR, cfg[i], &v, 1)) return 0;
        if (v != cfg[i + 1]) return 0;
    }
    ok = 1;
    return 1;
}

int mpu6050_read(hal_imu_t *out) {
    uint8_t status = 0;
    int16_t ax, ay, az, gx, gy, gz, tp;
    int32_t t100;
    if (!out || !ok) return 0;
    g_mpu_diag[1] = 0xF1u;
    if (!i2c_read(ADDR, REG_STATUS, &status, 1)) return 0;
    g_mpu_diag[0] = status;
    if (!i2c_read(ADDR, REG_DATA, g_mpu_raw, (int)sizeof g_mpu_raw)) {
        g_mpu_diag[1] = 0xF2u;
        if (++err_cnt >= 10u) ok = 0;
        return 0;
    }
    ax = (int16_t)((g_mpu_raw[0] << 8) | g_mpu_raw[1]);
    ay = (int16_t)((g_mpu_raw[2] << 8) | g_mpu_raw[3]);
    az = (int16_t)((g_mpu_raw[4] << 8) | g_mpu_raw[5]);
    tp = (int16_t)((g_mpu_raw[6] << 8) | g_mpu_raw[7]);
    gx = (int16_t)((g_mpu_raw[8] << 8) | g_mpu_raw[9]);
    gy = (int16_t)((g_mpu_raw[10] << 8) | g_mpu_raw[11]);
    gz = (int16_t)((g_mpu_raw[12] << 8) | g_mpu_raw[13]);
    out->ax = (int16_t)((int32_t)ax * 1000 / 2048);
    out->ay = (int16_t)((int32_t)ay * 1000 / 2048);
    out->az = (int16_t)((int32_t)az * 1000 / 2048);
    if (out->ax == 0 && out->ay == 0 && out->az == 0) {
        g_mpu_diag[1] = 0xF3u;
        if (++err_cnt >= 25u) ok = 0;
        return 0;
    }
    err_cnt = 0;
    out->gx = (int16_t)((int32_t)gx * 25 / 41);
    out->gy = (int16_t)((int32_t)gy * 25 / 41);
    out->gz = (int16_t)((int32_t)gz * 25 / 41);
    t100 = (int32_t)tp * 100 / 340 + 3653;
    out->temp = (int16_t)((t100 + (t100 >= 0 ? 5 : -5)) / 10);
    g_mpu_diag[1] = 0xFFu;
    return 1;
}
