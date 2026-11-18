#include "qmc5883l.h"
#include "i2c_bus.h"

#include <stdio.h>

/* 硬件实际焊装为矽睿 QMC5883P（7 位地址 0x2C，CHIPID[0x00] = 0x80）。
   按官方数据手册 Rev.E §7.1 正常模式例程初始化：
     0x29 = 0x06（定义 X/Y/Z 轴符号极性）
     0x0B = 0x08（Set/Reset 开启，量程 ±8G = 3750 LSB/G）
     0x0A = 0xCD（Normal 模式，ODR=200Hz，OSR1=8，OSR2=8） */
#define ADDR_P       0x2C
#define P_REG_ID     0x00
#define P_REG_DATA   0x01
#define P_REG_STATUS 0x09
#define P_REG_CTRL1  0x0A
#define P_REG_CTRL2  0x0B
#define P_REG_SIGN   0x29

static uint8_t ok;
static uint8_t err_cnt;

int qmc5883l_chip(void) {
    return ok ? 2 : 0;
}

int qmc5883l_present(void) {
    uint8_t id = 0;
    return i2c_read(ADDR_P, P_REG_ID, &id, 1) && id == 0x80u;
}

int qmc5883l_init(void) {
    static const uint8_t cfg[] = {
        P_REG_SIGN,  0x06,
        P_REG_CTRL2, 0x08,
        P_REG_CTRL1, 0xCD
    };
    int i;

    ok = 0u;
    err_cnt = 0u;
    if (!qmc5883l_present()) return 0;
    for (i = 0; i < (int)sizeof cfg; i += 2) {
        if (!i2c_write(ADDR_P, cfg[i], &cfg[i + 1], 1)) return 0;
    }
    if (!qmc5883l_present()) return 0;
    ok = 1u;
    return 1;
}

int qmc5883l_read(hal_mag_t *out) {
    uint8_t b[6];
    int16_t x, y, z;

    if (!out || !ok) return 0;
    /* Normal 200Hz 连续更新且读取期间内部自动锁存 0x01..0x06，直接一次读 6 字节 */
    if (!i2c_read(ADDR_P, P_REG_DATA, b, 6)) {
        if (++err_cnt >= 10u) ok = 0u;
        return 0;
    }
    err_cnt = 0u;
    x = (int16_t)((b[1] << 8) | b[0]);
    y = (int16_t)((b[3] << 8) | b[2]);
    z = (int16_t)((b[5] << 8) | b[4]);
    /* ±8G：3750 LSB/G -> mG = raw * 1000 / 3750 = raw * 4 / 15 */
    out->x = (int16_t)((int32_t)x * 4 / 15);
    out->y = (int16_t)((int32_t)y * 4 / 15);
    out->z = (int16_t)((int32_t)z * 4 / 15);
    return 1;
}

void qmc5883l_debug(char *out, int cap) {
    uint8_t id = 0, st = 0, c1 = 0, c2 = 0;

    if (!out || cap <= 0) return;
    if (!qmc5883l_present()) {
        snprintf(out, (size_t)cap, "QMC=NONE");
        return;
    }
    (void)i2c_read(ADDR_P, P_REG_ID, &id, 1);
    (void)i2c_read(ADDR_P, P_REG_STATUS, &st, 1);
    (void)i2c_read(ADDR_P, P_REG_CTRL1, &c1, 1);
    (void)i2c_read(ADDR_P, P_REG_CTRL2, &c2, 1);
    snprintf(out, (size_t)cap,
             "QMC5883P@2C;ID=%02X;ST=%02X;C1=%02X;C2=%02X",
             id, st, c1, c2);
}
