#include "ui_icons.h"

/* sin(k * 22.5deg) * 127; cos reads the same table 4 entries ahead. */
static const int8_t gear_sin[16] = {
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49
};

/* pos is an 8.8 fixed-point table index; linear interp keeps rotation smooth. */
static int16_t gear_tab(uint32_t pos)
{
    uint32_t i = (pos >> 8) & 15u;
    int32_t f = (int32_t)(pos & 255u);
    int32_t a = gear_sin[i];
    int32_t b = gear_sin[(i + 1u) & 15u];
    return (int16_t)((a * (256 - f) + b * f) >> 8);
}

/* Envelope, physical 4-layer pocket pop-out (4.0s ping-pong with end holds):
   stage 1: top flap hinges open (folds from front down-pointing to back up-pointing);
   stage 2: letter slides upward out of the envelope mouth (in front of open flap,
            occluded below the envelope mouth by the front body);
   then pauses at peak and reverses smoothly. */
void ui_icon_mail(int16_t x, int16_t y, int16_t w, int16_t h,
                  uint32_t now_ms, uint16_t color, uint16_t accent,
                  uint16_t bg, uint8_t opacity)
{
    /* svg cmds are kept by reference until flush, so each call uses its own slice. */
    static ui_svg_cmd_t flap[4];
    uint32_t p, tri;
    int fa, u;
    int t, bt, by, bh, rad, lw, lx, ty, ty_open, ay, fw, i;

    if (w < 8 || h < 8) {
        return;
    }
    t = (w < h ? w : h) / 16;
    if (t < 2) {
        t = 2;
    }
    bt = h * 45 / 100;
    by = y + bt;
    bh = h - bt;
    rad = bh / 5;
    if (rad < 2) {
        rad = 2;
    }
    fw = (w < h ? w : h) / 14;
    if (fw < 2) {
        fw = 2;
    }
    lw = w - 6 * t;
    lx = x + (w - lw) / 2;
    ty_open = y + 3;

    p = now_ms % 4000u;
    tri = p < 2000u ? p : 4000u - p;
    if (tri <= 250u) {
        fa = 0;
        u = 0;
    } else if (tri <= 900u) {
        fa = ui_ease_in_out((tri - 250u) * 256u / 650u);
        u = 0;
    } else if (tri <= 1650u) {
        fa = 255;
        u = ui_ease_in_out((tri - 900u) * 256u / 750u);
    } else {
        fa = 255;
        u = 255;
    }

    ay = (by + bh * 58 / 100) - ((by + bh * 58 / 100) - (y + fw / 2)) * fa / 255;
    flap[0].x0 = (int8_t)(t - w / 2);
    flap[0].y0 = (int8_t)(bt + t);
    flap[0].x1 = 0;
    flap[0].y1 = (int8_t)(ay - y);
    flap[0].r = 0;
    flap[0].ci = 0;
    flap[1].x0 = 0;
    flap[1].y0 = (int8_t)(ay - y);
    flap[1].x1 = (int8_t)(w / 2 - t);
    flap[1].y1 = (int8_t)(bt + t);
    flap[1].r = 0;
    flap[1].ci = 0;

    /* 1. Back layer: opened top flap (when folded above top rim) */
    if (ay <= by + t) {
        ui_svg_draw_w((int16_t)(x + w / 2), y, 1, flap, 2, color, opacity, (uint8_t)fw);
    }

    /* 2. Middle layer: letter rising upward out of the envelope mouth */
    if (u > 0) {
        static const uint8_t lw_num[3] = {2, 4, 3};
        int vis_h;
        ty = (by + t) - ((by + t) - ty_open) * u / 255;
        vis_h = (by + t * 2) - ty;
        if (vis_h > t) {
            int pad = lw / 6;
            int step = (bt - 4) / 3;
            ui_draw_round((int16_t)lx, (int16_t)ty, (int16_t)lw, (int16_t)vis_h,
                          (int16_t)t, accent, opacity);
            for (i = 0; i < 3; i++) {
                int ly = ty + 4 + i * step;
                if (ly + t <= by) {
                    ui_draw_fill((int16_t)(lx + pad), (int16_t)ly,
                                 (int16_t)((lw - 2 * pad) * lw_num[i] / 4), (int16_t)t,
                                 i == 0 ? color : bg, opacity);
                }
            }
        }
    }

    /* 3. Front layer: envelope pocket body (naturally occludes the inside of the pocket) */
    ui_draw_round(x, (int16_t)by, w, (int16_t)bh, (int16_t)rad, color, opacity);
    ui_draw_round((int16_t)(x + t), (int16_t)(by + t), (int16_t)(w - 2 * t),
                  (int16_t)(bh - 2 * t), (int16_t)(rad > t ? rad - t : 1), bg, opacity);

    /* 4. Permanent bottom pocket flap crease on the envelope body */
    flap[2].x0 = (int8_t)(t + 1 - w / 2);
    flap[2].y0 = (int8_t)(bt + bh - t - 1);
    flap[2].x1 = 0;
    flap[2].y1 = (int8_t)(bt + bh * 46 / 100);
    flap[2].r = 0;
    flap[2].ci = 0;
    flap[3].x0 = 0;
    flap[3].y0 = (int8_t)(bt + bh * 46 / 100);
    flap[3].x1 = (int8_t)(w / 2 - t - 1);
    flap[3].y1 = (int8_t)(bt + bh - t - 1);
    flap[3].r = 0;
    flap[3].ci = 0;
    ui_svg_draw_w((int16_t)(x + w / 2), y, 1, flap + 2, 2, color, opacity, (uint8_t)fw);

    /* 5. Top-most layer when top flap is closed/closing (folded down over the front) */
    if (ay > by + t) {
        ui_svg_draw_w((int16_t)(x + w / 2), y, 1, flap, 2, color, opacity, (uint8_t)fw);
    }
}

