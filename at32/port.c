#include "ui.h"

#include <stdio.h>

#include "at32f421.h"
#include "at32f421_crm.h"
#include "at32f421_gpio.h"
#include "at32f421_spi.h"
#include "at32f421_dma.h"
#include "at32f421_usart.h"
#include "at32f421_misc.h"
#include "at32f421_flash.h"

#include "board_config.h"
#include "i2c_bus.h"
#include "mpu6050.h"
#include "qmc5883l.h"
#include "ext_flash.h"
#include "adc_bat.h"
#include "beep.h"
#include "led_bl.h"
#include "sport.h"
#include "imu9.h"

/* AT32F421K8T7（LQFP-32，64KB Flash / 16KB SRAM，无 FPU）。
   时钟：HICK 48MHz / 2 = 24MHz 进 PLL，×5 = 120MHz。
   屏幕（1.8 寸 ST7735S，128×160，模块内部 CS 接地，没有 CS 脚）：
     SCK PA5 / SDA PA7 / RES PA4 / DC PA6 / BLK PB0
   串口（临时接到蓝牙座）：USART2 TX PA2 / RX PA3，115200。
   按键：KEY1 PA11 / KEY2 PA12 / KEY3 PA15，按下为低。 */

#define PIN_RES  GPIO_PINS_4
#define PIN_SCK  GPIO_PINS_5
#define PIN_DC   GPIO_PINS_6
#define PIN_SDA  GPIO_PINS_7
#define PIN_BLK  GPIO_PINS_0   /* GPIOB */
#define PIN_LED  GPIO_PINS_3   /* GPIOB，状态灯 */

#define SRC_RES  GPIO_PINS_SOURCE4
#define SRC_SCK  GPIO_PINS_SOURCE5
#define SRC_DC   GPIO_PINS_SOURCE6
#define SRC_SDA  GPIO_PINS_SOURCE7
#define SRC_TX   GPIO_PINS_SOURCE2
#define SRC_RX   GPIO_PINS_SOURCE3

/* SPI1 在 APB2=120MHz。DIV_8 -> 15MHz 先求稳；跑通后可换 DIV_4(30MHz)。 */
#define SPI_CLK_DIV SPI_MCLK_DIV_8

#define STK_CTRL   (*(volatile uint32_t *)0xE000E010u)
#define STK_LOAD   (*(volatile uint32_t *)0xE000E014u)
#define STK_VAL    (*(volatile uint32_t *)0xE000E018u)
#define SCB_ICSR   (*(volatile uint32_t *)0xE000ED04u)

static volatile uint32_t tick_ms;
static uint32_t pace_ms;
static uint32_t meter_t0;
static uint32_t idle_us;
static uint32_t meter_idle0;
static int display_active;
static uint32_t cpu_smooth;
static uint32_t cpu_shown;
static uint32_t cpu_shown_ms;
static int cpu_shown_ok;
static int clock_on;
static hal_clock_t clock_base;
static uint32_t clock_ms;
static unsigned char rx_q[256];
static volatile unsigned char rx_r;
static volatile uint16_t rx_n;
static unsigned char clock_src;

/* 上电自检结果与最近一次采样，供 SWD 直接读 RAM 验证（不依赖蓝牙）。
   bit0 MPU6050 / bit1 QMC5883L / bit3 AT24C256。 */
volatile uint8_t g_sensor_ok;
volatile int16_t g_imu_ax, g_imu_ay, g_imu_az;
volatile int16_t g_imu_gx, g_imu_gy, g_imu_gz;
volatile int16_t g_mag_x, g_mag_y, g_mag_z;
volatile uint16_t g_battery_mv;
volatile uint8_t g_imu_read_ok;
volatile uint8_t g_mag_read_ok;
/* 最近一次调试快照，SWD 直接按字符串读，不依赖蓝牙。 */
char g_debug_line[160];

static uint32_t irq_save(void) {
    uint32_t mask;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask) :: "memory");
    return mask;
}

static void irq_restore(uint32_t mask) {
    __asm volatile("msr primask, %0" :: "r"(mask) : "memory");
}

void SysTick_Handler(void) {
    uint32_t now = tick_ms + 1u;
    tick_ms = now;
    beep_tick(now);
}

void DMA1_Channel3_2_IRQHandler(void) {
    /* 只关中断源，FDT 标志留着给 hal_display_busy() 判断。 */
    dma_interrupt_enable(DMA1_CHANNEL3, DMA_FDT_INT, FALSE);
}

