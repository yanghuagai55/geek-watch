#include "hal.h"

/* STM32F103C8。SPI1：SCL PA5，SDA PA7，CS PA4。DC PB6，RES PB7，背光 PB1，PA14 也拉高。
   串口 USART2：PA2 发送，PA3 接收，115200。 */

#define REG(a) (*(volatile unsigned int *)(a))
#define RCC_CR       REG(0x40021000)
#define RCC_CFGR     REG(0x40021004)
#define RCC_APB2ENR  REG(0x40021018)
#define RCC_APB1ENR  REG(0x4002101C)
#define FLASH_ACR    REG(0x40022000)
#define GPIOA_CRL    REG(0x40010800)
#define GPIOA_CRH    REG(0x40010804)
#define GPIOA_BSRR   REG(0x40010810)
#define GPIOA_BRR    REG(0x40010814)
#define GPIOB_CRL    REG(0x40010C00)
#define GPIOB_BSRR   REG(0x40010C10)
#define GPIOB_BRR    REG(0x40010C14)
#define USART2_SR    REG(0x40004400)
#define USART2_DR    REG(0x40004404)
#define USART2_BRR   REG(0x40004408)
#define USART2_CR1   REG(0x4000440C)
#define SPI1_CR1     REG(0x40013000)
#define SPI1_CR2     REG(0x40013004)
#define SPI1_SR      REG(0x40013008)
#define SPI1_DR      (*(volatile unsigned short *)0x4001300C)
#define RCC_AHBENR   REG(0x40021014)
#define DMA1_ISR     REG(0x40020000)
#define DMA1_IFCR    REG(0x40020004)
#define DMA1_CCR3    REG(0x40020030)
#define DMA1_CNDTR3  REG(0x40020034)
#define DMA1_CPAR3   REG(0x40020038)
#define DMA1_CMAR3   REG(0x4002003C)
#define STK_CTRL     REG(0xE000E010)
#define STK_LOAD     REG(0xE000E014)
#define STK_VAL      REG(0xE000E018)
#define NVIC_ISER1   REG(0xE000E104)

#define PIN_CS   (1u << 4)
#define PIN_RST  (1u << 7)
#define PIN_BL   (1u << 1)
#define PIN_DC   (1u << 6)

static volatile unsigned int tick_ms;
static unsigned int pace_ms;
static unsigned int meter_t0;
static unsigned int cpu_smooth;
static unsigned int cpu_shown;
static unsigned int cpu_shown_ms;
static int cpu_shown_ok;
static int clock_on;
static hal_clock_t clock_base;
static unsigned int clock_ms;
static unsigned char rx_q[128];
static volatile unsigned char rx_r;
static volatile unsigned char rx_n;

void SysTick_Handler(void) {
    tick_ms++;
}

static int clock_72;

static int wait_bit(volatile unsigned int *reg, unsigned int bit) {
    volatile unsigned int n = 8000000u;
    while (n--) {
        if (*reg & bit) {
            return 1;
        }
    }
    return (*reg & bit) ? 1 : 0;
}

static void systick_on(unsigned int cycles_per_ms) {
    STK_LOAD = cycles_per_ms - 1u;
    STK_VAL = 0;
    STK_CTRL = 7u;
}

static void clock_setup(void) {
    /* 先按内部 8MHz 走。外部 8MHz 晶振起振偏慢，短等会错过，PLL 根本不会开。 */
    clock_72 = 0;
    FLASH_ACR = 0x12;
    systick_on(8000u);
    RCC_CR |= 1u << 16;
    if (!wait_bit(&RCC_CR, 1u << 17)) {
        return;
    }
    RCC_CFGR = (7u << 18) | (1u << 16) | (4u << 8);
    RCC_CR |= 1u << 24;
    if (!wait_bit(&RCC_CR, 1u << 25)) {
        return;
    }
    RCC_CFGR |= 2u;
    {
        volatile unsigned int n = 8000000u;
        while (n--) {
            if (((RCC_CFGR >> 2) & 3u) == 2u) {
                clock_72 = 1;
                systick_on(72000u);
                return;
            }
        }
    }
}

static void gpio_setup(void) {
    RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 12);
    RCC_APB1ENR |= (1u << 17);
    GPIOA_CRL &= ~((0xFu << 16) | (0xFu << 20) | (0xFu << 28) | (0xFu << 8));
    GPIOA_CRL |= (0x3u << 16) | (0xBu << 20) | (0xBu << 28) | (0xBu << 8);
    GPIOA_CRL &= ~(0xFu << 12);
    GPIOA_CRL |= (0x4u << 12);
    GPIOA_CRH &= ~(0xFu << 24);
    GPIOA_CRH |= (0x3u << 24);
    GPIOB_CRL &= ~((0xFu << 4) | (0xFu << 24) | (0xFu << 28));
    GPIOB_CRL |= (0x3u << 4) | (0x3u << 24) | (0x3u << 28);
    GPIOA_BSRR = PIN_CS | (1u << 14);
    GPIOB_BSRR = PIN_RST | PIN_DC | PIN_BL;
}

