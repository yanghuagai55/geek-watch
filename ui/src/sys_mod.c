#include "pic_mod.h"

#if defined(__ARM_ARCH) || defined(__thumb__)
#define PIC_ENTRY_ATTR __attribute__((section(".text.entry"), used))
void sys_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) PIC_ENTRY_ATTR;
#endif

static const int8_t sm_sin[16] = {
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49
};

static int16_t sm_trig(uint32_t pos) {
    uint32_t i = (pos >> 8) & 15u;
    int32_t f = (int32_t)(pos & 255u);
    int32_t a = sm_sin[i];
    int32_t b = sm_sin[(i + 1u) & 15u];
    return (int16_t)((a * (256 - f) + b * f) >> 8);
}

static void sm_disc(const pic_mod_api_t *api, int cx, int cy, int r, uint16_t col, uint8_t a) {
    if (r < 1) return;
    api->round((int16_t)(cx - r), (int16_t)(cy - r),
               (int16_t)(r * 2), (int16_t)(r * 2), (int16_t)r, col, a);
}

static void sm_stroke_w(const pic_mod_api_t *api, int x0, int y0, int x1, int y1,
                        int width, uint16_t col, uint8_t a, int caps) {
    (void)caps;
    if (width <= 2) {
        api->line((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, col, a);
    } else {
        api->line_w((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint8_t)width, col, a);
    }
}

typedef struct {
    int8_t x0, y0, x1, y1;
    uint8_t r, ci;
} sm_svg_t;

static void draw_runner_icon(const pic_mod_api_t *api, int x, int y, int w, int h,
                             uint32_t ms, uint8_t a) {
    /* Exact 5-frame AI SVG runner from original firmware (ci: 0=accent, 1=white, 2=accent) */
    static const sm_svg_t run_svg[5][10] = {
        { {21,13,14,24,0,0}, {14,24,21,32,0,0}, {21,32,23,42,0,0}, {14,24,7,28,0,2}, {7,28,2,21,0,2},
          {20,15,25,21,0,2}, {25,21,29,15,0,1}, {20,15,13,20,0,0}, {13,20,17,26,0,1}, {24,8,0,0,3,1} },
        { {21,15,15,26,0,0}, {15,26,22,34,0,0}, {22,34,19,42,0,0}, {15,26,15,34,0,2}, {15,34,9,32,0,2},
          {20,17,19,24,0,2}, {19,24,25,23,0,1}, {20,17,17,24,0,0}, {17,24,23,25,0,1}, {24,10,0,0,3,1} },
        { {22,12,15,23,0,0}, {15,23,10,32,0,0}, {10,32,5,42,0,0}, {15,23,24,23,0,2}, {24,23,22,32,0,2},
          {21,14,13,18,0,2}, {13,18,16,25,0,1}, {21,14,27,19,0,0}, {27,19,31,12,0,1}, {25,7,0,0,3,1} },
        { {23,10,16,21,0,0}, {16,21,8,26,0,0}, {8,26,1,23,0,0}, {16,21,26,22,0,2}, {26,22,29,31,0,2},
          {22,12,13,16,0,2}, {13,16,17,22,0,1}, {22,12,28,17,0,0}, {28,17,32,11,0,1}, {26,5,0,0,3,1} },
        { {22,11,15,22,0,0}, {15,22,9,29,0,0}, {9,29,4,24,0,0}, {15,22,23,28,0,2}, {23,28,28,37,0,2},
          {21,13,15,19,0,2}, {15,19,20,24,0,1}, {21,13,26,19,0,0}, {26,19,29,14,0,1}, {25,6,0,0,3,1} }
    };
    int sc = w / 32;
    int ox, oy, width, f, i;

    if (h / 42 < sc) sc = h / 42;
    if (sc < 1) sc = 1;
    ox = x + (w - 32 * sc) / 2;
    oy = y + (h - 42 * sc) / 2;
    width = sc + 3;
    if (width > 8) width = 8;
    f = (int)((ms / 60u) % 5u);

    for (i = 0; i < 10; i++) {
        const sm_svg_t *c = &run_svg[f][i];
        uint16_t col = (c->ci == 1u) ? api->col_text : api->col_accent;
        if (c->r > 0u) {
            sm_disc(api, ox + c->x0 * sc, oy + c->y0 * sc, (int)c->r * sc, col, a);
        } else {
            api->line_w((int16_t)(ox + c->x0 * sc), (int16_t)(oy + c->y0 * sc),
                        (int16_t)(ox + c->x1 * sc), (int16_t)(oy + c->y1 * sc),
                        (uint8_t)width, col, a);
        }
    }
}

static void draw_mail_icon(const pic_mod_api_t *api, int x, int y, int w, int h,
                           uint32_t ms, uint8_t a) {
    static const uint8_t lw_num[3] = {2, 4, 3};
    int t = (w < h ? w : h) / 16;
    int bt = h * 45 / 100;
    int by = y + bt;
    int bh = h - bt;
    int rad = bh / 5;
    int fw = (w < h ? w : h) / 14;
    int lw, lx, ty_open, fa, u, ay, cx = x + w / 2, i;
    uint32_t p = ms % 4000u;
    uint32_t tri = p < 2000u ? p : 4000u - p;

    if (t < 2) t = 2;
    if (rad < 2) rad = 2;
    if (fw < 2) fw = 2;
    lw = w - 6 * t;
    lx = x + (w - lw) / 2;
    ty_open = y + 3;

    if (tri <= 250u) {
        fa = 0;
        u = 0;
    } else if (tri <= 900u) {
        fa = api->ease_in_out((tri - 250u) * 256u / 650u);
        u = 0;
    } else if (tri <= 1650u) {
        fa = 255;
        u = api->ease_in_out((tri - 900u) * 256u / 750u);
    } else {
        fa = 255;
        u = 255;
    }
    ay = (by + bh * 58 / 100) - ((by + bh * 58 / 100) - (y + fw / 2)) * fa / 255;

    if (ay <= by + t) {
        sm_stroke_w(api, x + t, y + bt + t, cx, ay, fw, api->col_ring, a, 1);
        sm_stroke_w(api, cx, ay, x + w - t, y + bt + t, fw, api->col_ring, a, 1);
    }
    if (u > 0) {
        int ty = (by + t) - ((by + t) - ty_open) * u / 255;
        int vis_h = (by + t * 2) - ty;
        if (vis_h > t) {
            int pad = lw / 6;
            int step = (bt - 4) / 3;
            api->round((int16_t)lx, (int16_t)ty, (int16_t)lw, (int16_t)vis_h,
                       (int16_t)t, api->col_text, a);
            for (i = 0; i < 3; i++) {
                int ly = ty + 4 + i * step;
                if (ly + t <= by) {
                    api->fill((int16_t)(lx + pad), (int16_t)ly,
                              (int16_t)((lw - 2 * pad) * lw_num[i] / 4), (int16_t)t,
                              i == 0 ? api->col_ring : api->col_surface, a);
                }
            }
        }
    }
    api->round((int16_t)x, (int16_t)by, (int16_t)w, (int16_t)bh, (int16_t)rad, api->col_ring, a);
    api->round((int16_t)(x + t), (int16_t)(by + t), (int16_t)(w - 2 * t),
               (int16_t)(bh - 2 * t), (int16_t)(rad > t ? rad - t : 1), api->col_surface, a);
    {
        int cy_p = y + bt + bh * 46 / 100;
        int by_p = y + bt + bh - t - 1;
        sm_stroke_w(api, x + t + 1, by_p, cx, cy_p, fw, api->col_ring, a, 0);
        sm_stroke_w(api, cx, cy_p, x + w - t - 1, by_p, fw, api->col_ring, a, 0);
    }
    if (ay > by + t) {
        sm_stroke_w(api, x + t, y + bt + t, cx, ay, fw, api->col_ring, a, 1);
        sm_stroke_w(api, cx, ay, x + w - t, y + bt + t, fw, api->col_ring, a, 1);
    }
}

static void draw_gear_icon(const pic_mod_api_t *api, int cx, int cy, int r,
                           uint32_t ms, uint8_t a) {
    uint32_t ph = (ms % 4000u) * 4096u / 4000u;
    int tr = r / 4;
    int rc, rw, i;

    if (r < 4) return;
    if (tr < 2) tr = 2;
    rc = r - tr / 8;
    rw = r / 3;
    if (rw < 2) rw = 2;

    for (i = 0; i < 8; i++) {
        uint32_t ang = ph + (uint32_t)i * 512u;
        int16_t s = sm_trig(ang);
        int16_t c = sm_trig(ang + 1024u);
        sm_disc(api, cx + (rc * c / 127), cy + (rc * s / 127), tr, api->col_ring, a);
    }
    sm_disc(api, cx, cy, r, api->col_ring, a);
    sm_disc(api, cx, cy, r - rw, api->col_surface, a);
}

static void draw_music_icon(const pic_mod_api_t *api, int x, int y, int w, int h,
                            uint32_t ms, uint8_t a) {
    static const uint8_t eq_pct[5] = {18, 24, 30, 24, 18};
    uint32_t ph = (ms % 1600u) * 4096u / 1600u;
    uint32_t sway_ph = (ms % 3200u) * 4096u / 3200u;
    int m = w < h ? w : h;
    int cx = x + w / 2;
    int nr = m / 8;
    int fw = m / 13;
    int dx, dy1, dy2, x1, y1, x2, y2, sx1, sy1, sx2, sy2, eq_bot, bw, gap, i;

    if (nr < 3) nr = 3;
    if (fw < 2) fw = 2;
    dx = sm_trig(sway_ph) * (m / 22) / 127;
    dy1 = sm_trig(ph) * (m / 14) / 127;
    dy2 = sm_trig(ph + 896u) * (m / 14) / 127;

    bw = m / 14;
    if (bw < 2) bw = 2;
    for (i = 0; i < 4; i++) {
        int dir = (i & 2) ? 1 : -1;
        int outer = i & 1;
        int wv = sm_trig(ph + (uint32_t)i * 512u) + 127;
        int mh = h * (outer ? 13 : 20) / 100;
        int bh = bw * 2 + wv * mh / 254;
        int wx = cx + dir * (w * (outer ? 46 : 36) / 100) - bw / 2;
        api->round((int16_t)wx, (int16_t)(y + h * 34 / 100 - bh / 2),
                   (int16_t)bw, (int16_t)bh, (int16_t)(bw / 2),
                   outer ? api->col_text : api->col_ring, a);
    }

    x1 = cx - w * 16 / 100 + dx;
    y1 = y + h * 48 / 100 + dy1;
    x2 = cx + w * 14 / 100 + dx;
    y2 = y + h * 41 / 100 + dy2;
    sx1 = x1 + nr - fw / 2;
    sy1 = y1 - h * 34 / 100;
    sx2 = x2 + nr - fw / 2;
    sy2 = y2 - h * 34 / 100;

    api->fill((int16_t)(sx1 - fw / 2), (int16_t)sy1, (int16_t)fw, (int16_t)(y1 - sy1), api->col_ring, a);
    api->fill((int16_t)(sx2 - fw / 2), (int16_t)sy2, (int16_t)fw, (int16_t)(y2 - sy2), api->col_ring, a);
    sm_stroke_w(api, sx1, sy1, sx2, sy2, fw, api->col_ring, a, 1);
    sm_stroke_w(api, sx1, sy1 + fw * 2, sx2, sy2 + fw * 2, fw, api->col_ring, a, 1);

    api->round((int16_t)(x1 - nr), (int16_t)(y1 - nr * 4 / 5),
               (int16_t)(nr * 2 + 1), (int16_t)(nr * 2 - 1), (int16_t)nr, api->col_accent, a);
    api->round((int16_t)(x2 - nr), (int16_t)(y2 - nr * 4 / 5),
               (int16_t)(nr * 2 + 1), (int16_t)(nr * 2 - 1), (int16_t)nr, api->col_accent, a);

    eq_bot = y + h - 2;
    bw = m / 11;
    if (bw < 3) bw = 3;
    gap = m / 6;
    if (gap < bw + 2) gap = bw + 2;
    for (i = 0; i < 5; i++) {
        uint32_t bph = ph * (uint32_t)(i & 1 ? 2 : 1) + (uint32_t)i * 768u;
        int wv = sm_trig(bph) + 127;
        int max_h = h * eq_pct[i] / 100;
        int bh = bw + wv * (max_h - bw) / 254;
        uint16_t bc = (i == 2) ? api->col_accent : ((i & 1) ? api->col_ring : api->col_text);
        api->round((int16_t)(cx + (i - 2) * gap - bw / 2), (int16_t)(eq_bot - bh),
                   (int16_t)bw, (int16_t)bh, (int16_t)(bw / 2), bc, a);
    }
}

static void sys_mod_impl(const pic_mod_api_t *api, uint8_t mode, uint8_t a, int clear_bg) {
    int tw = (int)api->ui_w * 500 / 1000;
    int th = (int)api->ui_h * 400 / 1000;
    int tx, ty, w, h, x, y, rad;
    (void)mode;
    (void)clear_bg;

    if (!api || a == 0) return;
    if (tw < 48) tw = 48;
    if (th < 48) th = 48;
    tx = ((int)api->ui_w - tw) / 2;
    ty = (int)api->ui_h * 100 / 1000;
    w = tw * 3 / 4;
    h = th * 2 / 3;
    x = tx + (tw - w) / 2;
    y = ty + (th - h) / 2;
    rad = (w < h ? w : h) / 2;
    if (rad < 8) rad = 8;

    if (api->page_id == 1u) {
        draw_runner_icon(api, x, y, w, h, api->now_ms, a);
    } else if (api->page_id == 2u) {
        draw_mail_icon(api, x, y, w, h, api->now_ms, a);
    } else if (api->page_id == 3u) {
        draw_gear_icon(api, x + w / 2, y + h / 2, rad, api->now_ms, a);
    } else if (api->page_id == 4u) {
        draw_music_icon(api, x, y, w, h, api->now_ms, a);
    }
}

#if defined(__ARM_ARCH) || defined(__thumb__)
void sys_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    sys_mod_impl(api, mode, alpha, clear_bg);
}
#else
void sys_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    sys_mod_impl(api, mode, alpha, clear_bg);
}
#endif
