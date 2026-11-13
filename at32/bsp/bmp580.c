#include "bmp580.h"
#include "i2c_bus.h"

/* 本地无 BMP580 手册：寄存器/换算按 Bosch  datasheet 知识核对，假设项已在汇报标注。 */
#define ADDR 0x46
#define REG_ID 0x01
#define REG_CHIP_STATUS 0x03
#define REG_DATA 0x1D       /* 0x1D-0x1F 温度、0x20-0x22 压力，各按 XLSB/LSB/MSB */
#define REG_ODR 0x36
#define REG_OSR 0x37
#define REG_CMD 0x7E

static int ok;

static int wr(uint8_t reg, uint8_t v) {
    return i2c_write(ADDR, reg, &v, 1);
}

int bmp580_present(void) {
    uint8_t id = 0;
    if (!i2c_read(ADDR, REG_ID, &id, 1)) return 0;
    return id == 0x50;
}

int bmp580_init(void) {
    uint8_t v;
    uint32_t t0;
    ok = 0;
    if (!bmp580_present()) return 0;
    if (!wr(REG_CMD, 0xB6)) return 0;       /* 软复位：启动约 2ms */
    t0 = hal_tick_ms();
    do {
        v = 0;
        (void)i2c_read(ADDR, REG_CHIP_STATUS, &v, 1);
    } while (!(v & 0x01) && (hal_tick_ms() - t0 < 5u));   /* 等 nvm_rdy(bit0)，限时不阻塞 */
    if (!bmp580_present()) return 0;
    if (!wr(REG_ODR, 0x80)) return 0;       /* deep_dis=1 + ODR=0：先进 standby 再改配置 */
    if (!wr(REG_OSR, 0xC9)) return 0;       /* 使能位在 0x37：temp_en(7)|press_en(6)|osr x2，不是 0x31/0x32 */
    if (!wr(REG_ODR, 0x8F)) return 0;       /* deep_dis=1 + ~50Hz 连续；漏 deep_dis 会停在深待机 */
    ok = 1;
    return 1;
}

int bmp580_read(hal_baro_t *out) {
    uint8_t b[6];
    uint32_t p;
    int32_t t;
    if (!out || !ok) return 0;
    if (!i2c_read(ADDR, REG_DATA, b, 6)) return 0;   /* 温度在前压力在后；从 0x20 读会拿错温度 */
    t = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16));
    p = (uint32_t)b[3] | ((uint32_t)b[4] << 8) | ((uint32_t)b[5] << 16);
    if (t & 0x800000) t -= 0x1000000;       /* 温度 24-bit 有符号，压力无符号 */
    if (p == 0) return 0;                   /* 使能后首个 ODR周期前尚无有效样本 */
    out->pressure = (int32_t)((p + 32u) / 64u);   /* 1/64 Pa 每 LSB */
    out->temp = (int16_t)((t * 10 + (t >= 0 ? 32768 : -32768)) / 65536);   /* 1/65536 ℃ 每 LSB -> 0.1℃ */
    return 1;
}