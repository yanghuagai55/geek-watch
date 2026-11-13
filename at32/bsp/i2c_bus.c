#include "i2c_bus.h"
#include "board_config.h"
#include "at32f421_gpio.h"

/* 开漏输出：写1释放（外部上拉拉高），写0拉低。读 SDA 直接读 idt。 */
#define SCL_H()  gpio_bits_set(I2C_PORT, I2C_SCL_PIN)
#define SCL_L()  gpio_bits_reset(I2C_PORT, I2C_SCL_PIN)
#define SDA_H()  gpio_bits_set(I2C_PORT, I2C_SDA_PIN)
#define SDA_L()  gpio_bits_reset(I2C_PORT, I2C_SDA_PIN)
#define SDA_RD() ((I2C_PORT->idt & I2C_SDA_PIN) != 0)

/* 120MHz 下 20 次空循环约 600kHz，超过 MPU6050/QMC5883L 的 400kHz 上限，
   多字节突发读会偶发失败。放慢到约 200kHz，给传感器留足时序余量。 */
static void dly(void) {
    volatile int i;
    for (i = 0; i < 60; i++) {
    }
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
static int tx_byte(uint8_t b) {
    int i, ack;
    for (i = 7; i >= 0; i--) {
        if (b & (1u << i)) SDA_H(); else SDA_L();
        dly();
        SCL_H(); dly();
        SCL_L(); dly();
    }
    SDA_H(); dly();          /* 释放 SDA 读 ACK */
    SCL_H(); dly();
    ack = !SDA_RD();
    SCL_L(); dly();
    return ack;
}

static uint8_t rx_byte(int ack) {
    int i;
    uint8_t b = 0;
    SDA_H();
    for (i = 7; i >= 0; i--) {
        SCL_H(); dly();
        if (SDA_RD()) b |= (uint8_t)(1u << i);
        SCL_L(); dly();
    }
    if (ack) SDA_L(); else SDA_H();
    dly();
    SCL_H(); dly();
    SCL_L(); dly();
    SDA_H();
    return b;
}

int i2c_probe(uint8_t addr7) {
    int ok;
    start();
    ok = tx_byte((uint8_t)(addr7 << 1));
    stop();
    return ok;
}

int i2c_write(uint8_t addr7, uint8_t reg, const uint8_t *data, int n) {
    int i;
    start();
    if (!tx_byte((uint8_t)(addr7 << 1))) { stop(); return 0; }
    if (!tx_byte(reg)) { stop(); return 0; }
    for (i = 0; i < n; i++) {
        if (!tx_byte(data[i])) { stop(); return 0; }
    }
    stop();
    return 1;
}

int i2c_read(uint8_t addr7, uint8_t reg, uint8_t *data, int n) {
    int i;
    start();
    if (!tx_byte((uint8_t)(addr7 << 1))) { stop(); return 0; }
    if (!tx_byte(reg)) { stop(); return 0; }
    start();                              /* repeated start */
    if (!tx_byte((uint8_t)((addr7 << 1) | 1))) { stop(); return 0; }
    for (i = 0; i < n; i++) {
        data[i] = rx_byte(i < n - 1);
    }
    stop();
    return 1;
}