static void systick_on(uint32_t cycles_per_ms) {
    STK_LOAD = cycles_per_ms - 1u;
    STK_VAL = 0;
    STK_CTRL = 7u;
}

static void clock_setup(void) {
    uint32_t n;
    uint32_t pll_ok = 0;

    crm_reset();
    /* 120MHz 需要 3 个等待周期（厂商模板同值）。 */
    flash_psr_set(FLASH_WAIT_CYCLE_3);
    /* 复位后 SCLK = HICK/6 = 8MHz，先按 8MHz 打节拍。 */
    systick_on(8000u);

    /* 1) 先试外部 8MHz × 15 = 120MHz。
       PLL 锁定要几十到几百微秒，必须轮询等待；只看一次会直接掉回 HICK。 */
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
    if(crm_hext_stable_wait() == SUCCESS) {
        crm_pll_config(CRM_PLL_SOURCE_HEXT, CRM_PLL_MULT_15);
        crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
        for(n = 0; n < 400000u; n++) {
            if(crm_flag_get(CRM_PLL_STABLE_FLAG) == SET) {
                break;
            }
        }
        if(crm_flag_get(CRM_PLL_STABLE_FLAG) == SET) {
            pll_ok = 2; /* 外部晶振 */
        }
    }

    /* 2) 外部晶振没起来或 PLL 没锁：退回内部 HICK 48/2 × 5 = 120MHz。 */
    if(pll_ok == 0) {
        crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, FALSE);
        crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);
        if(crm_flag_get(CRM_HICK_STABLE_FLAG) == SET) {
            crm_pll_config(CRM_PLL_SOURCE_HICK, CRM_PLL_MULT_5);
            crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
            for(n = 0; n < 400000u; n++) {
                if(crm_flag_get(CRM_PLL_STABLE_FLAG) == SET) {
                    break;
                }
            }
            if(crm_flag_get(CRM_PLL_STABLE_FLAG) == SET) {
                pll_ok = 1; /* 内部 HICK */
            }
        }
    }

    if(pll_ok != 0) {
        crm_ahb_div_set(CRM_AHB_DIV_1);
        crm_apb2_div_set(CRM_APB2_DIV_1);
        crm_apb1_div_set(CRM_APB1_DIV_1);
        crm_sysclk_switch(CRM_SCLK_PLL);
        for(n = 0; n < 400000u; n++) {
            if(crm_sysclk_switch_status_get() == CRM_SCLK_PLL) {
                break;
            }
        }
    }

    /* 3) 按“真实生效”的时钟设 SysTick。任何回退都不能让节拍跑偏。 */
    if(crm_sysclk_switch_status_get() == CRM_SCLK_PLL) {
        clock_src = (unsigned char)pll_ok; /* 2=X 1=I */
        systick_on(120000u);
    } else if(crm_sysclk_switch_status_get() == CRM_SCLK_HEXT) {
        clock_src = 3;
        systick_on(8000u);
    } else {
        clock_src = (CRM->misc1_bit.hickdiv == CRM_HICK48_NODIV) ? 4u : 0u; /* 48 或 8 */
        systick_on(clock_src == 4u ? 48000u : 8000u);
    }
}

static void gpio_setup(void) {
    gpio_init_type gpio_init_struct;

    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

    /* PA4 RES / PA6 DC：推挽输出 */
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
    gpio_init_struct.gpio_pins = PIN_RES | PIN_DC;
    gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
    gpio_init(GPIOA, &gpio_init_struct);

    /* PB0 背光 / PB3 状态灯 */
    gpio_init_struct.gpio_pins = PIN_BLK | PIN_LED;
    gpio_init(GPIOB, &gpio_init_struct);

    /* PA5 SCK / PA7 SDA 复用 SPI1 */
    gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
    gpio_init_struct.gpio_pins = PIN_SCK | PIN_SDA;
    gpio_init(GPIOA, &gpio_init_struct);
    gpio_pin_mux_config(GPIOA, SRC_SCK, GPIO_MUX_0);
    gpio_pin_mux_config(GPIOA, SRC_SDA, GPIO_MUX_0);

    /* PA2 TX / PA3 RX 复用 USART2 */
    gpio_init_struct.gpio_pins = GPIO_PINS_2 | GPIO_PINS_3;
    gpio_init(GPIOA, &gpio_init_struct);
    gpio_pin_mux_config(GPIOA, SRC_TX, GPIO_MUX_1);
    gpio_pin_mux_config(GPIOA, SRC_RX, GPIO_MUX_1);

    /* PA11/PA12/PA15 按键，上拉输入 */
    gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
    gpio_init_struct.gpio_pins = GPIO_PINS_11 | GPIO_PINS_12 | GPIO_PINS_15;
    gpio_init_struct.gpio_pull = GPIO_PULL_UP;
    gpio_init(GPIOA, &gpio_init_struct);

    gpio_bits_set(GPIOA, PIN_RES | PIN_DC);
    gpio_bits_set(GPIOB, PIN_BLK | PIN_LED); /* 背光先点亮 */
}

