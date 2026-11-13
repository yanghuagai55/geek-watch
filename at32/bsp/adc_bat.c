#include "adc_bat.h"
#include "board_config.h"
#include "hal.h"
#include "at32f421_adc.h"
#include "at32f421_crm.h"
#include "at32f421_gpio.h"

void adc_init(void) {
    gpio_init_type g;
    adc_base_config_type base;
    uint32_t n;

    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    /* 120MHz PCLK2 默认 /2 会把 ADC 超到 60MHz；按厂商例程降到 20MHz。 */
    crm_adc_clock_div_set(CRM_ADC_DIV_6);

    gpio_default_para_init(&g);
    g.gpio_pins = MIC_ADC_PIN;
    g.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init(MIC_ADC_PORT, &g);
#if WATCH_BAT_ADC_ENABLE
    g.gpio_pins = BAT_ADC_PIN;
    gpio_init(BAT_ADC_PORT, &g);
#endif

    adc_base_default_para_init(&base);
    base.sequence_mode = FALSE;
    base.repeat_mode = FALSE;
    base.data_align = ADC_RIGHT_ALIGNMENT;
    base.ordinary_channel_length = 1;
    adc_base_config(ADC1, &base);
    adc_ordinary_channel_set(ADC1, (adc_channel_select_type)MIC_ADC_CHANNEL, 1, ADC_SAMPLETIME_239_5);
    adc_ordinary_conversion_trigger_set(ADC1, ADC12_ORDINARY_TRIG_SOFTWARE, TRUE);
    adc_enable(ADC1, TRUE);

    adc_calibration_init(ADC1);
    for (n = 0; n < 100000u && adc_calibration_init_status_get(ADC1) == RESET; n++) {}
    adc_calibration_start(ADC1);
    for (n = 0; n < 100000u && adc_calibration_status_get(ADC1) == RESET; n++) {}
}

uint16_t adc_read_mv(uint8_t channel) {
    uint32_t n;
    uint16_t raw;

    adc_ordinary_channel_set(ADC1, (adc_channel_select_type)channel, 1, ADC_SAMPLETIME_239_5);
    adc_flag_clear(ADC1, ADC_CCE_FLAG);
    adc_ordinary_software_trigger_enable(ADC1, TRUE);
    for (n = 0; n < 100000u && adc_flag_get(ADC1, ADC_CCE_FLAG) == RESET; n++) {}
    raw = adc_ordinary_conversion_data_get(ADC1);
    return (uint16_t)((uint32_t)raw * 3300u / 4095u);
}

uint16_t battery_mv(void) {
#if WATCH_BAT_ADC_ENABLE
    /* 分压器源阻抗 50k，单次采样有噪声；4 次平均 + 500ms 缓存，
       既让角标稳定，也避免每帧都占用 ADC。 */
    static uint16_t cached;
    static uint32_t updated_ms;
    static int valid;
    uint32_t now = hal_tick_ms();

    if(!valid || (now - updated_ms) >= 500u) {
        uint32_t sum = 0;
        int i;
        for(i = 0; i < 4; i++) {
            sum += adc_read_mv(BAT_ADC_CHANNEL);
        }
        cached = (uint16_t)(sum / 4u * BAT_DIV_NUM / BAT_DIV_DEN);
        updated_ms = now;
        valid = 1;
    }
    return cached;
#else
    return 0;
#endif
}

uint8_t battery_pct(void) {
#if WATCH_BAT_ADC_ENABLE
    uint32_t mv = battery_mv();
    /* 没接电池时 BAT 悬空，分压点只有几十~几百 mV；当作“未知”，
       角标显示 --，关于页显示 USB 供电，避免误报 0%。 */
    if (mv < 2000u) return 0xFF;
    if (mv <= 3300u) return 0;
    if (mv >= 4200u) return 100;
    return (uint8_t)((mv - 3300u) * 100u / 900u);
#else
    return 0xFF;
#endif
}