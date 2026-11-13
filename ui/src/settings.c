#include "settings.h"
#include "hal.h"

static settings_t st;

void settings_init(void) {
    st.debug = 0;
    st.brightness = 1;
    st.raise_wrist = 1;
    st.key_beep = 1;
    st.bluetooth = 1;
    st.led_mode = 1;
    st.screen_off = 0;
}

settings_t *settings_get(void) {
    return &st;
}

void about_fill(about_info_t *out) {
    if (!out) return;
    out->version = hal_fw_version();
    out->build = hal_fw_build();
    out->mcu = hal_mcu_name();
    out->sysclk_mhz = hal_sysclk_mhz();
    out->clock_src = hal_clock_src();
    out->battery_mv = hal_battery_mv();
    out->battery_pct = hal_battery_pct();
}