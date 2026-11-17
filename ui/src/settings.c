#include "settings.h"
#include "ui.h"
#include "hal.h"
#include "sport.h"
#include "imu9.h"

#include <string.h>

#define CFG_MAGIC       0x41544332u /* 'ATC2' */
#define CFG_VERSION     2u
#define CFG_RECORD_LEN  64u
#define CFG_SLOT0_OFF   (LINK_EXT_OFF_CFG + 0x00u)
#define CFG_SLOT1_OFF   (LINK_EXT_OFF_CFG + 0x80u)

static settings_t st;
static uint32_t cfg_seq;
static uint8_t cfg_active_slot;
static uint16_t clk_year = 2026u;
static uint8_t clk_month = 1u;
static uint8_t clk_day = 1u;
static uint8_t clk_hour = 0u;
static uint8_t clk_min = 0u;
static uint8_t clk_recorded = 0u;

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t settings_crc16_step(uint16_t crc, const uint8_t *buf, uint32_t len) {
    uint32_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ ((uint16_t)buf[i] << 8));
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t settings_crc16(const uint8_t *buf, uint32_t len) {
    return settings_crc16_step(0xFFFFu, buf, len);
}

static int read_slot(uint32_t off, uint8_t *buf) {
    if (!hal_ext_flash_read(off, buf, CFG_RECORD_LEN)) {
        return 0;
    }
    if (get_u32(buf + 0) != CFG_MAGIC || buf[8] != CFG_VERSION) {
        return 0;
    }
    return get_u16(buf + 62) == settings_crc16(buf, 62u);
}

/* 共享 20 字节序列化区：同时供 AT24C256 持久化槽 [9..28] 与蓝牙 0x2A/0x2C 复用。 */
void settings_pack_wire(uint8_t *dst20) {
    sport_stats_t sp;
    uint32_t goal;

    sport_get(&sp);
    goal = sp.goal ? sp.goal : 8000u;
    dst20[0] = (uint8_t)(0x01u |
                         ((st.cfg_source == UI_CFG_PRIO_APP) ? 0x02u : 0u) |
                         (st.use_24h ? 0x04u : 0u) |
                         (st.debug ? 0x08u : 0u) |
                         (st.bluetooth ? 0x10u : 0u) |
                         (st.msg_auto_clear ? 0x20u : 0u));
    dst20[1] = st.raise_wrist ? 1u : 0u;
    dst20[2] = st.key_beep ? 1u : 0u;
    dst20[3] = st.brightness <= 2u ? st.brightness : 1u;
    dst20[4] = st.led_mode <= 2u ? st.led_mode : 1u;
    dst20[5] = st.screen_off <= 4u ? st.screen_off : 0u;
    put_u32(dst20 + 6, goal);
    dst20[10] = st.dial_id < UI_DIAL_COUNT ? st.dial_id : UI_DIAL_AMBER;
    dst20[11] = st.dial_top <= UI_DIAL_COMP_NONE ? st.dial_top : UI_DIAL_COMP_DATE;
    dst20[12] = st.dial_bot <= UI_DIAL_COMP_NONE ? st.dial_bot : UI_DIAL_COMP_STEP;
    dst20[13] = st.dial_flags;
    put_u16(dst20 + 14, st.dial_text);
    put_u16(dst20 + 16, st.dial_accent);
    put_u16(dst20 + 18, st.dial_bg);
}

void settings_unpack_wire(const uint8_t *src20) {
    uint32_t goal = get_u32(src20 + 6);

    st.cfg_source = (src20[0] & 0x02u) ? UI_CFG_PRIO_APP : UI_CFG_PRIO_I2C;
    st.use_24h = (src20[0] & 0x04u) ? 1u : 0u;
    st.debug = (src20[0] & 0x08u) ? 1u : 0u;
    st.bluetooth = (src20[0] & 0x10u) ? 1u : st.bluetooth;
    st.msg_auto_clear = (src20[0] & 0x20u) ? 1u : 0u;
    st.raise_wrist = src20[1] ? 1u : 0u;
    st.key_beep = src20[2] ? 1u : 0u;
    st.brightness = src20[3] <= 2u ? src20[3] : 1u;
    st.led_mode = src20[4] <= 2u ? src20[4] : 1u;
    st.screen_off = src20[5] <= 4u ? src20[5] : 0u;
    if (goal >= 1000u && goal <= 100000u) {
        sport_set_goal(goal);
    }
    st.dial_id = src20[10] < UI_DIAL_COUNT ? src20[10] : UI_DIAL_AMBER;
    st.dial_top = src20[11] <= UI_DIAL_COMP_NONE ? src20[11] : UI_DIAL_COMP_DATE;
    st.dial_bot = src20[12] <= UI_DIAL_COMP_NONE ? src20[12] : UI_DIAL_COMP_STEP;
    st.dial_flags = src20[13];
    st.dial_text = get_u16(src20 + 14);
    st.dial_accent = get_u16(src20 + 16);
    st.dial_bg = get_u16(src20 + 18);
}

void settings_init(void) {
    memset(&st, 0, sizeof st);
    st.brightness = 1;
    st.raise_wrist = 1;
    st.key_beep = 0;
    st.bluetooth = 1;
    st.led_mode = 1;
    st.use_24h = 1;
    st.dial_top = UI_DIAL_COMP_DATE;
    st.dial_bot = UI_DIAL_COMP_STEP;
    st.dial_flags = UI_DIAL_FLAG_SECONDS | UI_DIAL_FLAG_SCRIM;
    st.dial_text = UI_COL_TEXT;
    st.dial_accent = UI_COL_MINT;
    st.dial_bg = UI_COL_BG;
    cfg_seq = 0;
    cfg_active_slot = 0;
    clk_recorded = 0;
}

