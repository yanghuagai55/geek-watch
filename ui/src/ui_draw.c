#include "ui_internal.h"

#include "font.h"
#include "hal.h"

/* 这一帧要画的东西先记成一张表，再切成高 8 行的带子送出去。
   表和带子都从 16KB 预算里切，不在栈上放整屏。 */

enum {
    KIND_FILL = 1,
    KIND_TEXT = 2,
    KIND_ROUND = 3,
    KIND_LINE = 4,
    KIND_SVG = 5,
    KIND_IMAGE = 6,
    KIND_MASK = 7,
    ITEM_MAX = 96,
    STR_MAX = 640,
    PIC_MAX = 16
};

typedef struct draw_item {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint16_t color;
    uint16_t str_off;
    uint8_t kind;
    uint8_t opacity;
    uint8_t scale;
    uint16_t tf_s;
    int16_t tf_ox;
    int16_t tf_oy;
} draw_item_t;

static draw_item_t *items;
static int item_n;
static char *strs;
static int str_n;
static uint16_t *band;
static int tf_s = 256;
static int tf_ox;
static int tf_oy;

static void stamp(draw_item_t *it) {
    it->tf_s = (uint16_t)(tf_s < 1 ? 1 : tf_s);
    it->tf_ox = (int16_t)tf_ox;
    it->tf_oy = (int16_t)tf_oy;
}

static void use_item_xform(const draw_item_t *it) {
    tf_s = it->tf_s < 1 ? 256 : it->tf_s;
    tf_ox = it->tf_ox;
    tf_oy = it->tf_oy;
}
static const void *pic_ref[PIC_MAX];
static int pic_n;
static volatile int output_preempt;
static int16_t gif_x, gif_y, gif_w, gif_h;
static const uint16_t *gif_px;

static int pic_add(const void *p) {
    if (!p || pic_n >= PIC_MAX) {
        return -1;
    }
    pic_ref[pic_n] = p;
    return pic_n++;
}

static uint32_t utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;

    if (*s == 0) {
        return 0;
    }
    if (*s < 0x80) {
        *ps += 1;
        return *s;
    }
    if ((*s & 0xE0) == 0xC0 && s[1]) {
        uint32_t cp = ((uint32_t)(*s & 0x1F) << 6) | (s[1] & 0x3F);
        *ps += 2;
        return cp;
    }
    if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
        uint32_t cp = ((uint32_t)(*s & 0x0F) << 12) |
                      ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *ps += 3;
        return cp;
    }
    if ((*s & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        uint32_t cp = ((uint32_t)(*s & 0x07) << 18) |
                      ((uint32_t)(s[1] & 0x3F) << 12) |
                      ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *ps += 4;
        return cp;
    }
    *ps += 1;
    return 0xFFFD;
}

static int glyph_on(const ui_glyph_t *g, int x, int y) {
    int stride;
    const uint8_t *row;

    if (!g || x < 0 || y < 0 || x >= g->w || y >= g->h) {
        return 0;
    }
    stride = (g->w + 7) >> 3;
    row = ui_font_bits() + g->offset + (uint32_t)y * (uint32_t)stride;
    return (row[x >> 3] >> (7 - (x & 7))) & 1;
}

void ui_draw_init(void) {
    items = ui_mem_alloc(sizeof(draw_item_t) * ITEM_MAX, 4);
    strs = ui_mem_alloc(STR_MAX, 1);
    band = ui_mem_alloc((uint32_t)UI_W * UI_BAND_H * sizeof(uint16_t), 2);
}

void ui_draw_begin(void) {
    item_n = 0;
    str_n = 0;
    pic_n = 0;
    tf_s = 256;
    tf_ox = 0;
    tf_oy = 0;
}

void ui_draw_xform(int scale256, int ox, int oy) {
    tf_s = scale256 < 1 ? 1 : scale256;
    tf_ox = ox;
    tf_oy = oy;
}