static void spi_setup(void) {
    spi_init_type spi_init_struct;

    crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

    spi_default_para_init(&spi_init_struct);
    spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
    spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
    spi_init_struct.mclk_freq_division = SPI_CLK_DIV;
    spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
    spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
    spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_LOW;
    spi_init_struct.clock_phase = SPI_CLOCK_PHASE_1EDGE;
    spi_init_struct.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
    spi_init(SPI1, &spi_init_struct);
    spi_enable(SPI1, TRUE);

    /* 像素走 DMA1 通道 3（SPI1_TX，固定映射） */
    dma_reset(DMA1_CHANNEL3);
    dma_flag_clear(DMA1_FDT3_FLAG);
    nvic_irq_enable(DMA1_Channel3_2_IRQn, 1, 0);
}

static void usart_setup(void) {
    uint32_t apb_hz = hal_sysclk_mhz() * 1000000u;
    crm_periph_clock_enable(CRM_USART2_PERIPH_CLOCK, TRUE);
    USART2->baudr_bit.div = (apb_hz + BLE_UART_BAUD / 2u) / BLE_UART_BAUD;
    USART2->ctrl2_bit.stopbn = USART_STOP_1_BIT;
    USART2->ctrl1 = (1u << 2) | (1u << 3) | (1u << 5) | (1u << 13); /* ren|ten|rdbfien|uen */
    nvic_irq_enable(USART2_IRQn, 2, 0);
}

void USART2_IRQHandler(void) {
    if(USART2->sts_bit.rdbf) {
        unsigned char c = (unsigned char)USART2->dt;
        if(rx_n < sizeof rx_q) {
            rx_q[(rx_r + rx_n) % sizeof rx_q] = c;
            rx_n++;
        }
    } else {
        (void)USART2->dt;
    }
}

static void uart_putc(unsigned char c) {
    uint32_t n = 100000u;
    while(!USART2->sts_bit.tdbe && n--) {
    }
    USART2->dt = c;
}

#if !defined(AT32_SLIM_ROM)
static void uart_puts(const char *s) {
    while(*s) {
        uart_putc((unsigned char)*s++);
    }
}
#endif

static void delay_ms(unsigned int ms) {
    uint32_t t = tick_ms;
    while((tick_ms - t) < ms) {
        hal_idle();
    }
}

static void spi_wait(void) {
    while(SPI1->sts_bit.tdbe == 0) {
    }
    while(SPI1->sts_bit.bf) {
    }
    (void)SPI1->dt;
    (void)SPI1->sts;
}

static void spi_bits(int wide) {
    spi_wait();
    spi_enable(SPI1, FALSE);
    spi_frame_bit_num_set(SPI1, wide ? SPI_FRAME_16BIT : SPI_FRAME_8BIT);
    spi_enable(SPI1, TRUE);
}

static void spi_byte(unsigned char data) {
    while(SPI1->sts_bit.tdbe == 0) {
    }
    SPI1->dt = data;
}

static void spi_dma(const uint16_t *px, int n) {
    dma_init_type dma_init_struct;

    dma_reset(DMA1_CHANNEL3);
    dma_flag_clear(DMA1_FDT3_FLAG);
    dma_default_para_init(&dma_init_struct);
    dma_init_struct.peripheral_base_addr = (uint32_t)&SPI1->dt;
    dma_init_struct.memory_base_addr = (uint32_t)px;
    dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
    dma_init_struct.buffer_size = (uint16_t)n;
    dma_init_struct.peripheral_inc_enable = FALSE;
    dma_init_struct.memory_inc_enable = TRUE;
    dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
    dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_HALFWORD;
    dma_init_struct.loop_mode_enable = FALSE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_init(DMA1_CHANNEL3, &dma_init_struct);
    dma_interrupt_enable(DMA1_CHANNEL3, DMA_FDT_INT, TRUE);
    display_active = 1;
    __asm volatile("dmb" ::: "memory");
    spi_i2s_dma_transmitter_enable(SPI1, TRUE);
    dma_channel_enable(DMA1_CHANNEL3, TRUE);
}

