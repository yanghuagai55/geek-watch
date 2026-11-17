#include "i2c_bus.h"
#include "board_config.h"
#include "at32f421_gpio.h"
#include "at32f421_crm.h"
#include "at32f421_i2c.h"

/* 硬件 I2C1：PA9=SCL / PA10=SDA（MUX4，外部 10k 上拉），400kHz 快速模式。
   MPU6050 / QMC5883L(P) / BMP580 / AT24C256 共用这条总线。
   DMA 不可用：I2C1_RX 与 LCD 的 SPI1_TX 都固定映射 DMA1 Ch3。
   总线异常时软件复位 I2C1 并重新初始化。 */

#define I2C_HW        I2C1
#define I2C_TO_SHORT  4000u
#define I2C_TO_LONG   40000u

static void pins_i2c(void) {
    gpio_init_type g;

    gpio_pin_mux_config(I2C_PORT, GPIO_PINS_SOURCE9, GPIO_MUX_4);
    gpio_pin_mux_config(I2C_PORT, GPIO_PINS_SOURCE10, GPIO_MUX_4);
    gpio_default_para_init(&g);
    g.gpio_pins = I2C_SCL_PIN | I2C_SDA_PIN;
    g.gpio_mode = GPIO_MODE_MUX;
    g.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
    g.gpio_pull = GPIO_PULL_UP;
    g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(I2C_PORT, &g);
}

static void hw_start(void) {
    /* APB1 = 120MHz；400kHz 快速模式 2:1 -> speed = 120M/(400k*3) = 100 */
    I2C_HW->ctrl1_bit.i2cen = 0;
    I2C_HW->ctrl1_bit.reset = 1;
    I2C_HW->ctrl1_bit.reset = 0;
    I2C_HW->ctrl2_bit.clkfreq = 120;
    I2C_HW->clkctrl_bit.speed = 100;
    I2C_HW->clkctrl_bit.dutymode = 0;
    I2C_HW->clkctrl_bit.speedmode = 1;
    I2C_HW->tmrise_bit.risetime = 37;
    I2C_HW->ctrl1_bit.stretch = 0;   /* 0 = 允许从机拉低 SCL 做时钟延展 */
    I2C_HW->ctrl1_bit.acken = 1;
    I2C_HW->ctrl1_bit.i2cen = 1;
}

void i2c_bus_init(void) {
    crm_periph_clock_enable(I2C_CRM_CLK, TRUE);
    crm_periph_clock_enable(CRM_I2C1_PERIPH_CLOCK, TRUE);
    pins_i2c();
    hw_start();
}

void i2c_recover(void) {
    /* 软件复位 I2C1 后重配。 */
    hw_start();
}

static void clear_err(void) {
    /* sts1 低位标志写 0 清除；ACKFAIL 是粘滞位，不清会让后续事务全部秒失败。 */
    I2C_HW->sts1 = (uint16_t)~(I2C_ACKFAIL_FLAG | I2C_BUSERR_FLAG |
                               I2C_ARLOST_FLAG | I2C_OUF_FLAG);
}

static int wait_idle(void) {
    uint32_t n = I2C_TO_LONG;
    while ((I2C_HW->sts2 & 0x2u) != 0u && --n != 0u) {
    }
    return n != 0u;
}

static int start_cond(uint8_t addr7, int rd) {
    uint32_t n = I2C_TO_SHORT;

    I2C_HW->ctrl1_bit.genstart = 1;
    while ((I2C_HW->sts1 & I2C_STARTF_FLAG) == 0u && --n != 0u) {
    }
    if (n == 0u) {
        return 0;
    }
    I2C_HW->dt_bit.dt = (uint32_t)(((uint32_t)addr7 << 1) | (uint32_t)(rd ? 1 : 0));
    n = I2C_TO_SHORT;
    while ((I2C_HW->sts1 & I2C_ADDR7F_FLAG) == 0u && --n != 0u) {
    }
    if (n == 0u || (I2C_HW->sts1 & I2C_ACKFAIL_FLAG) != 0u) {
        clear_err();
        return 0;
    }
    (void)I2C_HW->sts1;
    (void)I2C_HW->sts2;
    return 1;
}

