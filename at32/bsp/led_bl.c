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

static int led_mode;
static uint32_t led_t0;
static int led_phase;

void led_set(int on) {
    led_mode = LED_MODE_OFF;
    led_phase = on ? 1 : 0;
    if (on) gpio_bits_set(LED_PORT, LED_PIN);
    else gpio_bits_reset(LED_PORT, LED_PIN);
}

void led_mode_set(int mode) {
    led_mode = mode;
    led_t0 = hal_tick_ms();
    if (mode == LED_MODE_BLINK) {
        led_phase = 1;
        gpio_bits_set(LED_PORT, LED_PIN);
    } else {
        led_phase = mode == LED_MODE_ON ? 1 : 0;
        if (led_phase) gpio_bits_set(LED_PORT, LED_PIN);
        else gpio_bits_reset(LED_PORT, LED_PIN);
    }
}

void led_tick(uint32_t now_ms) {
    int on;

    if (led_mode != LED_MODE_BLINK) {
        return;
    }
    on = (((now_ms - led_t0) / 500u) & 1u) ? 0 : 1;
    if (on == led_phase) {
        return;
    }
    led_phase = on;
    if (on) gpio_bits_set(LED_PORT, LED_PIN);
    else gpio_bits_reset(LED_PORT, LED_PIN);
}

void backlight_init(void) {
    gpio_init_type g;

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

    TMR3->pr = BL_PWM_PERIOD - 1u;
    TMR3->div = 0u;
    TMR3->swevt = 1u;
    TMR3->cm2 = 0x0068u;   /* ch3 PWM_MODE_A (6<<4) | ch3oben (1<<3) */
    TMR3->ctrl2 = 0u;
    TMR3->cctrl = 0x0100u; /* ch3en = 1, active high */
    TMR3->c3dt = BL_PWM_PERIOD;
    TMR3->ctrl1 = 0x0081u; /* prben | tmren */
}

void backlight_set(uint8_t level) {
    TMR3->c3dt = (uint32_t)level * BL_PWM_PERIOD / 255u;
}

int ble_state_read(void) {
    return (BLE_STATE_PORT->idt & BLE_STATE_PIN) ? 1 : 0;
}