static void usart_setup(void) {
    /* APB1 在 72MHz 时是 36MHz，115200 对应 BRR 0x138。0x1388 会变成 7200。 */
    unsigned int div = (RCC_CFGR & 2u) ? 0x138u : 69u;
    USART2_BRR = div;
    USART2_CR1 = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2);
    NVIC_ISER1 = 1u << 6;
}

void USART2_IRQHandler(void) {
    unsigned int sr = USART2_SR;
    if (sr & ((1u << 5) | (1u << 3))) {
        unsigned char c = (unsigned char)USART2_DR;
        if ((sr & (1u << 5)) && rx_n < sizeof rx_q) {
            rx_q[(rx_r + rx_n) % sizeof rx_q] = c;
            rx_n++;
        }
    }
}

static void uart_putc(unsigned char c) {
    unsigned int n = 100000u;
    while ((USART2_SR & (1u << 7)) == 0 && n--) {
    }
    USART2_DR = c;
}

static void uart_puts(const char *s) {
    while (*s) {
        uart_putc((unsigned char)*s++);
    }
}

static void service_uart(void) {
}

static void delay_ms(unsigned int ms) {
    unsigned int t = tick_ms;
    while ((tick_ms - t) < ms) {
        service_uart();
    }
}

static void spi_setup(void) {
    /* SPI1 在 APB2。72MHz 除以 4 是 18MHz。像素走 DMA1 通道 3。 */
    RCC_AHBENR |= 1u;
    DMA1_CPAR3 = 0x4001300C;
    SPI1_CR1 = (1u << 9) | (1u << 8) | (1u << 3) | (1u << 2);
    SPI1_CR1 |= 1u << 6;
}

static void spi_wait(void) {
    while ((SPI1_SR & 2u) == 0) {
    }
    while (SPI1_SR & (1u << 7)) {
    }
    (void)SPI1_DR;
    (void)SPI1_SR;
}

static void spi_dma(const uint16_t *px, int n) {
    DMA1_CCR3 &= ~1u;
    DMA1_IFCR = 1u << 9;
    DMA1_CMAR3 = (unsigned int)px;
    DMA1_CNDTR3 = (unsigned int)n;
    DMA1_CCR3 = (1u << 12) | (1u << 10) | (1u << 8) | (1u << 7) | (1u << 4);
    SPI1_CR2 |= 1u << 1;
    DMA1_CCR3 |= 1u;
    while ((DMA1_ISR & (1u << 9)) == 0) {
    }
    DMA1_CCR3 &= ~1u;
    SPI1_CR2 &= ~(1u << 1);
    DMA1_IFCR = 1u << 9;
    spi_wait();
}

static void spi_byte(unsigned char data) {
    while ((SPI1_SR & 2u) == 0) {
    }
    SPI1_DR = data;
}

static void spi_bits(int wide) {
    spi_wait();
    SPI1_CR1 &= ~(1u << 6);
    if (wide) {
        SPI1_CR1 |= 1u << 11;
    } else {
        SPI1_CR1 &= ~(1u << 11);
    }
    SPI1_CR1 |= 1u << 6;
}

static void lcd_cmd(unsigned char cmd) {
    GPIOA_BRR = PIN_CS;
    GPIOB_BRR = PIN_DC;
    spi_byte(cmd);
    spi_wait();
    GPIOA_BSRR = PIN_CS;
}

static void lcd_data(unsigned char data) {
    GPIOA_BRR = PIN_CS;
    GPIOB_BSRR = PIN_DC;
    spi_byte(data);
    spi_wait();
    GPIOA_BSRR = PIN_CS;
}

static void lcd_dat16(unsigned short data) {
    GPIOA_BRR = PIN_CS;
    GPIOB_BSRR = PIN_DC;
    spi_byte((unsigned char)(data >> 8));
    spi_byte((unsigned char)data);
    spi_wait();
    GPIOA_BSRR = PIN_CS;
}

static void lcd_window(int x, int y, int w, int h) {
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
    /* 资料目录里的 ST7735S_CTC180_INITIAL.txt，对应这块 1.8 寸。
       厂家 C 例程注释写的是 1.44 寸 ST7735R，电源参数不一样。 */
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
    GPIOB_BSRR = PIN_RST;
    delay_ms(1);
    GPIOB_BRR = PIN_RST;
    delay_ms(1);
    GPIOB_BSRR = PIN_RST;
    delay_ms(120);
    while (p + 2 <= end) {
        unsigned char cmd = *p++;
        unsigned char n = *p++;
        unsigned char i;
        lcd_cmd(cmd);
        for (i = 0; i < n; i++) {
            lcd_data(*p++);
        }
        if (cmd == 0x11) {
            delay_ms(120);
        }
    }
}

