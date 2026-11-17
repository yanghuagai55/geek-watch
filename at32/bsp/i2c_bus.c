#include "i2c_bus.h"
#include "board_config.h"
#include "at32f421_gpio.h"

/* 开漏输出：写1释放（外部上拉拉高），写0拉低。读 SDA 直接读 idt。 */
#define SCL_H()  gpio_bits_set(I2C_PORT, I2C_SCL_PIN)
#define SCL_L()  gpio_bits_reset(I2C_PORT, I2C_SCL_PIN)
#define SDA_H()  gpio_bits_set(I2C_PORT, I2C_SDA_PIN)
#define SDA_L()  gpio_bits_reset(I2C_PORT, I2C_SDA_PIN)
#define SCL_RD() ((I2C_PORT->idt & I2C_SCL_PIN) != 0)
#define SDA_RD() ((I2C_PORT->idt & I2C_SDA_PIN) != 0)

/* 120MHz 下 20 次空循环约 600kHz，超过 MPU6050/QMC5883L 的 400kHz 上限，
   多字节突发读会偶发失败。传感器使用 dly(60) 约 200kHz；AT24C256 顺序读支持 400kHz+，
   使用 dly_n(30) 约 380kHz 提速近一倍。 */
static void dly_n(int loops) {
    volatile int i;
    for (i = 0; i < loops; i++) {
    }
}

static void dly(void) {
    dly_n(60);
}

#ifdef __GNUC__
#define I2C_NOINLINE __attribute__((noinline))
#else
#define I2C_NOINLINE
#endif

I2C_NOINLINE static void scl_high_wait(int loops) {
    int g = 64;
    SCL_H();
    while (!SCL_RD() && g-- > 0) {
    }
    dly_n(loops);
}

/* 9 周期总线恢复：清除热复位/异常中断遗留的 SDA 锁死状态 */
void i2c_recover(void) {
    int i;
    SDA_H();
    SCL_H();
    dly();
    for (i = 0; i < 9 && !SDA_RD(); i++) {
        SCL_L(); dly();
        scl_high_wait(60);
    }
    SDA_L(); dly();
    scl_high_wait(60);
    SDA_H(); dly();
}

void i2c_bus_init(void) {
    gpio_init_type g;

    crm_periph_clock_enable(I2C_CRM_CLK, TRUE);
    gpio_default_para_init(&g);
    g.gpio_pins = I2C_SCL_PIN | I2C_SDA_PIN;
    g.gpio_mode = GPIO_MODE_OUTPUT;
    g.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
    g.gpio_pull = GPIO_PULL_UP;
    g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(I2C_PORT, &g);
    SCL_H();
    SDA_H();
    i2c_recover();
}

static void start(void) {
    SDA_H(); SCL_H(); dly();
    SDA_L(); dly();
    SCL_L(); dly();
}

static void stop(void) {
    SDA_L(); dly();
    SCL_H(); dly();
    SDA_H(); dly();
}

/* 返回 1=收到 ACK */
I2C_NOINLINE static int tx_byte(uint8_t b) {
    int i, ack;
    for (i = 7; i >= 0; i--) {
        if (b & (1u << i)) SDA_H(); else SDA_L();
        dly();
        scl_high_wait(60);
        SCL_L(); dly();
    }
    SDA_H(); dly();          /* 释放 SDA 读 ACK */
    scl_high_wait(60);
    ack = !SDA_RD();
    SCL_L(); dly();
    return ack;
}

I2C_NOINLINE static uint8_t rx_byte_n(int ack, int loops) {
    int i;
    uint8_t b = 0;
    SDA_H();
    for (i = 7; i >= 0; i--) {
        scl_high_wait(loops);
        if (SDA_RD()) b |= (uint8_t)(1u << i);
        SCL_L(); dly_n(loops);
    }
    if (ack) SDA_L(); else SDA_H();
    dly_n(loops);
    scl_high_wait(loops);
    SCL_L(); dly_n(loops);
    SDA_H();
    return b;
}

static int i2c_begin_reg(uint8_t addr7, uint16_t reg, int is16) {
    start();
    if (!tx_byte((uint8_t)(addr7 << 1))) return 0;
    if (is16 && !tx_byte((uint8_t)(reg >> 8))) return 0;
    if (!tx_byte((uint8_t)(reg & 0xFFu))) return 0;
    return 1;
}

I2C_NOINLINE static int i2c_wr_common(uint8_t addr7, uint16_t reg, int is16, const uint8_t *data, int n) {
    int i, ok = 0;
    if (i2c_begin_reg(addr7, reg, is16)) {
        ok = 1;
        for (i = 0; i < n; i++) {
            if (!tx_byte(data[i])) { ok = 0; break; }
        }
    }
    stop();
    return ok;
}

I2C_NOINLINE static int i2c_rd_common(uint8_t addr7, uint16_t reg, int is16, uint8_t *data, int n, int loops) {
    int i, ok = 0;
    if (i2c_begin_reg(addr7, reg, is16)) {
        start();
        if (tx_byte((uint8_t)((addr7 << 1) | 1))) {
            for (i = 0; i < n; i++) {
                data[i] = rx_byte_n(i < n - 1, loops);
            }
            ok = 1;
        }
    }
    stop();
    return ok;
}

int i2c_probe(uint8_t addr7) {
    int ok;
    start();
    ok = tx_byte((uint8_t)(addr7 << 1));
    stop();
    return ok;
}

int i2c_write(uint8_t addr7, uint8_t reg, const uint8_t *data, int n) {
    return i2c_wr_common(addr7, reg, 0, data, n);
}

int i2c_read(uint8_t addr7, uint8_t reg, uint8_t *data, int n) {
    return i2c_rd_common(addr7, reg, 0, data, n, 60);
}

int i2c_write16(uint8_t addr7, uint16_t reg16, const uint8_t *data, int n) {
    return i2c_wr_common(addr7, reg16, 1, data, n);
}

int i2c_read16(uint8_t addr7, uint16_t reg16, uint8_t *data, int n) {
    return i2c_rd_common(addr7, reg16, 1, data, n, 30);
}