#include "beep.h"
#include "board_config.h"
#include "hal.h"
#include "at32f421_gpio.h"

/* 有源蜂鸣器：MOS 低边驱动，GPIO 开关即可，freq 忽略。 */
static uint8_t on;
static uint32_t off_at;

void beep_init(void) {
    gpio_init_type g;
    crm_periph_clock_enable(BEEP_CRM_CLK, TRUE);
    gpio_default_para_init(&g);
    g.gpio_pins = BEEP_PIN;
    g.gpio_mode = GPIO_MODE_OUTPUT;
    g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    g.gpio_pull = GPIO_PULL_DOWN;
    g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(BEEP_PORT, &g);
    gpio_bits_reset(BEEP_PORT, BEEP_PIN);
    on = 0;
}

void beep_set(uint16_t freq_hz, uint16_t ms) {
    (void)freq_hz;
    if (ms == 0) {
        gpio_bits_reset(BEEP_PORT, BEEP_PIN);
        on = 0;
        return;
    }
    gpio_bits_set(BEEP_PORT, BEEP_PIN);
    on = 1;
    off_at = hal_tick_ms() + ms;
}

void beep_tick(uint32_t now_ms) {
    if (on && (int32_t)(now_ms - off_at) >= 0) {
        gpio_bits_reset(BEEP_PORT, BEEP_PIN);
        on = 0;
    }
}