static int month_days(int year, int month) {
    static const unsigned char days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = days[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        n = 29;
    }
    return n;
}

static void add_seconds(hal_clock_t *t, unsigned int sec) {
    unsigned int s = t->second + sec;
    t->second = (unsigned char)(s % 60u);
    s = t->minute + s / 60u;
    t->minute = (unsigned char)(s % 60u);
    s = t->hour + s / 60u;
    t->hour = (unsigned char)(s % 24u);
    s = s / 24u;
    while (s > 0) {
        int md = month_days(t->year, t->month);
        unsigned int left = (unsigned int)(md - t->day);
        if (s <= left) {
            t->day = (unsigned char)(t->day + s);
            break;
        }
        s -= left + 1u;
        t->day = 1;
        t->month++;
        if (t->month > 12) {
            t->month = 1;
            t->year++;
        }
    }
}

uint32_t hal_tick_ms(void) {
    return tick_ms;
}

void hal_clock_set(const hal_clock_t *in) {
    if (!in) {
        return;
    }
    clock_base = *in;
    clock_ms = tick_ms;
    clock_on = 1;
}

void hal_clock_get(hal_clock_t *out) {
    if (!out) {
        return;
    }
    if (!clock_on) {
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
    return 0;
}

void hal_display_flush(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels) {
    int n;
    if (!pixels || w <= 0 || h <= 0) {
        return;
    }
    lcd_window(x, y, w, h);
    n = w * h;
    spi_bits(1);
    GPIOA_BRR = PIN_CS;
    GPIOB_BSRR = PIN_DC;
    spi_dma(pixels, n);
    GPIOA_BSRR = PIN_CS;
    spi_bits(0);
    (void)lcd_dat16;
}

int hal_display_busy(void) {
    return 0;
}

/* SysTick is 1 ms. VAL counts down inside that millisecond, so a frame
   shorter than 1 ms is still visible. The old meter stored milliseconds*10
   and treated them as microseconds, which pinned the badge near 0%. */
static unsigned int cycle_now(void) {
    unsigned int load = STK_LOAD;
    unsigned int span = load + 1u;
    unsigned int ms = tick_ms;
    unsigned int val = STK_VAL;

    if (ms != tick_ms) {
        ms = tick_ms;
        val = STK_VAL;
    }
    return ms * span + (load - val);
}

void hal_meter_begin(void) {
    meter_t0 = cycle_now();
}

void hal_meter_end(void) {
    unsigned int dt = cycle_now() - meter_t0;
    unsigned int span = STK_LOAD + 1u;
    unsigned int per_us = span / 1000u;
    unsigned int us;
    unsigned int tenths;
    unsigned int now;

    if (per_us == 0) {
        per_us = 1;
    }
    us = dt / per_us;
    if (us >= 16667u) {
        tenths = 1000u;
    } else {
        tenths = us * 1000u / 16667u;
    }
    if (!cpu_shown_ok) {
        cpu_smooth = tenths;
    } else {
        cpu_smooth = (cpu_smooth * 3u + tenths) / 4u;
    }
    now = tick_ms;
    if (!cpu_shown_ok || now - cpu_shown_ms >= 500u) {
        cpu_shown = cpu_smooth;
        cpu_shown_ms = now;
        cpu_shown_ok = 1;
    }
}

uint16_t hal_cpu_tenths(void) {
    return (uint16_t)cpu_shown;
}

void hal_frame_pace(void) {
    service_uart();
    while ((tick_ms - pace_ms) < 16u) {
        service_uart();
    }
    pace_ms = tick_ms;
}

int hal_link_read(uint8_t *out) {
    if (!out) {
        return 0;
    }
    __asm volatile("cpsid i");
    if (rx_n == 0) {
        __asm volatile("cpsie i");
        return 0;
    }
    *out = rx_q[rx_r];
    rx_r = (unsigned char)((rx_r + 1u) % sizeof rx_q);
    rx_n--;
    __asm volatile("cpsie i");
    return 1;
}

void hal_link_write(const uint8_t *data, int len) {
    int i;
    if (!data || len <= 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        uart_putc(data[i]);
    }
}

void board_init(void) {
    clock_setup();
    gpio_setup();
    spi_setup();
    usart_setup();
    uart_puts("boot\r\n");
    uart_puts(clock_72 ? "72\r\n" : "8\r\n");
    uart_puts("hw\r\n");
    lcd_init();
    uart_puts("lcd\r\n");
}