void ui_draw_fill(int16_t x, int16_t y, int16_t w, int16_t h,
                  uint16_t color, uint8_t opacity) {
    draw_item_t *it;

    if (!items || item_n >= ITEM_MAX || w <= 0 || h <= 0 || opacity == 0) {
        return;
    }
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->str_off = 0;
    it->kind = KIND_FILL;
    it->opacity = opacity;
    it->scale = 0;
    stamp(it);
}

void ui_draw_round(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius,
                   uint16_t color, uint8_t opacity) {
    draw_item_t *it;

    if (!items || item_n >= ITEM_MAX || w <= 0 || h <= 0 || opacity == 0) {
        return;
    }
    if (radius < 0) {
        radius = 0;
    }
    if (radius > w / 2) {
        radius = (int16_t)(w / 2);
    }
    if (radius > h / 2) {
        radius = (int16_t)(h / 2);
    }
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->str_off = 0;
    it->kind = KIND_ROUND;
    it->opacity = opacity;
    it->scale = (uint8_t)radius;
    stamp(it);
}

void ui_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  uint16_t color, uint8_t opacity) {
    draw_item_t *it;

    if (!items || item_n >= ITEM_MAX || opacity == 0) {
        return;
    }
    it = &items[item_n++];
    it->x = x0;
    it->y = y0;
    it->w = x1;
    it->h = y1;
    it->color = color;
    it->str_off = 0;
    it->kind = KIND_LINE;
    it->opacity = opacity;
    it->scale = 0;
    stamp(it);
}

void ui_draw_text(int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity, uint8_t scale) {
    draw_item_t *it;
    int len = 0;

    if (!items || !strs || !utf8 || item_n >= ITEM_MAX || opacity == 0) {
        return;
    }
    while (utf8[len]) {
        len++;
    }
    if (str_n + len + 1 > STR_MAX) {
        return;
    }
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = 0;
    it->h = 0;
    it->color = color;
    it->str_off = (uint16_t)str_n;
    it->kind = KIND_TEXT;
    it->opacity = opacity;
    it->scale = scale ? scale : 1;
    stamp(it);
    while (*utf8) {
        strs[str_n++] = *utf8++;
    }
    strs[str_n++] = 0;
}

void ui_svg_draw(int16_t x, int16_t y, uint8_t scale,
                 const ui_svg_cmd_t *cmd, int count,
                 uint16_t color, uint8_t opacity) {
    draw_item_t *it;
    int ref;

    if (!items || !cmd || count <= 0 || item_n >= ITEM_MAX || opacity == 0) {
        return;
    }
    if (count > 48) {
        count = 48;
    }
    ref = pic_add(cmd);
    if (ref < 0) {
        return;
    }
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = (int16_t)count;
    it->h = 0;
    it->color = color;
    it->str_off = (uint16_t)ref;
    it->kind = KIND_SVG;
    it->opacity = opacity;
    it->scale = scale ? scale : 1;
    stamp(it);
}

void ui_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint8_t scale, const uint16_t *rgb565, uint8_t opacity) {
    draw_item_t *it;
    int ref;

    if (!items || !rgb565 || w <= 0 || h <= 0 || item_n >= ITEM_MAX || opacity == 0) {
        return;
    }
    ref = pic_add(rgb565);
    if (ref < 0) {
        return;
    }
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = 0;
    it->str_off = (uint16_t)ref;
    it->kind = KIND_IMAGE;
    it->opacity = opacity;
    it->scale = scale ? scale : 1;
    stamp(it);
}

void ui_mask_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint8_t *cover4, uint16_t color, uint8_t opacity) {
    draw_item_t *it;
    int ref;

    if (!items || !cover4 || w <= 0 || h <= 0 || item_n >= ITEM_MAX || opacity == 0) {
        return;
    }
    ref = pic_add(cover4);
    if (ref < 0) {
        return;
    }
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->str_off = (uint16_t)ref;
    it->kind = KIND_MASK;
    it->opacity = opacity;
    it->scale = 1;
    stamp(it);
}

