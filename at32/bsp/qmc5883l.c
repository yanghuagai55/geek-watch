#include "qmc5883l.h"
#include "i2c_bus.h"

#define ADDR 0x0D
#define REG_DATA 0x00
#define REG_STATUS 0x06
#define REG_CTRL1 0x09
#define REG_CTRL2 0x0A
#define REG_SETRESET 0x0B
#define REG_ID 0x0D

static int ok;

static int wr(uint8_t reg, uint8_t v) {
    return i2c_write(ADDR, reg, &v, 1);
}

int qmc5883l_present(void) {
    uint8_t id = 0;
    if (!i2c_read(ADDR, REG_ID, &id, 1)) return 0;
    return id == 0xFF;
}

int qmc5883l_init(void) {
    uint8_t v;
    uint32_t t0;
    ok = 0;
    if (!qmc5883l_present()) return 0;
    if (!wr(REG_CTRL2, 0x80)) return 0;     /* SOFT_RST：全部寄存器回默认并回到 standby */
    t0 = hal_tick_ms();
    do {
        v = 0x80;
        (void)i2c_read(ADDR, REG_CTRL2, &v, 1);
    } while ((v & 0x80) && (hal_tick_ms() - t0 < 5u));   /* 复位完成约 350us，限时等待 */
    if (!wr(REG_SETRESET, 0x01)) return 0;  /* 手册强制 0x0B=0x01：AMR 置位/复位周期，缺了读数漂移 */
    if (!wr(REG_CTRL1, 0x19)) return 0;     /* OSR=512 | RNG=±8G | ODR=100Hz | MODE=连续 */
    ok = 1;
    return 1;
}

int qmc5883l_read(hal_mag_t *out) {
    uint8_t b[6];
    uint8_t st = 0;
    int16_t x, y, z;
    if (!out || !ok) return 0;
    if (!i2c_read(ADDR, REG_STATUS, &st, 1)) return 0;
    if (!(st & 0x01)) return 0;             /* DRDY=0：无新数据 */
    if (!i2c_read(ADDR, REG_DATA, b, 6)) return 0;   /* 必须连读 6 字节到 0x05 才解除数据锁存 */
    x = (int16_t)((b[1] << 8) | b[0]);
    y = (int16_t)((b[3] << 8) | b[2]);
    z = (int16_t)((b[5] << 8) | b[4]);
    out->x = (int16_t)(x / 3);              /* ±8G：3000 LSB/G -> milligauss = raw/3 */
    out->y = (int16_t)(y / 3);
    out->z = (int16_t)(z / 3);
    return 1;
}