static void lcd_cmd(unsigned char cmd) {
    gpio_bits_reset(GPIOA, PIN_DC);
    spi_byte(cmd);
    spi_wait();
}

static void lcd_data(unsigned char data) {
    gpio_bits_set(GPIOA, PIN_DC);
    spi_byte(data);
    spi_wait();
}

static void lcd_window(int x, int y, int w, int h) {
    /* 这块 CTC180 面板：起始地址不加偏移，终点列 +2、行 +1。
       改成对称偏移会把起始点也移走，每行少 2 像素，带会变成梳齿。 */
    unsigned int x0 = (unsigned int)x;
    unsigned int y0 = (unsigned int)y;
    unsigned int x1 = x0 + (unsigned int)w - 1u;
    unsigned int y1 = y0 + (unsigned int)h - 1u;

    lcd_cmd(0x2A);
    lcd_data(0);
    lcd_data((unsigned char)x0);
    lcd_data(0);
    lcd_data((unsigned char)(x1 + 2u));
    lcd_cmd(0x2B);
    lcd_data(0);
    lcd_data((unsigned char)y0);
    lcd_data(0);
    lcd_data((unsigned char)(y1 + 1u));
    lcd_cmd(0x2C);
}

static void lcd_init(void) {
    /* 资料目录 ST7735S_CTC180_INITIAL.txt，对应这块 1.8 寸。 */
    static const unsigned char seq[] = {
        0x11, 0,
        0xB1, 3, 0x05, 0x3C, 0x3C,
        0xB2, 3, 0x05, 0x3C, 0x3C,
        0xB3, 6, 0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C,
        0xB4, 1, 0x03,
        0xC0, 3, 0x28, 0x08, 0x04,
        0xC1, 1, 0xC0,
        0xC2, 2, 0x0D, 0x00,
        0xC3, 2, 0x8D, 0x2A,
        0xC4, 2, 0x8D, 0xEE,
        0xC5, 1, 0x1A,
        0x36, 1, 0xC0,
        0xE0, 16, 0x04, 0x22, 0x07, 0x0A, 0x2E, 0x30, 0x25, 0x2A, 0x28, 0x26, 0x2E, 0x3A, 0x00, 0x01, 0x03, 0x13,
        0xE1, 16, 0x04, 0x16, 0x06, 0x0D, 0x2D, 0x26, 0x23, 0x27, 0x27, 0x25, 0x2D, 0x3B, 0x00, 0x01, 0x04, 0x13,
        0x3A, 1, 0x05,
        0x29, 0
    };
    const unsigned char *p = seq;
    const unsigned char *end = seq + sizeof seq;

    gpio_bits_set(GPIOA, PIN_RES);
    delay_ms(1);
    gpio_bits_reset(GPIOA, PIN_RES);
    delay_ms(1);
    gpio_bits_set(GPIOA, PIN_RES);
    delay_ms(120);
    while(p + 2 <= end) {
        unsigned char cmd = *p++;
        unsigned char n = *p++;
        unsigned char i;
        lcd_cmd(cmd);
        for(i = 0; i < n; i++) {
            lcd_data(*p++);
        }
        if(cmd == 0x11) {
            delay_ms(120);
        }
    }
}

static int month_days(int year, int month) {
    static const unsigned char days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = days[month - 1];
    if(month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        n = 29;
    }
    return n;
}

static void add_seconds(hal_clock_t *t, uint32_t sec) {
    uint32_t s = t->second + sec;
    t->second = (unsigned char)(s % 60u);
    s = t->minute + s / 60u;
    t->minute = (unsigned char)(s % 60u);
    s = t->hour + s / 60u;
    t->hour = (unsigned char)(s % 24u);
    s = s / 24u;
    while(s > 0) {
        int md = month_days(t->year, t->month);
        uint32_t left = (uint32_t)(md - t->day);
        if(s <= left) {
            t->day = (unsigned char)(t->day + s);
            break;
        }
        s -= left + 1u;
        t->day = 1;
        t->month++;
        if(t->month > 12) {
            t->month = 1;
            t->year++;
        }
    }
}

uint32_t hal_tick_ms(void) {
    return tick_ms;
}

uint32_t hal_tick_us(void) {
    uint32_t mask = irq_save();
    uint32_t ms = tick_ms;
    uint32_t load = STK_LOAD;
    uint32_t val = STK_VAL;
    uint32_t per_us = (load + 1u) / 1000u;

    if(SCB_ICSR & (1u << 26)) {
        ms++;
        val = STK_VAL;
    }
    irq_restore(mask);
    if(per_us == 0) {
        per_us = 1;
    }
    return ms * 1000u + (val ? (load - val) / per_us : 0u);
}