void ui_text_draw(int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity, uint8_t scale) {
    ui_draw_text(x, y, utf8, color, opacity, scale);
}

int16_t ui_text_width(const char *utf8, uint8_t scale) {
    int width = 0;
    const char *s = utf8;

    if (!s) {
        return 0;
    }
    if (scale == 0) {
        scale = 1;
    }
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const ui_glyph_t *g = ui_font_find(cp);
        int adv = g ? g->advance : 8;
        width += adv * scale;
    }
    return (int16_t)width;
}

static void plot(int x, int y, int band_y, uint16_t color, uint8_t opacity) {
    uint16_t *dst;
    uint16_t d;
    int inv;
    int dr, dg, db, sr, sg, sb;
    int r, g, b;

    if (tf_s != 256 || tf_ox != 0 || tf_oy != 0) {
        x = tf_ox + x * tf_s / 256;
        y = tf_oy + y * tf_s / 256;
    }
    if (x < 0 || x >= UI_W || y < band_y || y >= band_y + UI_BAND_H) {
        return;
    }
    dst = &band[(y - band_y) * UI_W + x];
    if (opacity == 255) {
        *dst = color;
        return;
    }
    d = *dst;
    inv = 256 - opacity;
    dr = (d >> 11) & 31;
    dg = (d >> 5) & 63;
    db = d & 31;
    sr = (color >> 11) & 31;
    sg = (color >> 5) & 63;
    sb = color & 31;
    r = (dr * inv + sr * opacity) >> 8;
    g = (dg * inv + sg * opacity) >> 8;
    b = (db * inv + sb * opacity) >> 8;
    *dst = (uint16_t)((r << 11) | (g << 5) | b);
}

static void raster_fill(const draw_item_t *it, int band_y) {
    int sx0 = it->x;
    int sy0 = it->y;
    int sx1 = sx0 + it->w;
    int sy1 = sy0 + it->h;
    int x0;
    int y0;
    int x1;
    int y1;
    int x;
    int y;

    use_item_xform(it);
    if (tf_s == 256 && tf_ox == 0 && tf_oy == 0) {
        x0 = sx0;
        y0 = sy0;
        x1 = sx1;
        y1 = sy1;
    } else if (it->opacity == 255 && tf_s > 0) {
        x0 = tf_ox + sx0 * tf_s / 256;
        y0 = tf_oy + sy0 * tf_s / 256;
        x1 = tf_ox + (sx1 - 1) * tf_s / 256 + 1;
        y1 = tf_oy + (sy1 - 1) * tf_s / 256 + 1;
    } else {
        int ys = (band_y - tf_oy) * 256 / tf_s - 1;
        int ye = (band_y + UI_BAND_H - tf_oy) * 256 / tf_s + 2;
        if (sy0 < ys) {
            sy0 = ys;
        }
        if (sy1 > ye) {
            sy1 = ye;
        }
        for (y = sy0; y < sy1; y++) {
            for (x = sx0; x < sx1; x++) {
                plot(x, y, band_y, it->color, it->opacity);
            }
        }
        return;
    }
    if (y0 < band_y) {
        y0 = band_y;
    }
    if (y1 > band_y + UI_BAND_H) {
        y1 = band_y + UI_BAND_H;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > UI_W) {
        x1 = UI_W;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    if (it->opacity == 255) {
        for (y = y0; y < y1; y++) {
            uint16_t *row = &band[(y - band_y) * UI_W];
            for (x = x0; x < x1; x++) {
                row[x] = it->color;
            }
        }
        return;
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            plot(x, y, band_y, it->color, it->opacity);
        }
    }
}

