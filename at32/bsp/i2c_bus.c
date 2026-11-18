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
    /* PA9=SCL, PA10=SDA: MUX4, open-drain, pull-up, stronger drive */
    GPIOA->muxh = (GPIOA->muxh & ~0x00000FF0u) | 0x00000440u;
    GPIOA->cfgr = (GPIOA->cfgr & ~(0xFu << 18)) | (0xAu << 18);
    GPIOA->omode |= (1u << 9) | (1u << 10);
    GPIOA->pull = (GPIOA->pull & ~(0xFu << 18)) | (0x5u << 18);
    GPIOA->odrvr &= ~(0xFu << 18);
}

static void hw_start(void) {
    /* APB1 = 120MHz；400kHz 快速模式 2:1 -> speed = 120M/(400k*3) = 100 */
    I2C_HW->ctrl1 = 0x8000u;         /* reset = 1 */
    I2C_HW->ctrl1 = 0u;              /* reset = 0 */
    I2C_HW->ctrl2 = 120u;
    I2C_HW->clkctrl = 0x8000u | 100u;/* speedmode = 1, dutymode = 0, speed = 100 */
    I2C_HW->tmrise = 37u;
    I2C_HW->ctrl1 = 0x0401u;         /* acken = 1, stretch = 0, i2cen = 1 */
}

/* 从机异常（如 MPU6050 被复位打断）时可能把 SDA 一直拉低，只复位 I2C
   外设解锁不了总线。临时把 PA9/PA10 切成 GPIO 开漏，打 9 个 SCL 时钟
   再补一个标准 STOP，然后再切回 I2C 复用。 */
static void bus_delay(void) {
    volatile int d = 0;
    while (d < 200) {
        d++;
    }
}

static void bus_clear(void) {
    int i;

    I2C_HW->ctrl1 = 0u;
    GPIOA->cfgr = (GPIOA->cfgr & ~(0xFu << 18)) | (0x5u << 18);
    GPIOA->omode |= (1u << 9) | (1u << 10);
    GPIOA->odt |= (1u << 9) | (1u << 10);
    bus_delay();
    for (i = 0; i < 9; i++) {
        GPIOA->odt &= ~(1u << 9);
        bus_delay();
        GPIOA->odt |= (1u << 9);
        bus_delay();
    }
    /* 标准 STOP：先在 SCL 低电平时拉低 SDA，再释放 SCL 高，最后释放 SDA 高 */
    GPIOA->odt &= ~(1u << 9);
    bus_delay();
    GPIOA->odt &= ~(1u << 10);
    bus_delay();
    GPIOA->odt |= (1u << 9);
    bus_delay();
    GPIOA->odt |= (1u << 10);
    bus_delay();
    pins_i2c();
}

void i2c_bus_init(void) {
    crm_periph_clock_enable(I2C_CRM_CLK, TRUE);
    crm_periph_clock_enable(CRM_I2C1_PERIPH_CLOCK, TRUE);
    bus_clear();
    hw_start();
}

void i2c_recover(void) {
    /* 软件复位 I2C1 后重配；先解锁可能被从机拉死的总线。 */
    bus_clear();
    hw_start();
}

static void clear_err(void) {
    /* sts1 高 8 位错误标志均为 rc_w0（写 0 清除），低 8 位只读不受影响。 */
    I2C_HW->sts1 = 0u;
}

static int wait_idle(void) {
    uint32_t n = I2C_TO_LONG;
    while (((I2C_HW->sts2 & 0x2u) != 0u || (I2C_HW->ctrl1 & 0x300u) != 0u) && --n != 0u) {
    }
    return n != 0u;
}

static int wait_set(uint32_t flag) {
    uint32_t n = I2C_TO_SHORT;
    uint32_t s;

    while (((s = I2C_HW->sts1) & flag) == 0u) {
        if ((s & (I2C_BUSERR_FLAG | I2C_ARLOST_FLAG |
                  I2C_ACKFAIL_FLAG | I2C_OUF_FLAG)) != 0u || --n == 0u) {
            return 0;
        }
    }
    return 1;
}

