#include "beep.h"
#include "board_config.h"
#include "hal.h"
#include "at32f421_gpio.h"

/* PA8 接 AO3400A 低边 MOS 驱动 1207 转子马达（含 1N4148 续流）。
   转子马达需要足额直流高电平脉冲起转，按传统功能机场景提供单震/长震/双震/短信三连震。 */
static uint8_t phase;
static uint8_t pat;
static uint16_t step_ms;
static uint32_t next_at;

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
    phase = 0u;
    pat = 0u;
    step_ms = 0u;
}

void beep_set(uint16_t freq_hz, uint16_t ms) {
    if (ms == 0u) {
        gpio_bits_reset(BEEP_PORT, BEEP_PIN);
        phase = 0u;
        return;
    }
    gpio_bits_set(BEEP_PORT, BEEP_PIN);
    step_ms = ms;
    pat = (uint8_t)((freq_hz == 2u || freq_hz == 3u) ? freq_hz : 1u);
    phase = (uint8_t)(pat == 3u ? 5u : (pat == 2u ? 3u : 1u));
    next_at = hal_tick_ms() + ms;
}

void beep_tick(uint32_t now_ms) {
    if (phase == 0u || (int32_t)(now_ms - next_at) < 0) {
        return;
    }
    phase--;
    if (phase == 0u) {
        gpio_bits_reset(BEEP_PORT, BEEP_PIN);
        return;
    }
    if ((phase & 1u) == 0u) {
        gpio_bits_reset(BEEP_PORT, BEEP_PIN);
        next_at = now_ms + (pat == 3u ? 55u : (uint32_t)step_ms);
    } else {
        gpio_bits_set(BEEP_PORT, BEEP_PIN);
        next_at = now_ms + ((pat == 3u && phase == 1u) ? (uint32_t)step_ms * 2u : (uint32_t)step_ms);
    }
}