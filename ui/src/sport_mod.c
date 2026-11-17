#include "pic_mod.h"

#if defined(__ARM_ARCH) || defined(__thumb__)
#define PIC_ENTRY_ATTR __attribute__((section(".text.entry"), used))
void sport_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) PIC_ENTRY_ATTR;
#endif

static char *s_puts(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return dst;
}

static char *s_putu(char *dst, uint32_t v) {
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v > 0u);
    while (n > 0) *dst++ = tmp[--n];
    *dst = '\0';
    return dst;
}

static char *s_puti4(char *dst, int32_t v) {
    char buf[8];
    int len = 0, i;
    uint32_t uv;
    if (v < 0) {
        uv = (uint32_t)(-v);
    } else {
        uv = (uint32_t)v;
    }
    do {
        buf[len++] = (char)('0' + (uv % 10u));
        uv /= 10u;
    } while (uv > 0u && len < 6);
    if (v < 0) buf[len++] = '-';
    for (i = len; i < 4; i++) *dst++ = ' ';
    while (len > 0) *dst++ = buf[--len];
    *dst = '\0';
    return dst;
}

static char *s_put_deg(char *dst, char tag, int d10) {
    int v = d10 < 0 ? -d10 : d10;
    *dst++ = tag;
    *dst++ = d10 < 0 ? '-' : '+';
    if (v >= 1000) {
        dst = s_putu(dst, (uint32_t)(v / 10));
    } else {
        dst = s_putu(dst, (uint32_t)(v / 10));
        *dst++ = '.';
        *dst++ = (char)('0' + (v % 10));
    }
    *dst = '\0';
    return dst;
}

static void draw_steps_impl(const pic_mod_api_t *api, uint8_t a, int clear_bg) {
    sport_stats_t ss;
    uint32_t bars[12];
    hal_clock_t clk;
    char line[36];
    char goal[24];
    int w = (int)api->ui_w;
    int h = (int)api->ui_h;
    int margin = 70 * w / 1000;
    int clock_h = api->song_h(1);
    int number_y = 75 * h / 1000;
    int label_y = number_y + clock_h + (15 * h / 1000);
    int label_h = api->text_h("\xe4\xbb\x8a", 1);
    int footer_h = api->text_h("0", 1);
    int footer_y = h - footer_h - (30 * h / 1000);
    int chart_bot = footer_y - (50 * h / 1000);
    int chart_top = label_y + label_h + (35 * h / 1000);
    int chart_h = chart_bot - chart_top;
    int pitch = (w - margin * 2) / 12;
    int bw = pitch - (24 * w / 1000);
    int bar_x0, percent, cur, i;
    uint32_t max = 1u;
    uint32_t elapsed = api->now_ms - api->reveal_ms;
    char *p;

    api->sport_get(&ss);
    api->sport_bars(bars, 12);
    api->clock_get(&clk);
    cur = (clk.hour / 2) % 12;
    for (i = 0; i < 12; i++) {
        if (bars[i] > max) max = bars[i];
    }
    if (chart_h < 8) chart_h = 8;
    if (bw < 2) bw = 2;
    bar_x0 = (w - (11 * pitch + bw)) / 2;
    percent = ss.goal ? (int)(ss.steps * 100u / ss.goal) : 0;
    if (percent > 100) percent = 100;

    if (clear_bg && a == 255) {
        api->fill(0, 0, (int16_t)w, (int16_t)h, api->col_bg, a);
    }
    s_putu(line, ss.steps);
    if (api->song_w(1, line) <= w - margin * 2) {
        int16_t nw = api->song_w(1, line);
        api->song_draw(1, (int16_t)((w - nw) / 2), (int16_t)number_y, line, api->col_text, a);
    } else {
        int th;
        api->text_fit(goal, (int)sizeof goal, line, (int16_t)(w - margin * 2), 2);
        th = api->text_h(goal, 2);
        api->text_center((int16_t)(w / 2), (int16_t)(number_y + (clock_h - th) / 2), goal, api->col_text, a, 2);
    }
    api->text((int16_t)margin, (int16_t)label_y, "\xe4\xbb\x8a\xe6\x97\xa5\xe6\xad\xa5\xe6\x95\xb0", api->col_dim, a, 1);
    p = s_putu(goal, (uint32_t)percent);
    *p++ = '%';
    *p = '\0';
    api->text_right((int16_t)(w - margin), (int16_t)label_y, goal, api->col_ring, a, 1);

    for (i = 0; i < 12; i++) {
        uint32_t local = elapsed > (uint32_t)i * 28u ? elapsed - (uint32_t)i * 28u : 0u;
        int grow = api->ease_in_out(local * 256u / 420u);
        int bh = (int)(bars[i] * (uint32_t)chart_h / max);
        int bx = bar_x0 + i * pitch;
        uint8_t bar_a = (uint8_t)((int)a * (i == cur ? 255 : 155) / 255);
        bh = bh * grow / 255;
        if (bars[i] > 0u && grow > 160 && bh < 2) bh = 2;
        if (bars[i] == 0u) {
            api->fill((int16_t)bx, (int16_t)(chart_bot - 1), (int16_t)bw, 1, api->col_line, a);
        } else if (bh > 0) {
            api->round((int16_t)bx, (int16_t)(chart_bot - bh), (int16_t)bw, (int16_t)bh, 2,
                       i == cur ? api->col_accent : api->col_ring, bar_a);
        }
    }
    api->line((int16_t)margin, (int16_t)chart_bot, (int16_t)(w - margin), (int16_t)chart_bot, api->col_line, a);

    p = s_putu(line, ss.distance_m);
    p = s_puts(p, " m   ");
    p = s_putu(p, ss.calories);
    s_puts(p, " kcal");
    api->text_fit(goal, (int)sizeof goal, line, (int16_t)(w - margin * 2), 1);
    api->text_center((int16_t)(w / 2), (int16_t)footer_y, goal, api->col_dim, a, 1);
}