void hal_idle(void) {
    uint32_t mask = irq_save();
    uint32_t before;

    if(rx_n || (display_active && dma_flag_get(DMA1_FDT3_FLAG) != RESET)) {
        irq_restore(mask);
        return;
    }
    before = hal_tick_us();
    __asm volatile("dsb\n\twfi" ::: "memory");
    idle_us += hal_tick_us() - before;
    irq_restore(mask);
}

void hal_clock_set(const hal_clock_t *in) {
    if(!in) {
        return;
    }
    clock_base = *in;
    clock_ms = tick_ms;
    clock_on = 1;
}

void hal_clock_get(hal_clock_t *out) {
    if(!out) {
        return;
    }
    if(!clock_on) {
        out->year = 2026;
        out->month = 1;
        out->day = 1;
        out->hour = 0;
        out->minute = 0;
        out->second = 0;
        clock_base = *out;
        clock_ms = tick_ms;
        clock_on = 1;
    }
    *out = clock_base;
    add_seconds(out, (tick_ms - clock_ms) / 1000u);
}

uint8_t hal_key_level(void) {
    uint32_t idt = GPIOA->idt;
    uint8_t keys = 0;
    if((idt & GPIO_PINS_11) == 0) {
        keys |= HAL_KEY1;
    }
    if((idt & GPIO_PINS_12) == 0) {
        keys |= HAL_KEY2;
    }
    if((idt & GPIO_PINS_15) == 0) {
        keys |= HAL_KEY3;
    }
    return keys;
}

void hal_display_flush(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels) {
    if(!pixels || x < 0 || y < 0 || w <= 0 || h <= 0 ||
       x + w > HAL_LCD_W || y + h > HAL_LCD_H) {
        return;
    }
    if(hal_display_busy()) {
        return;
    }
    lcd_window(x, y, w, h);
    spi_bits(1);
    gpio_bits_set(GPIOA, PIN_DC);
    spi_dma(pixels, w * h);
}

int hal_display_busy(void) {
    if(!display_active) {
        return 0;
    }
    if(dma_flag_get(DMA1_FDT3_FLAG) == RESET) {
        return 1;
    }
    /* FDT 只说明最后一个半字进了 DR，还没移出。两个都要等。 */
    if(SPI1->sts_bit.tdbe == 0 || SPI1->sts_bit.bf) {
        return 1;
    }
    dma_channel_enable(DMA1_CHANNEL3, FALSE);
    spi_i2s_dma_transmitter_enable(SPI1, FALSE);
    spi_bits(0);
    dma_flag_clear(DMA1_FDT3_FLAG);
    __asm volatile("dmb" ::: "memory");
    display_active = 0;
    return 0;
}

void hal_meter_begin(void) {
    meter_t0 = hal_tick_us();
    meter_idle0 = idle_us;
}

void hal_meter_end(void) {
    uint32_t dt = hal_tick_us() - meter_t0;
    uint32_t slept = idle_us - meter_idle0;
    uint32_t us = dt >= slept ? dt - slept : 0;
    uint32_t tenths;
    uint32_t now;

    if(us >= 16667u) {
        tenths = 1000u;
    } else {
        tenths = us * 1000u / 16667u;
    }
    if(!cpu_shown_ok) {
        cpu_smooth = tenths;
    } else {
        cpu_smooth = (cpu_smooth * 3u + tenths) / 4u;
    }
    now = tick_ms;
    if(!cpu_shown_ok || now - cpu_shown_ms >= 500u) {
        cpu_shown = cpu_smooth;
        cpu_shown_ms = now;
        cpu_shown_ok = 1;
    }
}

uint16_t hal_cpu_tenths(void) {
    return (uint16_t)cpu_shown;
}

void hal_frame_pace(void) {
    for(;;) {
        ui_service();
        if((tick_ms - pace_ms) >= 16u) {
            break;
        }
        hal_idle();
    }
    pace_ms = tick_ms;
}

int hal_link_read(uint8_t *out) {
    uint32_t mask;
    if(!out) {
        return 0;
    }
    mask = irq_save();
    if(rx_n == 0) {
        irq_restore(mask);
        return 0;
    }
    *out = rx_q[rx_r];
    rx_r = (unsigned char)((rx_r + 1u) % sizeof rx_q);
    rx_n--;
    irq_restore(mask);
    return 1;
}

void hal_link_write(const uint8_t *data, int len) {
    int i;
    if(!data || len <= 0) {
        return;
    }
    for(i = 0; i < len; i++) {
        uart_putc(data[i]);
    }
}