static int in_round(int x, int y, const draw_item_t *it) {
    int rad = it->scale;
    int x0 = it->x;
    int y0 = it->y;
    int x1 = x0 + it->w;
    int y1 = y0 + it->h;
    int cx;
    int cy;
    int dx;
    int dy;

    if (x < x0 || y < y0 || x >= x1 || y >= y1) {
        return 0;
    }
    if (rad <= 0) {
        return 1;
    }
    if (x >= x0 + rad && x < x1 - rad) {
        return 1;
    }
    if (y >= y0 + rad && y < y1 - rad) {
        return 1;
    }
    cx = (x < x0 + rad) ? (x0 + rad) : (x1 - rad);
    cy = (y < y0 + rad) ? (y0 + rad) : (y1 - rad);
    dx = x - cx;
    dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

static void raster_round(const draw_item_t *it, int band_y) {
    use_item_xform(it);
    int x0 = it->x;
    int y0 = it->y;
    int x1 = x0 + it->w;
    int y1 = y0 + it->h;
    int x, y;

    if (x0 < 0) {
        x0 = 0;
    }
    if (tf_s == 256 && tf_ox == 0 && tf_oy == 0) {
        if (y0 < band_y) {
            y0 = band_y;
        }
        if (y1 > band_y + UI_BAND_H) {
            y1 = band_y + UI_BAND_H;
        }
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > UI_W) {
            x1 = UI_W;
        }
    } else {
        int ys = (band_y - tf_oy) * 256 / tf_s - 1;
        int ye = (band_y + UI_BAND_H - tf_oy) * 256 / tf_s + 2;
        if (y0 < ys) {
            y0 = ys;
        }
        if (y1 > ye) {
            y1 = ye;
        }
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (in_round(x, y, it)) {
                plot(x, y, band_y, it->color, it->opacity);
            }
        }
    }
}