static void clear_addr(void) {
    (void)I2C_HW->sts1;
    (void)I2C_HW->sts2;
}

static int start_cond(uint8_t addr7, int rd) {
    I2C_HW->ctrl1_bit.genstart = 1;
    if (!wait_set(I2C_STARTF_FLAG)) {
        return 0;
    }
    /* 直接写整个 DT，不要用 dt_bit.dt 位域赋值：位域会编译成“读-改-写”，
       发送缓冲为空时读 DT 会触发 underflow，随后写入的值被硬件丢弃。 */
    I2C_HW->dt = (uint32_t)(((uint32_t)addr7 << 1) | (uint32_t)(rd ? 1 : 0));
    if (!wait_set(I2C_ADDR7F_FLAG)) {
        return 0;
    }
    if (!rd) {
        clear_addr();
    }
    return 1;
}

static int put_byte(uint8_t b) {
    if (!wait_set(I2C_TDBE_FLAG)) {
        return 0;
    }
    I2C_HW->dt = b;
    return 1;
}

static int wait_tdc(void) {
    return wait_set(I2C_TDC_FLAG);
}

static void stop_cond(void) {
    I2C_HW->ctrl1_bit.genstop = 1;
}

static void stop_end(void) {
    if ((I2C_HW->sts2 & 0x3u) != 0u) {
        I2C_HW->ctrl1_bit.genstop = 1;
    }
    if ((I2C_HW->ctrl1 & 0x100u) != 0u) {
        I2C_HW->ctrl1_bit.genstart = 0;
    }
    clear_err();
    if (!wait_idle()) {
        i2c_recover();
    }
}

static int get_bytes(uint8_t *dst, int n) {
    int left = n;

    if (n == 1) {
        I2C_HW->ctrl1_bit.acken = 0;
        clear_addr();
        stop_cond();
    } else if (n == 2) {
        I2C_HW->ctrl1_bit.mackctrl = 1;
        I2C_HW->ctrl1_bit.acken = 0;
        clear_addr();
    } else {
        I2C_HW->ctrl1_bit.acken = 1;
        clear_addr();
    }

    while (left > 3) {
        if (!wait_set(I2C_RDBF_FLAG)) {
            return 0;
        }
        *dst++ = (uint8_t)I2C_HW->dt;
        left--;
    }
    if (left == 3) {
        if (!wait_set(I2C_TDC_FLAG)) {
            return 0;
        }
        I2C_HW->ctrl1_bit.acken = 0;
        *dst++ = (uint8_t)I2C_HW->dt;
        if (!wait_set(I2C_TDC_FLAG)) {
            return 0;
        }
        stop_cond();
        dst[0] = (uint8_t)I2C_HW->dt;
        dst[1] = (uint8_t)I2C_HW->dt;
    } else if (left == 2) {
        if (!wait_set(I2C_TDC_FLAG)) {
            return 0;
        }
        stop_cond();
        dst[0] = (uint8_t)I2C_HW->dt;
        dst[1] = (uint8_t)I2C_HW->dt;
    } else {
        if (!wait_set(I2C_RDBF_FLAG)) {
            return 0;
        }
        *dst = (uint8_t)I2C_HW->dt;
    }
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
    I2C_HW->ctrl1_bit.mackctrl = 0;
    I2C_HW->ctrl1_bit.acken = 1;
    if (!start_cond(addr7, 0)) {
        goto fail;
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
        stop_end();
        return 1;
    }
    if (n <= 0 || !wait_tdc() || !start_cond(addr7, 1) || !get_bytes(rd, n)) {
        goto fail;
    }
    if (!wait_idle()) {
        i2c_recover();
    }
    return 1;

fail:
    stop_end();
    return 0;
}

int i2c_probe(uint8_t addr7) {
    int ok;

    if (!wait_idle()) {
        i2c_recover();
        return 0;
    }
    clear_err();
    I2C_HW->ctrl1_bit.mackctrl = 0;
    ok = start_cond(addr7, 0);
    stop_end();
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