void board_init(void) {
    clock_setup();
    gpio_setup();
    spi_setup();
    usart_setup();
    lcd_init();

    i2c_bus_init();
    {
        int mpu_ok = mpu6050_init();
        int mag_ok = qmc5883l_init();
        int ext_ok = ext_flash_init();
        g_sensor_ok = (uint8_t)((mpu_ok ? 1u : 0u) |
                                (mag_ok ? 2u : 0u) |
                                (ext_ok ? 8u : 0u));
    }
    adc_init();
    beep_init();
    led_init();
    backlight_init();
    backlight_set(255);
    imu9_init();
    /* 上电把 I2C 扫描 + QMC 寄存器存一份到 RAM，SWD 与蓝牙 DBG 共用。 */
    g_battery_mv = battery_mv();
    (void)hal_debug_snapshot(g_debug_line, (int)sizeof g_debug_line);
#if !defined(AT32_SLIM_ROM)
    uart_puts(g_debug_line);
    uart_puts("\r\n");
#endif
}

/* ---- 传感器/执行器 HAL 包装 ---- */
/* 按实物模块焊装方位（D:\下载\IMG_20261115_130804.jpg）统一映射到手表右手系：
   +X = 屏幕右，+Y = 屏幕上（12点方向），+Z = 朝屏幕外（平放指天）。
   - GY-521 MPU6050(0x68)：Pin1 在右上 (+X_mpu=-Y_b, +Y_mpu=+X_b, +Z_mpu=+Z_b)
   - GY-273 QMC5883P(0x2C)：Pin1 在右上，按手册 Fig.2 旋转 90°CW
     (+X_qmc=+Y_b, +Y_qmc=-X_b, +Z_qmc=+Z_b)；若为 QMC5883L(0x0D) 按丝印 (-mx,-my,+mz)。 */
static void board_align_imu(hal_imu_t *s) {
    int16_t rx = s->ax, ry = s->ay;
    int16_t gx = s->gx, gy = s->gy;
    s->ax = ry;
    s->ay = (int16_t)-rx;
    s->gx = gy;
    s->gy = (int16_t)-gx;
}

static void board_align_mag(hal_mag_t *m, int chip) {
    int16_t rx = m->x, ry = m->y;
    if(chip == 2) {         /* QMC5883P @ 0x2C */
        m->x = (int16_t)-ry;
        m->y = rx;
    } else if(chip == 1) {  /* QMC5883L @ 0x0D */
        m->x = (int16_t)-rx;
        m->y = (int16_t)-ry;
    }
}

int hal_imu_read(hal_imu_t *out) {
    if(!out || !mpu6050_read(out)) {
        return 0;
    }
    board_align_imu(out);
    return 1;
}

int hal_mag_read(hal_mag_t *out) {
    if(!out || !qmc5883l_read(out)) {
        return 0;
    }
    board_align_mag(out, qmc5883l_chip());
    return 1;
}

int hal_baro_read(hal_baro_t *out) {
    (void)out;
    return 0;
}

uint16_t hal_battery_mv(void) {
    return battery_mv();
}

uint8_t hal_battery_pct(void) {
    return battery_pct();
}

int hal_i2c_scan(uint8_t *out, int max) {
    int addr;
    int n = 0;

    if(!out || max <= 0) {
        return 0;
    }
    for(addr = 0x08; addr < 0x78 && n < max; addr++) {
        if(i2c_probe((uint8_t)addr)) {
            out[n++] = (uint8_t)addr;
        }
    }
    return n;
}