static void stroke(int x0, int y0, int x1, int y1, int band_y,
                   uint16_t color, uint8_t opacity) {
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        plot(x0, y0, band_y, color, opacity);
        plot(x0 + 1, y0, band_y, color, opacity);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        {
            int e2 = err * 2;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

static void fill_disc(int cx, int cy, int r, int band_y,
                      uint16_t color, uint8_t opacity) {
    int x0 = cx - r;
    int y0 = cy - r;
    int x1 = cx + r;
    int y1 = cy + r;
    int x;
    int y;

    if (r < 1) {
        return;
    }
    if (tf_s == 256 && tf_ox == 0 && tf_oy == 0) {
        if (y0 < band_y) {
            y0 = band_y;
        }
        if (y1 > band_y + UI_BAND_H) {
            y1 = band_y + UI_BAND_H;
        }
    } else {
        int ys = (band_y - tf_oy) * 256 / tf_s - 1;
        int ye = (band_y + UI_BAND_H - tf_oy) * 256 / tf_s + 2;
        if (y0 < ys) {
            y0 = ys;
        }
        if (y1 > ye) {
            y1 = ye;
        }
    }
    for (y = y0; y < y1; y++) {
        int dy = y - cy;
        for (x = x0; x < x1; x++) {
            int dx = x - cx;
            if (dx * dx + dy * dy <= r * r) {
                plot(x, y, band_y, color, opacity);
            }
        }
    }
}

static void raster_line(const draw_item_t *it, int band_y) {
    use_item_xform(it);
    stroke(it->x, it->y, it->w, it->h, band_y, it->color, it->opacity);
}

static void raster_svg(const draw_item_t *it, int band_y) {
    const ui_svg_cmd_t *cmd;
    int scale = it->scale ? it->scale : 1;
    int n = it->w;
    int i;

    if (it->str_off >= PIC_MAX) {
        return;
    }
    cmd = (const ui_svg_cmd_t *)pic_ref[it->str_off];
    if (!cmd) {
        return;
    }
    use_item_xform(it);
    for (i = 0; i < n; i++) {
        if (cmd[i].r > 0) {
            fill_disc(it->x + cmd[i].x0 * scale, it->y + cmd[i].y0 * scale,
                      cmd[i].r * scale, band_y, it->color, it->opacity);
        } else {
            stroke(it->x + cmd[i].x0 * scale, it->y + cmd[i].y0 * scale,
                   it->x + cmd[i].x1 * scale, it->y + cmd[i].y1 * scale,
                   band_y, it->color, it->opacity);
        }
    }
}

static void raster_image(const draw_item_t *it, int band_y) {
    const uint16_t *px;
    int scale = it->scale ? it->scale : 1;
    int sw = it->w;
    int sh = it->h;
    int y0 = it->y;
    int y1 = it->y + sh * scale;
    int x;
    int y;

    if (it->str_off >= PIC_MAX) {
        return;
    }
    px = (const uint16_t *)pic_ref[it->str_off];
    if (!px) {
        return;
    }
    use_item_xform(it);
    if (tf_s == 256 && tf_ox == 0 && tf_oy == 0) {
        if (y0 < band_y) {
            y0 = band_y;
        }
        if (y1 > band_y + UI_BAND_H) {
            y1 = band_y + UI_BAND_H;
        }
    } else {
        int ys = (band_y - tf_oy) * 256 / tf_s - 1;
        int ye = (band_y + UI_BAND_H - tf_oy) * 256 / tf_s + 2;
        if (y0 < ys) {
            y0 = ys;
        }
        if (y1 > ye) {
            y1 = ye;
        }
    }
    for (y = y0; y < y1; y++) {
        int sy = (y - it->y) / scale;
        if (sy < 0 || sy >= sh) {
            continue;
        }
        for (x = 0; x < sw * scale; x++) {
            int sx = x / scale;
            plot(it->x + x, y, band_y, px[sy * sw + sx], it->opacity);
        }
    }
}

static int mask_nibble(const uint8_t *cover, int i) {
    int byte = cover[i >> 1];
    return (i & 1) ? (byte & 15) : (byte >> 4);
}

static void raster_mask(const draw_item_t *it, int band_y) {
    const uint8_t *cover;
    int sw = it->w;
    int sh = it->h;
    int y0 = it->y;
    int y1 = it->y + sh;
    int x;
    int y;

    if (it->str_off >= PIC_MAX) {
        return;
    }
    cover = (const uint8_t *)pic_ref[it->str_off];
    if (!cover) {
        return;
    }
    use_item_xform(it);
    if (tf_s == 256 && tf_ox == 0 && tf_oy == 0) {
        if (y0 < band_y) {
            y0 = band_y;
        }
        if (y1 > band_y + UI_BAND_H) {
            y1 = band_y + UI_BAND_H;
        }
    } else {
        int ys = (band_y - tf_oy) * 256 / tf_s - 1;
        int ye = (band_y + UI_BAND_H - tf_oy) * 256 / tf_s + 2;
        if (y0 < ys) {
            y0 = ys;
        }
        if (y1 > ye) {
            y1 = ye;
        }
    }
    for (y = y0; y < y1; y++) {
        int sy = y - it->y;
        if (sy < 0 || sy >= sh) {
            continue;
        }
        for (x = 0; x < sw; x++) {
            int cov = mask_nibble(cover, sy * sw + x);
            int op;
            if (cov == 0) {
                continue;
            }
            op = (int)it->opacity * cov / 15;
            if (op > 0) {
                plot(it->x + x, y, band_y, it->color, (uint8_t)op);
            }
        }
    }
}

static void raster_text(const draw_item_t *it, int band_y) {
    use_item_xform(it);
    const char *s = strs + it->str_off;
    int pen = it->x;
    int scale = it->scale ? it->scale : 1;

    while (*s) {
        uint32_t cp = utf8_next(&s);
        const ui_glyph_t *g = ui_font_find(cp);
        int gw = g ? g->w : 8;
        int gh = g ? g->h : 16;
        int adv = g ? g->advance : 8;
        int gx, gy, sx, sy;

        for (gy = 0; gy < gh; gy++) {
            for (gx = 0; gx < gw; gx++) {
                int ink = g ? glyph_on(g, gx, gy)
                            : (gx == 0 || gy == 0 || gx == gw - 1 || gy == gh - 1);
                if (!ink) {
                    continue;
                }
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        plot(pen + gx * scale + sx,
                             it->y + gy * scale + sy,
                             band_y, it->color, it->opacity);
                    }
                }
            }
        }
        pen += adv * scale;
    }
}

