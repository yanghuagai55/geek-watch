#include "led_bl.h"
#include "board_config.h"
#include "hal.h"
#include "at32f421_gpio.h"
#include "at32f421_tmr.h"

/* 背光 PWM 周期：120MHz/6000 = 20kHz，高于可见闪烁，且不受 CPU 负载影响。 */
#define BL_PWM_PERIOD 6000u

void led_init(void) {
    gpio_init_type g;
    crm_periph_clock_enable(LED_CRM_CLK, TRUE);
    gpio_default_para_init(&g);
    g.gpio_pins = LED_PIN;
    g.gpio_mode = GPIO_MODE_OUTPUT;
    g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    g.gpio_pull = GPIO_PULL_NONE;
    g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(LED_PORT, &g);
    gpio_bits_reset(LED_PORT, LED_PIN);

    /* BLE_STATE 输入 */
    g.gpio_pins = BLE_STATE_PIN;
    g.gpio_mode = GPIO_MODE_INPUT;
    g.gpio_pull = GPIO_PULL_DOWN;
    gpio_init(BLE_STATE_PORT, &g);
}

void led_set(int on) {
    if (on) gpio_bits_set(LED_PORT, LED_PIN);
    else gpio_bits_reset(LED_PORT, LED_PIN);
}

void backlight_init(void) {
    gpio_init_type g;
    tmr_output_config_type oc;

    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);

    /* PB0 = TMR3_CH3（库例程 oc_toggle_tmr3 同复用 MUX1） */
    gpio_default_para_init(&g);
    g.gpio_pins = LCD_BLK_PIN;
    g.gpio_mode = GPIO_MODE_MUX;
    g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    g.gpio_pull = GPIO_PULL_NONE;
    g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(LCD_BLK_PORT, &g);
    gpio_pin_mux_config(LCD_BLK_PORT, GPIO_PINS_SOURCE0, GPIO_MUX_1);

    tmr_base_init(TMR3, BL_PWM_PERIOD - 1u, 0u);
    tmr_cnt_dir_set(TMR3, TMR_COUNT_UP);
    tmr_output_default_para_init(&oc);
    oc.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    oc.oc_idle_state = FALSE;
    oc.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
    oc.oc_output_state = TRUE;
    tmr_output_channel_config(TMR3, TMR_SELECT_CHANNEL_3, &oc);
    tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_3, BL_PWM_PERIOD);
    tmr_period_buffer_enable(TMR3, TRUE);
    tmr_output_channel_buffer_enable(TMR3, TMR_SELECT_CHANNEL_3, TRUE);
    tmr_counter_enable(TMR3, TRUE);
}

void backlight_set(uint8_t level) {
    tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_3,
                          (uint32_t)level * BL_PWM_PERIOD / 255u);
}

int ble_state_read(void) {
    return (BLE_STATE_PORT->idt & BLE_STATE_PIN) ? 1 : 0;
}