/* Ring + 8 long rounded teeth, one revolution per 4s. */
void ui_icon_gear(int16_t cx, int16_t cy, int16_t r,
                  uint32_t now_ms, uint16_t color, uint16_t bg, uint8_t opacity)
{
    static ui_svg_cmd_t cmd[11]; /* static: referenced until flush */
    uint32_t ph;
    int tr, rc, rw, i;

    if (r < 4) {
        return;
    }
    if (r > 127) {
        r = 127; /* svg coords and radii are int8 */
    }
    tr = r / 4;
    if (tr < 2) {
        tr = 2;
    }
    rc = r - tr / 8;
    rw = r / 3;
    if (rw < 2) {
        rw = 2;
    }

    ph = (now_ms % 4000u) * 4096u / 4000u;
    for (i = 0; i < 8; i++) {
        uint32_t a = ph + (uint32_t)i * 512u;
        int16_t s = gear_tab(a);
        int16_t c = gear_tab(a + 1024u);
        cmd[i].x0 = (int8_t)(rc * c / 127);
        cmd[i].y0 = (int8_t)(rc * s / 127);
        cmd[i].x1 = 0;
        cmd[i].y1 = 0;
        cmd[i].r = (int8_t)tr;
        cmd[i].ci = 0;
    }
    cmd[8].x0 = 0;
    cmd[8].y0 = 0;
    cmd[8].x1 = 0;
    cmd[8].y1 = 0;
    cmd[8].r = (int8_t)r;
    cmd[8].ci = 0;
    ui_svg_draw_w(cx, cy, 1, cmd, 9, color, opacity, 0);

    cmd[9].x0 = 0;
    cmd[9].y0 = 0;
    cmd[9].x1 = 0;
    cmd[9].y1 = 0;
    cmd[9].r = (int8_t)(r - rw);
    cmd[9].ci = 0;
    cmd[10] = cmd[9];
    cmd[10].r = (int8_t)(r / 3);
    ui_svg_draw_w(cx, cy, 1, cmd + 9, 2, bg, opacity, 0);
}

