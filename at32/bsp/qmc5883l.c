#include "qmc5883l.h"
#include "i2c_bus.h"

#include <stdio.h>

/* GY-273/GY-271 常见两种芯片，寄存器布局不同：
     QMC5883L  7 位地址 0x0D，ID 寄存器 0x0D 读回 0xFF
     QMC5883P  7 位地址 0x2C，ID 寄存器 0x00 读回 0x80
   上电自动探测，谁应答就用谁的初始化/读数流程。 */
#define ADDR_L 0x0D
#define ADDR_P 0x2C

#define L_REG_DATA     0x00
#define L_REG_STATUS   0x06
#define L_REG_CTRL1    0x09
#define L_REG_CTRL2    0x0A
#define L_REG_SETRESET 0x0B
#define L_REG_ID       0x0D

#define P_REG_ID      0x00
#define P_REG_DATA    0x01
#define P_REG_STATUS  0x09
#define P_REG_CTRL1   0x0A
#define P_REG_CTRL2   0x0B

static int ok;
static int chip;   /* 0=无, 1=QMC5883L, 2=QMC5883P */
static uint8_t addr;

static int rd(uint8_t reg, uint8_t *v) {
    return i2c_read(addr, reg, v, 1);
}

static int wr(uint8_t reg, uint8_t v) {
    return i2c_write(addr, reg, &v, 1);
}

int qmc5883l_chip(void) {
    return chip;
}

int qmc5883l_present(void) {
    uint8_t id = 0;

    chip = 0;
    addr = ADDR_L;
    if (i2c_read(ADDR_L, L_REG_ID, &id, 1) && id == 0xFF) {
        chip = 1;
        return 1;
    }
    addr = ADDR_P;
    id = 0;
    if (i2c_read(ADDR_P, P_REG_ID, &id, 1) && id == 0x80) {
        chip = 2;
        return 1;
    }
    return 0;
}

static int init_l(void) {
    uint8_t v;
    uint32_t t0;

    if (!wr(L_REG_CTRL2, 0x80)) return 0;     /* SOFT_RST */
    t0 = hal_tick_ms();
    do {
        v = 0x80;
        (void)rd(L_REG_CTRL2, &v);
    } while ((v & 0x80) && (hal_tick_ms() - t0 < 5u));
    if (!wr(L_REG_SETRESET, 0x01)) return 0;  /* 手册强制 0x0B=0x01 */
    if (!wr(L_REG_CTRL1, 0x19)) return 0;     /* OSR512 | ±8G | 100Hz | 连续 */
    return 1;
}

static int init_p(void) {
    uint8_t id = 0;

    /* 按 QMC5883P 手册 7.1 正常模式例程：0x29 轴符号 -> 0x0B -> 0x0A。
       不要先软复位：这块模块复位后紧随的配置会被清掉（实测 C1/C2 读回 0）。 */
    if (!wr(0x29, 0x06)) return 0;            /* X/Y/Z 轴符号 */
    if (!wr(P_REG_CTRL2, 0x08)) return 0;     /* set/reset on | ±8G */
    if (!wr(P_REG_CTRL1, 0xCD)) return 0;     /* normal | 200Hz | OSR8 | DSR8 */
    if (!rd(P_REG_ID, &id) || id != 0x80) return 0;
    return 1;
}

int qmc5883l_init(void) {
    ok = 0;
    if (!qmc5883l_present()) return 0;
    if (chip == 1) {
        if (!init_l()) return 0;
    } else {
        if (!init_p()) return 0;
    }
    ok = 1;
    return 1;
}

static int read_l(hal_mag_t *out) {
    uint8_t b[6];
    uint8_t st = 0;
    int16_t x, y, z;

    if (!rd(L_REG_STATUS, &st)) return 0;
    if (!(st & 0x01)) return 0;               /* DRDY=0 */
    if (!i2c_read(addr, L_REG_DATA, b, 6)) return 0;
    x = (int16_t)((b[1] << 8) | b[0]);
    y = (int16_t)((b[3] << 8) | b[2]);
    z = (int16_t)((b[5] << 8) | b[4]);
    out->x = (int16_t)(x / 3);                /* ±8G：3000 LSB/G -> mG */
    out->y = (int16_t)(y / 3);
    out->z = (int16_t)(z / 3);
    return 1;
}

static int read_p(hal_mag_t *out) {
    uint8_t b[6];
    uint8_t st = 0;
    int16_t x, y, z;

    if (!rd(P_REG_STATUS, &st)) return 0;
    if (!(st & 0x01)) return 0;               /* DRDY=0 */
    if (!i2c_read(addr, P_REG_DATA, b, 6)) return 0;
    x = (int16_t)((b[1] << 8) | b[0]);
    y = (int16_t)((b[3] << 8) | b[2]);
    z = (int16_t)((b[5] << 8) | b[4]);
    /* ±8G：3750 LSB/G -> mG 为 raw * 1000 / 3750 = raw * 4 / 15 */
    out->x = (int16_t)((int32_t)x * 4 / 15);
    out->y = (int16_t)((int32_t)y * 4 / 15);
    out->z = (int16_t)((int32_t)z * 4 / 15);
    return 1;
}

int qmc5883l_read(hal_mag_t *out) {
    if (!out || !ok || chip == 0) return 0;
    return chip == 1 ? read_l(out) : read_p(out);
}

void qmc5883l_debug(char *out, int cap) {
    uint8_t id = 0, st = 0, c1 = 0, c2 = 0, sr = 0;

    if (!out || cap <= 0) return;
    if (!qmc5883l_present()) {
        snprintf(out, (size_t)cap, "QMC=NONE");
        return;
    }
    if (chip == 1) {
        (void)rd(L_REG_ID, &id);
        (void)rd(L_REG_STATUS, &st);
        (void)rd(L_REG_CTRL1, &c1);
        (void)rd(L_REG_CTRL2, &c2);
        (void)rd(L_REG_SETRESET, &sr);
        snprintf(out, (size_t)cap,
                 "QMC5883L@0D;ID=%02X;ST=%02X;C1=%02X;C2=%02X;SR=%02X",
                 id, st, c1, c2, sr);
    } else {
        (void)rd(P_REG_ID, &id);
        (void)rd(P_REG_STATUS, &st);
        (void)rd(P_REG_CTRL1, &c1);
        (void)rd(P_REG_CTRL2, &c2);
        snprintf(out, (size_t)cap,
                 "QMC5883P@2C;ID=%02X;ST=%02X;C1=%02X;C2=%02X",
                 id, st, c1, c2);
    }
}