/* 蓝牙 0x13 调试快照。复用 hal_i2c_scan 节省体积，支持热插拔即时体现。 */
int hal_debug_snapshot(char *out, int cap) {
    uint8_t addrs[6];
    int na, i, n = 0;
    imu9_state_t s9;
#if !defined(AT32_SLIM_ROM)
    char qbuf[64];
#endif

    if(!out || cap < 32) {
        return 0;
    }
    na = hal_i2c_scan(addrs, (int)sizeof addrs);
    imu9_get(&s9);
#if defined(AT32_SLIM_ROM)
    n = snprintf(out, (size_t)cap, "I2C%d M%d Q%d 9AX=%u R%dP%dH%u B%u",
                 na, mpu6050_present(), qmc5883l_chip(),
                 (unsigned)s9.fused_axes,
                 (int)(s9.roll_d10 / 10), (int)(s9.pitch_d10 / 10),
                 (unsigned)s9.heading_deg, (unsigned)g_battery_mv);
    if (n < 0) n = 0;
    if (n >= cap) n = cap - 1;
    for (i = 0; i < n && i < (int)sizeof g_debug_line - 1; i++) {
        g_debug_line[i] = out[i];
    }
    g_debug_line[i] = 0;
    return n;
#else
#define DBG_ADD(...) do {                                              \
        int w = snprintf(out + n, (size_t)(cap - n), __VA_ARGS__);      \
        if(w > 0) {                                                    \
            n += w;                                                    \
            if(n > cap - 1) n = cap - 1;                               \
        }                                                              \
    } while(0)

    DBG_ADD("I2C=");
    if(na == 0) {
        DBG_ADD("NONE");
    } else {
        for(i = 0; i < na; i++) {
            DBG_ADD("%s%02X", i ? "," : "", (unsigned)addrs[i]);
        }
    }
    qmc5883l_debug(qbuf, (int)sizeof qbuf);
    DBG_ADD(";MPU=%d;%s;A=%d,%d,%d;M=%d,%d,%d;9AX=%u;RPY=%d,%d,%u;BAT=%u",
            mpu6050_present(), qbuf,
            g_imu_ax, g_imu_ay, g_imu_az,
            g_mag_x, g_mag_y, g_mag_z,
            (unsigned)s9.fused_axes,
            (int)(s9.roll_d10 / 10), (int)(s9.pitch_d10 / 10),
            (unsigned)s9.heading_deg,
            (unsigned)g_battery_mv);
#undef DBG_ADD
    for(i = 0; i < n && i < (int)sizeof g_debug_line - 1; i++) {
        g_debug_line[i] = out[i];
    }
    g_debug_line[i < (int)sizeof g_debug_line ? i : (int)sizeof g_debug_line - 1] = 0;
    return n;
#endif
}

const char *hal_mag_name(void) {
    int c = qmc5883l_chip();
    if(c == 1) return "QMC5883L";
    if(c == 2) return "QMC5883P";
    return "--";
}

int hal_ble_connected(void) {
    return ble_state_read();
}

void hal_beep(uint16_t freq_hz, uint16_t ms) {
    beep_set(freq_hz, ms);
}

void hal_led_set(int on) {
    led_set(on);
}

void hal_led_mode(int mode) {
    led_mode_set(mode);
}

void hal_backlight_set(uint8_t level) {
    backlight_set(level);
}

void hal_ram_get(uint32_t *used, uint32_t *total) {
    extern uint32_t _sdata;
    extern uint32_t _ebss;
    extern uint32_t _estack;
    uint32_t base = (uint32_t)&_sdata;
    uint32_t end = (uint32_t)&_ebss;
    uint32_t top = (uint32_t)&_estack;
    uint32_t static_used = end - base;
    uint32_t ui_used = 0;
    uint32_t ui_total = 0;
    uint32_t ui_free;
    uint32_t msp;
    uint32_t stack_used;
    uint32_t all = top - base;

    ui_mem_get(&ui_used, &ui_total);
    ui_free = ui_total > ui_used ? (ui_total - ui_used) : 0u;
    if(static_used > ui_free) {
        static_used -= ui_free;
    }
    __asm volatile("mrs %0, msp" : "=r"(msp));
    if(msp > top) {
        msp = top;
    }
    stack_used = top - msp;
    if(used) {
        uint32_t v = static_used + stack_used;
        *used = v > all ? all : v;
    }
    if(total) {
        *total = all;
    }
}

int hal_ext_flash_present(void) {
    return ext_flash_present();
}

uint8_t hal_ext_flash_addr(void) {
    return ext_flash_addr();
}

int hal_ext_flash_read(uint32_t offset, uint8_t *dst, uint32_t len) {
    return ext_flash_read(offset, dst, len);
}

int hal_ext_flash_write(uint32_t offset, const uint8_t *src, uint32_t len) {
    return ext_flash_write(offset, src, len);
}

int hal_ext_flash_erase(uint32_t offset, uint32_t len) {
    return ext_flash_erase(offset, len);
}

const char *hal_fw_version(void) {
    return "1.0.0";
}

const char *hal_fw_build(void) {
    return __DATE__ " " __TIME__;
}

const char *hal_mcu_name(void) {
    return "AT32F421K8T7";
}

uint32_t hal_sysclk_mhz(void) {
    if(clock_src == 1 || clock_src == 2) {
        return 120;
    }
    if(clock_src == 4) {
        return 48;
    }
    return 8;
}