static char *s_fmt_vec(char *line, char tag, int ok, int32_t v0, int32_t v1, int32_t v2) {
    char *p = line;
    *p++ = tag;
    if (!ok) {
        s_puts(p, "  --  --  --");
        return line;
    }
    p = s_puti4(p, v0);
    p = s_puti4(p, v1);
    s_puti4(p, v2);
    return line;
}

static int s_clamp999(int v) {
    if (v > 999) return 999;
    return (v < -999) ? -999 : v;
}

static void draw_sensor_impl(const pic_mod_api_t *api, uint8_t a) {
    static const char dirs[8][3] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    imu9_state_t s9;
    char line[32];
    char rbuf[10], pbuf[10];
    const char *badge;
    uint16_t badge_col;
    int w = (int)api->ui_w;
    int h = (int)api->ui_h;
    int x = 45 * w / 1000;
    int y = 175 * h / 1000;
    int step = api->text_h("A", 1) + (12 * h / 1000);
    int max_step = (h - 20 - 2 - y) / 7;
    int t;
    char *p;

    if (step > max_step && max_step > 10) step = max_step;
    api->imu_get(&s9);
    if (s9.mag_cal == 1) {
        badge = "CAL";
        badge_col = api->col_accent;
    } else if (s9.fused_axes) {
        badge = (s9.fused_axes == 9) ? "9DOF" : ((s9.fused_axes == 6) ? "6DOF" : "3DOF");
        badge_col = (s9.fused_axes == 9) ? api->col_ring : api->col_accent;
    } else {
        badge = "--";
        badge_col = api->col_dim;
    }

    api->fill(0, 0, (int16_t)w, (int16_t)h, api->col_bg, a);
    api->round((int16_t)x, (int16_t)(40 * h / 1000), (int16_t)(20 * w / 1000),
               (int16_t)(100 * h / 1000), 1, api->col_accent, a);
    api->text((int16_t)(x + (50 * w / 1000)), (int16_t)(30 * h / 1000), "\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8", api->col_text, a, 1);
    api->text((int16_t)(x + (50 * w / 1000) + api->text_w("\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8", 1) + (45 * w / 1000)),
              (int16_t)(30 * h / 1000), badge, badge_col, a, 1);
    api->line((int16_t)x, (int16_t)(155 * h / 1000),
              (int16_t)(w - x), (int16_t)(155 * h / 1000), api->col_line, a);

    if (s9.mag_cal == 1) {
        api->text((int16_t)x, (int16_t)y, "MAG CAL ROT360", api->col_accent, a, 1);
    } else if (s9.imu_ok) {
        t = s9.temp;
        p = s_putu(line, (uint32_t)s9.fused_axes);
        p = s_puts(p, "DOF");
        *p++ = (s9.mag_cal == 2) ? '*' : (s9.gyro_cal ? '.' : ' ');
        p = s_puts(p, " T");
        if (t < 0) { *p++ = '-'; t = -t; }
        p = s_putu(p, (uint32_t)(t / 10));
        *p++ = '.';
        *p++ = (char)('0' + (t % 10));
        *p++ = 'C';
        *p = '\0';
        api->text((int16_t)x, (int16_t)y, line,
                  s9.fused_axes == 9 ? api->col_ring : api->col_accent, a, 1);
    } else {
        p = s_puts(line, api->mag_name());
        *p++ = ' ';
        s_puts(p, s9.mag_ok ? "3DOF" : "--");
        api->text((int16_t)x, (int16_t)y, line, s9.mag_ok ? api->col_accent : api->col_dim, a, 1);
    }

    if (s9.imu_ok) {
        s_put_deg(rbuf, 'R', s9.roll_d10);
        s_put_deg(pbuf, 'P', s9.pitch_d10);
        p = s_puts(line, rbuf);
        *p++ = ' ';
        *p++ = ' ';
        s_puts(p, pbuf);
    } else {
        s_puts(line, "R --   P --");
    }
    api->text((int16_t)x, (int16_t)(y + step), line, api->col_text, a, 1);

    if (s9.fused_axes) {
        const char *cstr = s9.mag_cal == 1 ? "CAL" :
                           (s9.mag_cal == 2 ? "MCAL" : (s9.gyro_cal ? "OK" : "INIT"));
        *line = 'H';
        p = s_putu(line + 1, (uint32_t)s9.heading_deg);
        *p++ = ' ';
        p = s_puts(p, dirs[((s9.heading_deg + 22u) % 360u) / 45u]);
        *p++ = ' ';
        s_puts(p, cstr);
    } else {
        s_puts(line, "H --- --");
    }
    api->text((int16_t)x, (int16_t)(y + step * 2), line,
              s9.mag_ok ? api->col_ring : api->col_accent, a, 1);

    api->text((int16_t)x, (int16_t)(y + step * 3),
              s_fmt_vec(line, 'A', s9.imu_ok, s9.ax / 100, s9.ay / 100, s9.az / 100), api->col_text, a, 1);
    api->text((int16_t)x, (int16_t)(y + step * 4),
              s_fmt_vec(line, 'G', s9.imu_ok, s_clamp999(s9.gx / 10), s_clamp999(s9.gy / 10), s_clamp999(s9.gz / 10)),
              api->col_text, a, 1);
    api->text((int16_t)x, (int16_t)(y + step * 5),
              s_fmt_vec(line, 'M', s9.mag_ok, s9.mx / 10, s9.my / 10, s9.mz / 10), api->col_text, a, 1);

    p = s_puts(line, api->i2c_scan ? api->i2c_scan : "I2C --");
    *p++ = ' ';
    s_puts(p, s9.mag_cal == 1 ? "K2ok" : "K2c");
    api->text((int16_t)x, (int16_t)(y + step * 6), line, api->col_dim, a, 1);
}

