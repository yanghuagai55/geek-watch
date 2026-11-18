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
static uint8_t err_cnt;

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
        REG_SMPLRT, 0x07, /* 1kHz / (1+7) = 125Hz */
        REG_CONFIG, 0x03, /* DLPF 44Hz/42Hz */
        REG_GYRO,   0x18, /* ±2000dps */
        REG_ACCEL,  0x18, /* ±16g */
        REG_PWR1,   0x00  /* 最后清 SLEEP，CLKSEL=0 内部 8MHz 振荡器 */
    };
    uint8_t v = 0;
    int i;

    ok = 0;
    err_cnt = 0;
    if (!mpu6050_present()) return 0;
    /* 不做 DEVICE_RESET：这块芯片复位后会拒绝后续配置写（只有 PWR1 生效，
       数据恒 0）。直接覆盖配置寄存器，写完回读校验。 */
    for (i = 0; i < (int)sizeof cfg; i += 2) {
        if (!wr(cfg[i], cfg[i + 1])) return 0;
    }
    if (!i2c_read(ADDR, REG_CONFIG, &v, 1) || v != 0x03u) return 0;
    if (!i2c_read(ADDR, REG_PWR1, &v, 1) || (v & 0x40u)) return 0;
    ok = 1;
    return 1;
}

int mpu6050_read(hal_imu_t *out) {
    uint8_t b[14];
    int16_t ax, ay, az, gx, gy, gz, tp;
    int32_t t100;
    if (!out || !ok) return 0;
    {
        int i;
        /* 这片 MPU6050 的连续读（auto-increment）在硬件 I2C 上会返回 0/超时，
           按寄存器逐个读才稳定；每次 1 字节，14 字节约 2ms@400kHz。 */
        for (i = 0; i < 14; i++) {
            if (!i2c_read(ADDR, (uint8_t)(REG_DATA + i), &b[i], 1)) {
                if (++err_cnt >= 10u) ok = 0;
                return 0;
            }
        }
    }
    ax = (int16_t)((b[0] << 8) | b[1]);
    ay = (int16_t)((b[2] << 8) | b[3]);
    az = (int16_t)((b[4] << 8) | b[5]);
    tp = (int16_t)((b[6] << 8) | b[7]);
    gx = (int16_t)((b[8] << 8) | b[9]);
    gy = (int16_t)((b[10] << 8) | b[11]);
    gz = (int16_t)((b[12] << 8) | b[13]);
    if ((ax | az | tp) == 0) {
        if (++err_cnt >= 25u) ok = 0;
        return 0;
    }
    err_cnt = 0;
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