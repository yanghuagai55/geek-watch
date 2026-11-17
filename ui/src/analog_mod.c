#include "pic_mod.h"

#if defined(__ARM_ARCH) || defined(__thumb__)
#define PIC_ENTRY_ATTR __attribute__((section(".text.entry"), used))
void analog_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) PIC_ENTRY_ATTR;
#endif

static char *im_puts(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return dst;
}

static char *im_putu2(char *dst, uint32_t v) {
    *dst++ = (char)('0' + (v / 10u) % 10u);
    *dst++ = (char)('0' + (v % 10u));
    *dst = '\0';
    return dst;
}

static char *im_putu3(char *dst, uint32_t v) {
    *dst++ = (char)('0' + (v / 100u) % 10u);
    *dst++ = (char)('0' + (v / 10u) % 10u);
    *dst++ = (char)('0' + (v % 10u));
    *dst = '\0';
    return dst;
}

static char *im_put_signed(char *dst, int v) {
    char tmp[8];
    int n = 0;
    uint32_t uv;
    *dst++ = (v < 0) ? '-' : '+';
    uv = (uint32_t)(v < 0 ? -v : v);
    do {
        tmp[n++] = (char)('0' + (uv % 10u));
        uv /= 10u;
    } while (uv > 0u);
    while (n > 0) *dst++ = tmp[--n];
    *dst = '\0';
    return dst;
}

static int a_sin_q8(int deg) {
    static const uint8_t sin5[19] = {
        0, 22, 44, 66, 87, 108, 128, 146, 164, 180,
        195, 209, 221, 231, 240, 246, 251, 254, 255
    };
    int q, a, i, r, v;
    deg %= 360;
    if (deg < 0) deg += 360;
    q = deg / 90;
    a = deg % 90;
    if (q == 1 || q == 3) a = 90 - a;
    i = a / 5;
    r = a % 5;
    v = sin5[i] + (sin5[i + 1] - sin5[i]) * r / 5;
    return (q >= 2) ? -v : v;
}

static void a_vec(int deg, int *dx, int *dy) {
    *dx = a_sin_q8(deg);
    *dy = -a_sin_q8(deg + 90);
}

static void a_disc(const pic_mod_api_t *api, int cx, int cy, int r, uint16_t col, uint8_t a) {
    api->round((int16_t)(cx - r), (int16_t)(cy - r), (int16_t)(r * 2), (int16_t)(r * 2), (int16_t)r, col, a);
}

static void analog_impl(const pic_mod_api_t *api, uint8_t mode, uint8_t a, int clear_bg) {
    hal_clock_t clk;
    int w = (int)api->ui_w;
    int h = (int)api->ui_h;
    int cx = w / 2;
    int cy = h / 2 - (15 * h / 1000);
    int r = (w < h ? w : h) * 34 / 100;
    int k, vx, vy, hr_deg, min_deg, sec_deg;
    char fitted[36];
    (void)mode;
    (void)clear_bg;

    if (!api || a == 0) return;
    api->clock_get(&clk);
    api->fill(0, 0, (int16_t)w, (int16_t)h, api->dial_bg, a);
    if (api->comp_top && api->comp_top[0]) {
        api->text_fit(fitted, (int)sizeof fitted, api->comp_top,
                      (int16_t)((w * 68 / 100)), 1);
        api->text_center((int16_t)cx, (int16_t)(50 * h / 1000), fitted, api->col_dim, a, 1);
    }
    a_disc(api, cx, cy, r, api->col_line, a);
    a_disc(api, cx, cy, r - 2, api->dial_bg, a);
    for (k = 0; k < 12; k++) {
        int r0 = r - ((k % 3 == 0) ? 7 : 4);
        int r1 = r - 3;
        a_vec(k * 30, &vx, &vy);
        api->line((int16_t)(cx + ((vx * r0) >> 8)), (int16_t)(cy + ((vy * r0) >> 8)),
                  (int16_t)(cx + ((vx * r1) >> 8)), (int16_t)(cy + ((vy * r1) >> 8)),
                  (k % 3 == 0) ? api->dial_ac : api->col_dim, a);
    }
    hr_deg = (clk.hour % 12) * 30 + clk.minute / 2;
    min_deg = clk.minute * 6;
    a_vec(hr_deg, &vx, &vy);
    api->line((int16_t)cx, (int16_t)cy,
              (int16_t)(cx + ((vx * (r * 52 / 100)) >> 8)),
              (int16_t)(cy + ((vy * (r * 52 / 100)) >> 8)), api->dial_fg, a);
    a_vec(min_deg, &vx, &vy);
    api->line((int16_t)cx, (int16_t)cy,
              (int16_t)(cx + ((vx * (r * 76 / 100)) >> 8)),
              (int16_t)(cy + ((vy * (r * 76 / 100)) >> 8)), api->dial_ac, a);
    if (api->dial_flags & 1u) {
        sec_deg = clk.second * 6;
        a_vec(sec_deg, &vx, &vy);
        api->line((int16_t)cx, (int16_t)cy,
                  (int16_t)(cx + ((vx * (r * 84 / 100)) >> 8)),
                  (int16_t)(cy + ((vy * (r * 84 / 100)) >> 8)), api->col_accent, a);
    }
    a_disc(api, cx, cy, 4, api->dial_ac, a);
    a_disc(api, cx, cy, 2, api->dial_bg, a);
    if (api->comp_bot && api->comp_bot[0]) {
        api->text_fit(fitted, (int)sizeof fitted, api->comp_bot,
                      (int16_t)(w - (140 * w / 1000)), 1);
        api->text_center((int16_t)cx, (int16_t)(885 * h / 1000), fitted, api->dial_fg, a, 1);
    }
}