const char *hal_clock_src(void) {
    if(clock_src == 2) {
        return "120MHz HEXT";
    }
    if(clock_src == 1) {
        return "120MHz HICK";
    }
    if(clock_src == 4) {
        return "48MHz HICK";
    }
    return "8MHz HICK";
}

/* 帧空隙里的协作服务：串口解析 + 周期采样计步 + 传感器掉线热恢复（20ms 节拍）。
   由 main.c 通过 ui_set_service 挂上。采样节流到 ~50Hz。 */
void port_service(void) {
    static uint32_t sensor_ms;
    static uint8_t imu_miss, mag_miss;
    uint32_t now = tick_ms;
    hal_imu_t imu;
    hal_mag_t mag;

    link_poll();
    if((now - sensor_ms) >= 20u) {
        int imu_ok = hal_imu_read(&imu);
        int mag_ok = hal_mag_read(&mag);
        sensor_ms = now;
        if(imu_ok) {
            imu_miss = 0u;
            sport_feed(&imu, now);
            g_imu_read_ok = 1;
            g_imu_ax = imu.ax;
            g_imu_ay = imu.ay;
            g_imu_az = imu.az;
            g_imu_gx = imu.gx;
            g_imu_gy = imu.gy;
            g_imu_gz = imu.gz;
        } else if(++imu_miss >= 50u) {
            imu_miss = 0u;
            i2c_recover();
            if(mpu6050_init()) {
                g_sensor_ok |= 1u;
            } else {
                g_sensor_ok &= (uint8_t)~1u;
            }
        }
        if(mag_ok) {
            mag_miss = 0u;
            g_mag_read_ok = 1;
            g_mag_x = mag.x;
            g_mag_y = mag.y;
            g_mag_z = mag.z;
        } else if(++mag_miss >= 50u) {
            mag_miss = 0u;
            if(qmc5883l_init()) {
                g_sensor_ok |= 2u;
            } else {
                g_sensor_ok &= (uint8_t)~2u;
            }
        }
        if(imu_ok || mag_ok) {
            imu9_feed(imu_ok ? &imu : 0, mag_ok ? &mag : 0, now);
        }
        g_battery_mv = battery_mv();
    }
    led_tick(now);
}

extern const uint8_t __dial_swap_start[];
extern const uint8_t __dial_swap_end[];

const uint8_t *hal_int_img_ptr(void) {
    return __dial_swap_start;
}

uint32_t hal_int_img_cap(void) {
    uintptr_t s = (uintptr_t)__dial_swap_start;
    uintptr_t e = (uintptr_t)__dial_swap_end;
    return (e > s) ? (uint32_t)(e - s) : 0u;
}

static int flash_wait_idle(void) {
    uint32_t to = 2000000u;
    while (FLASH->sts_bit.obf && to--) {
    }
    return to > 0u && !FLASH->sts_bit.prgmerr && !FLASH->sts_bit.epperr;
}

int hal_int_img_write(uint32_t offset, const uint8_t *src, uint32_t len) {
    uint32_t cap = hal_int_img_cap();
    uintptr_t base = (uintptr_t)__dial_swap_start;
    uint32_t i;
    int ok = 1;

    if (!src || len == 0u || (offset & 1u) != 0u || offset > cap || len > cap - offset) {
        return 0;
    }
    FLASH->unlock = FLASH_UNLOCK_KEY1;
    FLASH->unlock = FLASH_UNLOCK_KEY2;
    FLASH->sts = FLASH_ODF_FLAG | FLASH_PRGMERR_FLAG | FLASH_EPPERR_FLAG;
    for (i = 0u; i < len; ) {
        uint32_t addr = (uint32_t)(base + offset + i);
        uint16_t lo, hi;
        if ((addr & 0x3FFu) == 0u) {
            FLASH->ctrl_bit.secers = 1;
            FLASH->addr = addr;
            FLASH->ctrl_bit.erstr = 1;
            ok = flash_wait_idle();
            FLASH->ctrl_bit.secers = 0;
            FLASH->sts = FLASH_ODF_FLAG;
            if (!ok) {
                break;
            }
        }
        lo = src[i];
        hi = (i + 1u < len) ? src[i + 1u] : 0xFFu;
        FLASH->ctrl_bit.fprgm = 1;
        *(__IO uint16_t *)addr = (uint16_t)(lo | (hi << 8));
        ok = flash_wait_idle();
        FLASH->ctrl_bit.fprgm = 0;
        FLASH->sts = FLASH_ODF_FLAG;
        if (!ok) {
            break;
        }
        i += (i + 1u < len) ? 2u : 1u;
    }
    FLASH->ctrl_bit.oplk = 1;
    return ok;
}