static int put_byte(uint8_t b) {
    uint32_t n = I2C_TO_SHORT;

    while ((I2C_HW->sts1 & I2C_TDBE_FLAG) == 0u && --n != 0u) {
    }
    if (n == 0u) {
        return 0;
    }
    I2C_HW->dt_bit.dt = b;
    return 1;
}

static int wait_tdc(void) {
    uint32_t n = I2C_TO_SHORT;
    while ((I2C_HW->sts1 & I2C_TDC_FLAG) == 0u && --n != 0u) {
    }
    return n != 0u;
}

static void stop_cond(void) {
    uint32_t n = I2C_TO_SHORT;

    I2C_HW->ctrl1_bit.genstop = 1;
    while ((I2C_HW->sts1 & I2C_STOPF_FLAG) == 0u && --n != 0u) {
    }
    I2C_HW->sts1 = 0xFFFFu;
    I2C_HW->ctrl1_bit.i2cen = 1;
}

static int get_bytes(uint8_t *dst, int n) {
    int i;

    I2C_HW->ctrl1_bit.acken = 1;
    for (i = 0; i < n; i++) {
        uint32_t t = I2C_TO_SHORT;
        if (i == n - 1) {
            I2C_HW->ctrl1_bit.acken = 0;
        }
        while ((I2C_HW->sts1 & I2C_RDBF_FLAG) == 0u && --t != 0u) {
        }
        if (t == 0u) {
            I2C_HW->ctrl1_bit.acken = 1;
            return 0;
        }
        dst[i] = (uint8_t)I2C_HW->dt_bit.dt;
    }
    I2C_HW->ctrl1_bit.acken = 1;
    return 1;
}

static int xfer(uint8_t addr7, uint16_t reg, int reg16,
                const uint8_t *wr, uint8_t *rd, int n) {
    int i;

    if (!wait_idle()) {
        i2c_recover();
        return 0;
    }
    clear_err();
    if (!start_cond(addr7, 0)) {
        stop_cond();
        return 0;
    }
    if (reg16 && !put_byte((uint8_t)(reg >> 8))) {
        goto fail;
    }
    if (!put_byte((uint8_t)reg)) {
        goto fail;
    }
    if (wr != 0) {
        for (i = 0; i < n; i++) {
            if (!put_byte(wr[i])) {
                goto fail;
            }
        }
        if (!wait_tdc()) {
            goto fail;
        }
    } else {
        if (!wait_tdc()) {
            goto fail;
        }
        if (!start_cond(addr7, 1)) {
            goto fail;
        }
        if (!get_bytes(rd, n)) {
            goto fail;
        }
    }
    stop_cond();
    return 1;

fail:
    stop_cond();
    return 0;
}

int i2c_probe(uint8_t addr7) {
    int ok;

    if (!wait_idle()) {
        i2c_recover();
        return 0;
    }
    ok = start_cond(addr7, 0);
    stop_cond();
    return ok;
}

int i2c_write(uint8_t addr7, uint8_t reg, const uint8_t *data, int n) {
    return xfer(addr7, reg, 0, data, 0, n);
}

int i2c_read(uint8_t addr7, uint8_t reg, uint8_t *data, int n) {
    return xfer(addr7, reg, 0, 0, data, n);
}

int i2c_write16(uint8_t addr7, uint16_t reg16, const uint8_t *data, int n) {
    return xfer(addr7, reg16, 1, data, 0, n);
}

int i2c_read16(uint8_t addr7, uint16_t reg16, uint8_t *data, int n) {
    return xfer(addr7, reg16, 1, 0, data, n);
}