void ui_output_preempt(void) {
    output_preempt = 1;
}

void ui_gif_post(int16_t x, int16_t y, int16_t w, int16_t h,
                 const uint16_t *rgb565) {
    gif_x = x;
    gif_y = y;
    gif_w = w;
    gif_h = h;
    gif_px = rgb565;
    output_preempt = 1;
}

static void flush_gif(void) {
    if (!gif_px || gif_w <= 0 || gif_h <= 0) {
        output_preempt = 0;
        return;
    }
    while (hal_display_busy()) {
    }
    hal_display_flush(gif_x, gif_y, gif_w, gif_h, gif_px);
    gif_px = 0;
    gif_w = 0;
    output_preempt = 0;
}

static void put_u(char **p, unsigned v) {
    char tmp[8];
    int n = 0;

    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v && n < 8) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n) {
        *(*p)++ = tmp[--n];
    }
}

void ui_draw_flush(void) {
    int y, n, i;
    static uint32_t acc_d;
    static uint32_t acc_s;
    static uint32_t acc_n;
    static uint32_t acc_t;
    uint32_t draw_ms = 0;
    uint32_t spi_ms = 0;
    uint32_t t0;
    uint32_t t1;

    if (!band || !items) {
        flush_gif();
        return;
    }
    if (acc_t == 0) {
        acc_t = hal_tick_ms();
    }
    link_snap_begin(UI_W, UI_H);
    for (y = 0; y < UI_H; y += UI_BAND_H) {
        if (output_preempt) {
            break;
        }
        t0 = hal_tick_ms();
        for (i = 0; i < UI_W * UI_BAND_H; i++) {
            band[i] = 0;
        }
        for (n = 0; n < item_n; n++) {
            if (items[n].kind == KIND_FILL) {
                raster_fill(&items[n], y);
            } else if (items[n].kind == KIND_ROUND) {
                raster_round(&items[n], y);
            } else if (items[n].kind == KIND_LINE) {
                raster_line(&items[n], y);
            } else if (items[n].kind == KIND_TEXT) {
                raster_text(&items[n], y);
            } else if (items[n].kind == KIND_SVG) {
                raster_svg(&items[n], y);
            } else if (items[n].kind == KIND_IMAGE) {
                raster_image(&items[n], y);
            } else if (items[n].kind == KIND_MASK) {
                raster_mask(&items[n], y);
            }
        }
        t1 = hal_tick_ms();
        draw_ms += t1 - t0;
        while (hal_display_busy()) {
        }
        hal_display_flush(0, (int16_t)y, UI_W, UI_BAND_H, band);
        spi_ms += hal_tick_ms() - t1;
        link_snap_pixels(band, UI_W * UI_BAND_H);
    }
    link_snap_end();
    flush_gif();
    acc_d += draw_ms;
    acc_s += spi_ms;
    acc_n++;
    if (hal_tick_ms() - acc_t >= 1000u && acc_n) {
        char msg[32];
        char *p = msg;
        *p++ = 'd';
        *p++ = ' ';
        put_u(&p, acc_d / acc_n);
        *p++ = ' ';
        *p++ = 's';
        *p++ = ' ';
        put_u(&p, acc_s / acc_n);
        *p++ = ' ';
        *p++ = 'n';
        *p++ = ' ';
        put_u(&p, acc_n);
        *p++ = '\r';
        *p++ = '\n';
        hal_link_write((const uint8_t *)msg, (int)(p - msg));
        acc_d = 0;
        acc_s = 0;
        acc_n = 0;
        acc_t = hal_tick_ms();
    }
}