/* Dancing beamed double eighth-note (♫) + acoustic side waves + 5-bar rhythm EQ. */
void ui_icon_music(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint32_t now_ms, uint16_t color, uint16_t accent,
                   uint16_t text_col, uint8_t opacity)
{
    static ui_svg_cmd_t beam[4];
    uint32_t ph = (now_ms % 1600u) * 4096u / 1600u;
    uint32_t sway_ph = (now_ms % 3200u) * 4096u / 3200u;
    int m = w < h ? w : h;
    int cx = x + w / 2;
    int nr = m / 8;
    int fw = m / 13;
    int dx = gear_tab(sway_ph) * (m / 22) / 127;
    int dy1 = gear_tab(ph) * (m / 14) / 127;
    int dy2 = gear_tab(ph + 896u) * (m / 14) / 127;
    int x1, y1, x2, y2, sx1, sy1, sx2, sy2, eq_bot, bw, gap, i;

    if (nr < 3) nr = 3;
    if (fw < 2) fw = 2;

    /* 1. Acoustic side wave bars pulsing outward on left & right */
    bw = m / 14;
    if (bw < 2) bw = 2;
    for (i = 0; i < 4; i++) {
        int dir = (i & 2) ? 1 : -1;
        int outer = i & 1;
        int wv = gear_tab(ph + (uint32_t)i * 512u) + 127;
        int mh = h * (outer ? 13 : 20) / 100;
        int bh = bw * 2 + wv * mh / 254;
        int wx = cx + dir * (w * (outer ? 46 : 36) / 100) - bw / 2;
        ui_draw_round((int16_t)wx, (int16_t)(y + h * 34 / 100 - bh / 2),
                      (int16_t)bw, (int16_t)bh, (int16_t)(bw / 2),
                      outer ? text_col : color, opacity);
    }

    /* 2. Centerpiece dancing beamed double eighth-note (♫) */
    x1 = cx - w * 16 / 100 + dx;
    y1 = y + h * 48 / 100 + dy1;
    x2 = cx + w * 14 / 100 + dx;
    y2 = y + h * 41 / 100 + dy2;
    sx1 = x1 + nr - fw / 2;
    sy1 = y1 - h * 34 / 100;
    sx2 = x2 + nr - fw / 2;
    sy2 = y2 - h * 34 / 100;

    /* Left stem, right stem, and double slanted top beams */
    beam[0].x0 = (int8_t)(sx1 - cx);
    beam[0].y0 = (int8_t)(y1 - y);
    beam[0].x1 = (int8_t)(sx1 - cx);
    beam[0].y1 = (int8_t)(sy1 - y);
    beam[1].x0 = (int8_t)(sx2 - cx);
    beam[1].y0 = (int8_t)(y2 - y);
    beam[1].x1 = (int8_t)(sx2 - cx);
    beam[1].y1 = (int8_t)(sy2 - y);
    beam[2].x0 = (int8_t)(sx1 - cx);
    beam[2].y0 = (int8_t)(sy1 - y);
    beam[2].x1 = (int8_t)(sx2 - cx);
    beam[2].y1 = (int8_t)(sy2 - y);
    beam[3].x0 = (int8_t)(sx1 - cx);
    beam[3].y0 = (int8_t)(sy1 + fw * 2 - y);
    beam[3].x1 = (int8_t)(sx2 - cx);
    beam[3].y1 = (int8_t)(sy2 + fw * 2 - y);
    ui_svg_draw_w((int16_t)cx, y, 1, beam, 4, color, opacity, (uint8_t)fw);

    /* Plump note heads */
    ui_draw_round((int16_t)(x1 - nr), (int16_t)(y1 - nr * 4 / 5),
                  (int16_t)(nr * 2 + 1), (int16_t)(nr * 2 - 1), (int16_t)nr, accent, opacity);
    ui_draw_round((int16_t)(x2 - nr), (int16_t)(y2 - nr * 4 / 5),
                  (int16_t)(nr * 2 + 1), (int16_t)(nr * 2 - 1), (int16_t)nr, accent, opacity);

    /* 3. Bottom 5-bar live audio equalizer growing from baseline */
    {
        static const uint8_t eq_pct[5] = {18, 24, 30, 24, 18};
        eq_bot = y + h - 2;
        bw = m / 11;
        if (bw < 3) bw = 3;
        gap = m / 6;
        if (gap < bw + 2) gap = bw + 2;
        for (i = 0; i < 5; i++) {
            uint32_t bph = ph * (uint32_t)(i & 1 ? 2 : 1) + (uint32_t)i * 768u;
            int wv = gear_tab(bph) + 127;
            int max_h = h * eq_pct[i] / 100;
            int bh = bw + wv * (max_h - bw) / 254;
            uint16_t bc = (i == 2) ? accent : ((i & 1) ? color : text_col);
            ui_draw_round((int16_t)(cx + (i - 2) * gap - bw / 2), (int16_t)(eq_bot - bh),
                          (int16_t)bw, (int16_t)bh, (int16_t)(bw / 2), bc, opacity);
        }
    }
}