settings_t *settings_get(void) {
    return &st;
}

void settings_record_clock(const void *hal_clock_ptr) {
    const hal_clock_t *c = (const hal_clock_t *)hal_clock_ptr;
    if (!c || c->year < 2025u || c->month < 1u || c->month > 12u ||
        c->day < 1u || c->day > 31u) {
        return;
    }
    clk_year = c->year;
    clk_month = c->month;
    clk_day = c->day;
    clk_hour = c->hour;
    clk_min = c->minute;
    clk_recorded = 1u;
}

int settings_load_ext(void) {
    uint8_t s0[CFG_RECORD_LEN];
    uint8_t s1[CFG_RECORD_LEN];
    const uint8_t *best = 0;
    uint32_t seq0 = 0, seq1 = 0;
    int ok0, ok1;
    uint16_t yr;

    if (!hal_ext_flash_present()) {
        return 0;
    }
    ok0 = read_slot(CFG_SLOT0_OFF, s0);
    ok1 = read_slot(CFG_SLOT1_OFF, s1);
    if (!ok0 && !ok1) {
        return 0;
    }
    if (ok0) seq0 = get_u32(s0 + 4);
    if (ok1) seq1 = get_u32(s1 + 4);
    if (ok0 && (!ok1 || (int32_t)(seq0 - seq1) >= 0)) {
        best = s0;
        cfg_seq = seq0;
        cfg_active_slot = 0;
    } else {
        best = s1;
        cfg_seq = seq1;
        cfg_active_slot = 1;
    }

    settings_unpack_wire(best + 9);
    st.cfg_loaded = 1u;

    st.img_w = (int16_t)get_u16(best + 29);
    st.img_h = (int16_t)get_u16(best + 31);
    st.img_fmt = best[33];
    st.img_bytes = get_u16(best + 35);
    if (best[34] && st.img_w > 0 && st.img_w <= 240 &&
        st.img_h > 0 && st.img_h <= 240 &&
        st.img_fmt >= UI_EXT_FMT_1BPP && st.img_fmt <= UI_EXT_FMT_RGB565_RLE &&
        st.img_bytes > 0u && st.img_bytes <= LINK_EXT_IMG_MAX) {
        st.img_ready = 1u;
        st.img_on_chip = (best[34] == 2u && (uint32_t)st.img_bytes <= hal_int_img_cap()) ? 1u : 0u;
    } else {
        st.img_ready = 0u;
        st.img_on_chip = 0u;
        st.img_w = st.img_h = 0;
        st.img_fmt = 0;
        st.img_bytes = 0u;
    }

    if (best[37]) {
        imu9_load_mag_cal((int16_t)get_u16(best + 38),
                          (int16_t)get_u16(best + 40),
                          (int16_t)get_u16(best + 42));
        st.mag_cal_saved = 1u;
    }

    yr = get_u16(best + 44);
    if (yr >= 2025u && yr <= 2099u &&
        best[46] >= 1u && best[46] <= 12u &&
        best[47] >= 1u && best[47] <= 31u &&
        best[48] <= 23u && best[49] <= 59u) {
        hal_clock_t c;
        c.year = yr;
        c.month = best[46];
        c.day = best[47];
        c.hour = best[48];
        c.minute = best[49];
        c.second = 0u;
        hal_clock_set(&c);
        settings_record_clock(&c);
    }
    return 1;
}

int settings_save_ext(uint8_t source_prio) {
    uint8_t buf[CFG_RECORD_LEN];
    imu9_state_t s9;
    hal_clock_t clk;
    uint32_t next_slot;

    st.cfg_source = source_prio <= UI_CFG_PRIO_APP ? source_prio : UI_CFG_PRIO_I2C;
    if (!hal_ext_flash_present()) {
        return 0;
    }
    imu9_get(&s9);
    hal_clock_get(&clk);
    if (clk.year >= 2025u) {
        settings_record_clock(&clk);
    }

    memset(buf, 0, sizeof buf);
    cfg_seq++;
    put_u32(buf + 0, CFG_MAGIC);
    put_u32(buf + 4, cfg_seq);
    buf[8] = CFG_VERSION;
    settings_pack_wire(buf + 9);

    put_u16(buf + 29, (uint16_t)st.img_w);
    put_u16(buf + 31, (uint16_t)st.img_h);
    buf[33] = st.img_fmt;
    buf[34] = st.img_ready ? (st.img_on_chip ? 2u : 1u) : 0u;
    put_u16(buf + 35, st.img_bytes);

    if (s9.mag_cal == 2u) {
        buf[37] = 1u;
        put_u16(buf + 38, (uint16_t)s9.mox);
        put_u16(buf + 40, (uint16_t)s9.moy);
        put_u16(buf + 42, (uint16_t)s9.moz);
        st.mag_cal_saved = 1u;
    }

    if (clk_recorded) {
        put_u16(buf + 44, clk_year);
        buf[46] = clk_month;
        buf[47] = clk_day;
        buf[48] = clk_hour;
        buf[49] = clk_min;
    }

    put_u16(buf + 62, settings_crc16(buf, 62u));

    next_slot = (uint32_t)(cfg_active_slot ^ 1u);
    if (!hal_ext_flash_write(next_slot ? CFG_SLOT1_OFF : CFG_SLOT0_OFF,
                             buf, CFG_RECORD_LEN)) {
        return 0;
    }
    cfg_active_slot = (uint8_t)next_slot;
    return 1;
}