static void imu_face_impl(const pic_mod_api_t *api, uint8_t a) {
    hal_clock_t clk;
    sport_stats_t sp;
    imu9_state_t s9;
    int w = (int)api->ui_w;
    int h = (int)api->ui_h;
    int margin = 70 * w / 1000;
    int cw = w - margin * 2;
    int sc = w >= 200 ? 3 : 2;
    int horizon_y = 540 * h / 1000;
    int disp_hr, pct, mid_y, max_t, max_p, pitch_off, tilt, is_level, fill;
    uint16_t ref_c;
    char tbuf[16];
    char line[36];
    char *p;

    api->clock_get(&clk);
    api->sport_get(&sp);
    api->imu_get(&s9);
    disp_hr = api->use_24h ? (int)clk.hour : (((int)clk.hour % 12) ? ((int)clk.hour % 12) : 12);
    pct = sp.goal ? (int)(sp.steps * 100u / sp.goal) : 0;
    if (pct > 100) pct = 100;

    api->fill(0, 0, (int16_t)w, (int16_t)h, api->dial_bg, a);
    p = im_puts(line, s9.fused_axes == 9 ? "9DOF " : (s9.fused_axes == 6 ? "6DOF " : "IMU "));
    p = im_putu3(p, (uint32_t)s9.heading_deg);
    im_puts(p, "\xe5\xba\xa6");
    api->text((int16_t)margin, (int16_t)(55 * h / 1000), line, api->dial_ac, a, 1);

    p = im_putu2(tbuf, (uint32_t)disp_hr);
    *p++ = ':';
    p = im_putu2(p, clk.minute);
    if (api->dial_flags & 1u) {
        *p++ = ':';
        im_putu2(p, clk.second);
    }
    api->text_center((int16_t)(w / 2), (int16_t)(240 * h / 1000), tbuf, api->dial_fg, a, (uint8_t)sc);

    api->round((int16_t)margin, (int16_t)horizon_y, (int16_t)cw, (int16_t)(150 * h / 1000), 6, api->col_blue_dim, a);
    mid_y = horizon_y + (45 * h / 1000);
    max_t = 36 * h / 1000;
    max_p = 15 * h / 1000;
    if (max_t < 4) max_t = 4;
    if (max_p < 2) max_p = 2;
    pitch_off = (int)s9.pitch_d10 / 50;
    tilt = (int)s9.roll_d10 / 30;
    if (tilt > max_t) tilt = max_t;
    if (tilt < -max_t) tilt = -max_t;
    if (pitch_off > max_p) pitch_off = max_p;
    if (pitch_off < -max_p) pitch_off = -max_p;
    is_level = (s9.roll_d10 >= -20 && s9.roll_d10 <= 20 && s9.pitch_d10 >= -20 && s9.pitch_d10 <= 20);
    ref_c = is_level ? 0x2F54u : 0x7412u;
    api->line((int16_t)(margin + 3), (int16_t)mid_y, (int16_t)(margin + 6), (int16_t)mid_y, ref_c, a);
    api->line((int16_t)(margin + cw - 7), (int16_t)mid_y, (int16_t)(margin + cw - 4), (int16_t)mid_y, ref_c, a);
    api->line((int16_t)(w / 2 - 3), (int16_t)mid_y, (int16_t)(w / 2 + 3), (int16_t)mid_y, ref_c, a);
    api->line((int16_t)(margin + 8), (int16_t)(mid_y + pitch_off + tilt),
              (int16_t)(margin + cw - 8), (int16_t)(mid_y + pitch_off - tilt),
              is_level ? 0x2F54u : api->dial_ac, a);

    p = im_puts(line, "R ");
    p = im_put_signed(p, (int)(s9.roll_d10 / 10));
    p = im_puts(p, "  P ");
    im_put_signed(p, (int)(s9.pitch_d10 / 10));
    api->text_center((int16_t)(w / 2), (int16_t)(horizon_y + 95 * h / 1000), line, api->dial_fg, a, 1);

    api->round((int16_t)margin, (int16_t)(770 * h / 1000), (int16_t)cw, (int16_t)(20 * h / 1000), 2, api->col_blue_dim, a);
    if (pct > 0) {
        fill = cw * pct / 100;
        if (fill < 2) fill = 2;
        api->round((int16_t)margin, (int16_t)(770 * h / 1000), (int16_t)fill, (int16_t)(20 * h / 1000), 2, api->dial_ac, a);
    }
    if (api->comp_bot && api->comp_bot[0]) {
        api->text_center((int16_t)(w / 2), (int16_t)(840 * h / 1000), api->comp_bot, api->col_dim, a, 1);
    }
}

#if defined(__ARM_ARCH) || defined(__thumb__)
void analog_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    if (!api || alpha == 0) return;
    if (mode == 1u) {
        imu_face_impl(api, alpha);
    } else {
        analog_impl(api, mode, alpha, clear_bg);
    }
}
#else
void analog_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    if (!api || alpha == 0) return;
    if (mode == 1u) {
        imu_face_impl(api, alpha);
    } else {
        analog_impl(api, mode, alpha, clear_bg);
    }
}
#endif