static int c_sin_q8(int deg) {
    static const uint8_t sin5[19] = {
        0, 22, 44, 66, 87, 108, 128, 146, 164, 180,
        195, 209, 221, 231, 240, 246, 251, 254, 255
    };
    int q, ang, idx, rem, v;
    deg %= 360;
    if (deg < 0) deg += 360;
    q = deg / 90;
    ang = deg % 90;
    if (q == 1 || q == 3) ang = 90 - ang;
    idx = ang / 5;
    rem = ang % 5;
    v = sin5[idx] + (sin5[idx + 1] - sin5[idx]) * rem / 5;
    return (q >= 2) ? -v : v;
}

static void c_disc(const pic_mod_api_t *api, int cx, int cy, int r, uint16_t col, uint8_t a) {
    api->round((int16_t)(cx - r), (int16_t)(cy - r), (int16_t)(r * 2), (int16_t)(r * 2), (int16_t)r, col, a);
}

static void draw_compass_2d_impl(const pic_mod_api_t *api, uint8_t a, int clear_bg) {
    static const char dirs[8][8] = {
        "\xe5\x8c\x97", "\xe4\xb8\x9c\xe5\x8c\x97", "\xe4\xb8\x9c", "\xe4\xb8\x9c\xe5\x8d\x97",
        "\xe5\x8d\x97", "\xe8\xa5\xbf\xe5\x8d\x97", "\xe8\xa5\xbf", "\xe8\xa5\xbf\xe5\x8c\x97"
    };
    imu9_state_t s9;
    int w = (int)api->ui_w;
    int h = (int)api->ui_h;
    int sc = w >= 200 ? 2 : 1;
    int cx = w >> 1;
    int cy = h * 46 / 100;
    int r = (w < h ? w : h) * 20 / 100;
    int needle_deg, dx, dy, len;
    char line[32];
    char *p;

    api->imu_get(&s9);
    if (clear_bg && a == 255) {
        api->fill(0, 0, (int16_t)w, (int16_t)h, api->col_bg, a);
    }
    api->text((int16_t)(70 * w / 1000), (int16_t)(55 * h / 1000), "\xe6\x8c\x87\xe5\x8d\x97\xe9\x92\x88", api->col_text, a, 1);
    if (r < 16) r = 16;
    c_disc(api, cx, cy, r, api->col_line, a);
    c_disc(api, cx, cy, r - 2, api->col_bg, a);
    needle_deg = s9.fused_axes ? ((360 - (int)s9.heading_deg) % 360) : 0;
    dx = c_sin_q8(needle_deg);
    dy = -c_sin_q8(needle_deg + 90);
    len = r - 4;
    api->line((int16_t)cx, (int16_t)cy, (int16_t)(cx + ((dx * len) >> 8)), (int16_t)(cy + ((dy * len) >> 8)), 0xF800u, a);
    api->line((int16_t)cx, (int16_t)cy, (int16_t)(cx - ((dx * len * 7 / 10) >> 8)), (int16_t)(cy - ((dy * len * 7 / 10) >> 8)), api->col_text, a);
    c_disc(api, cx, cy, 3, api->col_ring, a);

    if (s9.fused_axes) {
        p = s_putu(line, (uint32_t)s9.heading_deg);
        p = s_puts(p, "\xe5\xba\xa6 ");
        s_puts(p, dirs[((s9.heading_deg + 22u) % 360u) / 45u]);
    } else {
        s_puts(line, "---\xe5\xba\xa6 --");
    }
    api->text_center((int16_t)cx, (int16_t)(cy + r + 8), line, api->col_text, a, (uint8_t)sc);
}

static void sport_mod_impl(const pic_mod_api_t *api, uint8_t mode, uint8_t a, int clear_bg) {
    if (!api || a == 0) return;
    if (mode == 2u) {
        draw_compass_2d_impl(api, a, clear_bg);
    } else if (mode == 1u) {
        draw_sensor_impl(api, a);
    } else {
        draw_steps_impl(api, a, clear_bg);
    }
}

#if defined(__ARM_ARCH) || defined(__thumb__)
void sport_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    sport_mod_impl(api, mode, alpha, clear_bg);
}
#else
void sport_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    sport_mod_impl(api, mode, alpha, clear_bg);
}
#endif
