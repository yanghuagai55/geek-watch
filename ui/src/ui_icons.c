#include "ui_icons.h"

/* Envelope, slow two-stage ping-pong (~2.2s each way):
   stage 1 flap opens; stage 2 letter pops up, then moves to the top layer and
   lands in front of the body; then everything reverses. Rounded corners. */
static void mail_letter(int16_t lx, int ty, int lw, int lh, int t,
                        uint16_t accent, uint16_t bg, uint8_t opacity) {
    int i;
    ui_draw_round(lx, (int16_t)ty, (int16_t)lw, (int16_t)lh, (int16_t)t, accent, opacity);
    for (i = 0; i < 2; i++) {
        int ly = ty + lh / 5 + i * (lh / 4);
        int lt = lh / 12;
        int pad = lw / 6;
        int lwi = (lw - 2 * pad) * (i == 0 ? 3 : 2) / 3;
        if (lt < 1) {
            lt = 1;
        }
        ui_draw_fill((int16_t)(lx + pad), (int16_t)ly, (int16_t)lwi, (int16_t)lt, bg, opacity);
    }
}

void ui_icon_mail(int16_t x, int16_t y, int16_t w, int16_t h,
                  uint32_t now_ms, uint16_t color, uint16_t accent,
                  uint16_t bg, uint8_t opacity)
{
    /* svg cmds are kept by reference until flush, so they must be static. */
    static ui_svg_cmd_t flap[2];
    uint32_t p, tri;
    int e, fa, u, front;
    int t, bt, by, bh, rad, lw, lh, lx, ty, ay, fw;

    if (w < 8 || h < 8) {
        return;
    }
    t = (w < h ? w : h) / 16;
    if (t < 1) {
        t = 1;
    }
    bt = h * 3 / 10;
    by = y + bt;
    bh = h - bt;
    rad = (w < h ? w : h) / 7;
    if (rad < 2) {
        rad = 2;
    }
    lw = w * 3 / 4;
    lh = bh - 2 * t;
    lx = x + (w - lw) / 2;

    p = now_ms % 4400u;
    tri = p < 2200u ? p : 4400u - p;
    e = ui_ease_in_out(tri * 256u / 2200u);
    fa = e > 102 ? 255 : e * 255 / 102;          /* stage 1: flap */
    u = e <= 102 ? 0 : (e - 102) * 255 / 153;    /* stage 2: letter */
    front = u >= 128;

    if (u < 128) {
        ty = by + t - (by + t - (y - lh * 2 / 5)) * (u * 2) / 255;
    } else {
        ty = (y - lh * 2 / 5) + ((by + bh / 2 - lh / 2) - (y - lh * 2 / 5)) * ((u - 128) * 2) / 255;
    }

    ui_draw_round(x, (int16_t)by, w, (int16_t)bh, (int16_t)rad, color, opacity);
    ui_draw_round((int16_t)(x + t), (int16_t)(by + t), (int16_t)(w - 2 * t),
                  (int16_t)(bh - 2 * t), (int16_t)(rad - t), bg, opacity);

    ay = by + bh * 3 / 5;
    ay -= (ay - (y - bt / 2)) * fa / 255;
    fw = (w < h ? w : h) / 12;
    if (fw < 2) {
        fw = 2;
    }
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
    ui_svg_draw_w((int16_t)(x + w / 2), y, 1, flap, 2, color, opacity, (uint8_t)fw);

    /* Letter last (top layer) so the opened flap never covers it; hidden while
       still fully tucked inside the body. */
    if (front || ty < by) {
        mail_letter((int16_t)lx, ty, lw, lh, t, accent, bg, opacity);
    }
}

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