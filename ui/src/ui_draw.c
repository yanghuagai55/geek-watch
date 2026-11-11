#include "ui_internal.h"

#include "font.h"
#include "hal.h"
#include <string.h>

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
typedef struct bounds {
    int16_t x0, y0, x1, y1;
} bounds_t;

typedef struct history {
    uint32_t signature;
    bounds_t box;
} history_t;

typedef struct dirty_band {
    int16_t x0, x1;
} dirty_band_t;

enum { BAND_N = (UI_H + UI_BAND_H - 1) / UI_BAND_H };
static uint16_t *buffers[2];
static uint16_t *band;
static history_t *history;
static dirty_band_t *dirty;
static int history_n;
static int invalidated;
static int clip_x0, clip_x1, clip_y1;
static ui_render_stats_t stats;
static uint16_t dropped;
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

static int item_space(void) {
    if (item_n < ITEM_MAX) return 1;
    dropped++;
    return 0;
}

static int pic_add(const void *p) {
    if (!p || pic_n >= PIC_MAX) {
        dropped++;
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

void ui_draw_init(void) {
    items = ui_mem_alloc(sizeof(draw_item_t) * ITEM_MAX, 4);
    strs = ui_mem_alloc(STR_MAX, 1);
    buffers[0] = ui_mem_alloc((uint32_t)UI_W * UI_BAND_H * 2u, 4);
    buffers[1] = ui_mem_alloc((uint32_t)UI_W * UI_BAND_H * 2u, 4);
    history = ui_mem_alloc(sizeof(history_t) * ITEM_MAX, 4);
    dirty = ui_mem_alloc(sizeof(dirty_band_t) * BAND_N, 2);
    history_n = 0;
    invalidated = 1;
    output_preempt = 0;
    gif_px = 0;
    gif_w = gif_h = 0;
    memset(&stats, 0, sizeof stats);
}

void ui_draw_invalidate(void) {
    invalidated = 1;
}

void ui_render_get_stats(ui_render_stats_t *out) {
    if (out) {
        *out = stats;
    }
}

void ui_draw_begin(void) {
    item_n = 0;
    str_n = 0;
    pic_n = 0;
    dropped = 0;
    tf_s = 256;
    tf_ox = 0;
    tf_oy = 0;
}

void ui_draw_xform(int scale256, int ox, int oy) {
    tf_s = scale256 < 1 ? 1 : scale256 > 4096 ? 4096 : scale256;
    tf_ox = ox;
    tf_oy = oy;
}

void ui_draw_fill(int16_t x, int16_t y, int16_t w, int16_t h,
                  uint16_t color, uint8_t opacity) {
    draw_item_t *it;

    if (!items || !item_space() || w <= 0 || h <= 0 || opacity == 0) {
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

    if (!items || !item_space() || w <= 0 || h <= 0 || opacity == 0) {
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

    if (!items || !item_space() || opacity == 0) {
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

    if (!items || !strs || !utf8 || !item_space() || opacity == 0) {
        return;
    }
    while (utf8[len]) {
        len++;
    }
    if (str_n + len + 1 > STR_MAX) {
        dropped++;
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

    if (!items || !cmd || count <= 0 || !item_space() || opacity == 0) {
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

    if (!items || !rgb565 || w <= 0 || h <= 0 || !item_space() || opacity == 0) {
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

    if (!items || !cover4 || w <= 0 || h <= 0 || !item_space() || opacity == 0) {
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

static void pixel(int x, int y, int band_y, uint16_t color, uint8_t opacity) {
    uint16_t *dst;
    uint16_t d;
    int inv;
    int dr, dg, db, sr, sg, sb;
    int r, g, b;

    if (x < clip_x0 || x >= clip_x1 || y < band_y || y >= clip_y1 || !opacity) {
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

static void span(int x0, int x1, int y, int band_y, uint16_t color, uint8_t a) {
    uint16_t *p;
    int n;
    unsigned inv = 256u - a;
    unsigned r = ((color >> 11) & 31u) * a;
    unsigned g = ((color >> 5) & 63u) * a;
    unsigned b = (color & 31u) * a;
    if (x0 < clip_x0) x0 = clip_x0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y < band_y || y >= clip_y1 || x0 >= x1 || !a) return;
    p = band + (y - band_y) * UI_W + x0;
    n = x1 - x0;
    if (a == 255) {
        while (n--) *p++ = color;
    } else {
        while (n--) {
            unsigned d = *p;
            *p++ = (uint16_t)((((((d >> 11) & 31u) * inv + r) >> 8) << 11) |
                              (((((d >> 5) & 63u) * inv + g) >> 8) << 5) |
                              (((d & 31u) * inv + b) >> 8));
        }
    }
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
    int y;

    use_item_xform(it);
    if (tf_s == 256 && tf_ox == 0 && tf_oy == 0) {
        x0 = sx0;
        y0 = sy0;
        x1 = sx1;
        y1 = sy1;
    } else {
        x0 = tf_ox + sx0 * tf_s / 256;
        y0 = tf_oy + sy0 * tf_s / 256;
        x1 = tf_ox + (sx1 - 1) * tf_s / 256 + 1;
        y1 = tf_oy + (sy1 - 1) * tf_s / 256 + 1;
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
    for (y = y0; y < y1; y++) {
        span(x0, x1, y, band_y, it->color, it->opacity);
    }
}

static unsigned isqrt(unsigned n) {
    unsigned result = 0;
    unsigned bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= result + bit) {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static void raster_round(const draw_item_t *it, int band_y) {
    int x0, x1, y0, y1, rad, y, end;
    use_item_xform(it);
    x0 = tf_ox + it->x * tf_s / 256;
    x1 = tf_ox + (it->x + it->w - 1) * tf_s / 256 + 1;
    y0 = tf_oy + it->y * tf_s / 256;
    y1 = tf_oy + (it->y + it->h - 1) * tf_s / 256 + 1;
    rad = it->scale * tf_s / 256;
    if (rad > (x1 - x0) / 2) rad = (x1 - x0) / 2;
    if (rad > (y1 - y0) / 2) rad = (y1 - y0) / 2;
    y = y0 > band_y ? y0 : band_y;
    end = y1 < clip_y1 ? y1 : clip_y1;
    for (; y < end; y++) {
        int left = x0, right = x1;
        int dy = y < y0 + rad ? y0 + rad - y :
                 y >= y1 - rad ? y - (y1 - rad) : 0;
        if (dy && rad) {
            int dx = (int)isqrt((unsigned)(rad * rad - dy * dy));
            left = x0 + rad - dx;
            right = x1 - rad + dx + 1;
            if (right > x1) right = x1;
        }
        span(left, right, y, band_y, it->color, it->opacity);
    }
}

static void stroke(int x0, int y0, int x1, int y1, int band_y,
                   uint16_t color, uint8_t opacity) {
    int dx, dy, step, k0, k1, k, value, error;
    int64_t initial;
    x0 = tf_ox + x0 * tf_s / 256;
    y0 = tf_oy + y0 * tf_s / 256;
    x1 = tf_ox + x1 * tf_s / 256;
    y1 = tf_oy + y1 * tf_s / 256;
    if ((x0 < clip_x0 - 1 && x1 < clip_x0 - 1) ||
        (x0 >= clip_x1 && x1 >= clip_x1) ||
        (y0 < band_y && y1 < band_y) || (y0 >= clip_y1 && y1 >= clip_y1)) return;
    dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    dy = y1 >= y0 ? y1 - y0 : y0 - y1;
    if (dx >= dy) {
        if (x0 > x1) {
            int t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
        }
        if (!dy) {
            span(x0, x1 + 2, y0, band_y, color, opacity);
            if (opacity < 255) span(x0 + 1, x1 + 1, y0, band_y, color, opacity);
            return;
        }
        step = y1 >= y0 ? 1 : -1;
        k0 = (int)((int64_t)(step > 0 ? band_y - y0 - 1 : y0 - clip_y1) * dx / dy);
        k1 = (int)((int64_t)(step > 0 ? clip_y1 - y0 + 1 : y0 - band_y + 1) * dx / dy);
        if (k0 < clip_x0 - 1 - x0) k0 = clip_x0 - 1 - x0;
        if (k1 > clip_x1 - x0) k1 = clip_x1 - x0;
        if (k0 < 0) k0 = 0;
        if (k1 > dx) k1 = dx;
        initial = (int64_t)dy * k0 + dx / 2;
        value = (int)(initial / dx);
        error = (int)(initial % dx);
        for (k = k0; k <= k1; k++) {
            int y = y0 + step * value;
            pixel(x0 + k, y, band_y, color, opacity);
            pixel(x0 + k + 1, y, band_y, color, opacity);
            error += dy;
            if (error >= dx) { error -= dx; value++; }
        }
    } else {
        if (y0 > y1) {
            int t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
        }
        step = x1 >= x0 ? 1 : -1;
        k0 = band_y > y0 ? band_y - y0 : 0;
        k1 = clip_y1 - 1 < y1 ? clip_y1 - 1 - y0 : dy;
        initial = (int64_t)dx * k0 + dy / 2;
        value = (int)(initial / dy);
        error = (int)(initial % dy);
        for (k = k0; k <= k1; k++) {
            int x = x0 + step * value;
            pixel(x, y0 + k, band_y, color, opacity);
            pixel(x + 1, y0 + k, band_y, color, opacity);
            error += dx;
            if (error >= dy) { error -= dy; value++; }
        }
    }
}

static void fill_disc(int cx, int cy, int r, int band_y,
                      uint16_t color, uint8_t opacity) {
    int y, end;
    cx = tf_ox + cx * tf_s / 256;
    cy = tf_oy + cy * tf_s / 256;
    r = r * tf_s / 256;
    if (r < 1) return;
    y = cy - r > band_y ? cy - r : band_y;
    end = cy + r < clip_y1 ? cy + r : clip_y1;
    for (; y < end; y++) {
        int dy = y - cy;
        int dx = (int)isqrt((unsigned)(r * r - dy * dy));
        int right = cx + dx + 1;
        if (right > cx + r) right = cx + r;
        span(cx - dx, right, y, band_y, color, opacity);
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

typedef struct sample_axis {
    int value, error, step, remainder, denominator;
} sample_axis_t;

static sample_axis_t sample_begin(int numerator, int denominator) {
    sample_axis_t a;
    a.value = numerator / denominator;
    a.error = numerator % denominator;
    if (a.error < 0) {
        a.value--;
        a.error += denominator;
    }
    a.step = 256 / denominator;
    a.remainder = 256 % denominator;
    a.denominator = denominator;
    return a;
}

static void sample_next(sample_axis_t *a) {
    a->value += a->step;
    a->error += a->remainder;
    if (a->error >= a->denominator) {
        a->error -= a->denominator;
        a->value++;
    }
}

static void bitmap(const draw_item_t *it, int band_y, int ox, int oy,
                   int sw, int sh, int scale, const void *data, int kind) {
    int x0, x1, y0, y1, x, y;
    uint8_t alpha[16];
    sample_axis_t ay, ax0;
    use_item_xform(it);
    x0 = tf_ox + ox * tf_s / 256;
    x1 = tf_ox + (ox + sw * scale - 1) * tf_s / 256 + 1;
    y0 = tf_oy + oy * tf_s / 256;
    y1 = tf_oy + (oy + sh * scale - 1) * tf_s / 256 + 1;
    if (x0 < clip_x0) x0 = clip_x0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y0 < band_y) y0 = band_y;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x0 >= x1 || y0 >= y1) return;
    if (kind == KIND_MASK) {
        for (x = 0; x < 16; x++) alpha[x] = (uint8_t)(it->opacity * x / 15);
    }
    ax0 = sample_begin((x0 - (tf_ox + ox * tf_s / 256)) * 256, tf_s * scale);
    ay = sample_begin((y0 - (tf_oy + oy * tf_s / 256)) * 256, tf_s * scale);
    for (y = y0; y < y1; y++, sample_next(&ay)) {
        sample_axis_t ax = ax0;
        int sy = ay.value;
        if (sy < 0 || sy >= sh) continue;
        for (x = x0; x < x1; x++, sample_next(&ax)) {
            int sx = ax.value;
            if (sx < 0 || sx >= sw) continue;
            if (kind == KIND_IMAGE) {
                pixel(x, y, band_y, ((const uint16_t *)data)[sy * sw + sx], it->opacity);
            } else if (kind == KIND_MASK) {
                int i = sy * sw + sx;
                int byte = ((const uint8_t *)data)[i >> 1];
                pixel(x, y, band_y, it->color, alpha[(i & 1) ? byte & 15 : byte >> 4]);
            } else {
                int ink = data ? ((((const uint8_t *)data)[sy * ((sw + 7) >> 3) + (sx >> 3)] >> (7 - (sx & 7))) & 1)
                               : (sx == 0 || sy == 0 || sx == sw - 1 || sy == sh - 1);
                if (ink) pixel(x, y, band_y, it->color, it->opacity);
            }
        }
    }
}

static void raster_image(const draw_item_t *it, int band_y) {
    bitmap(it, band_y, it->x, it->y, it->w, it->h, it->scale,
           pic_ref[it->str_off], KIND_IMAGE);
}

static void raster_mask(const draw_item_t *it, int band_y) {
    bitmap(it, band_y, it->x, it->y, it->w, it->h, 1,
           pic_ref[it->str_off], KIND_MASK);
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
        bitmap(it, band_y, pen, it->y, gw, gh, scale,
               g ? ui_font_bits() + g->offset : 0, KIND_TEXT);
        pen += adv * scale;
    }
}

static uint32_t hash_bytes(uint32_t h, const void *data, unsigned n) {
    const uint8_t *p = data;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

static void extend(int *lo, int *hi, int a, int b) {
    if (a < *lo) *lo = a;
    if (b > *hi) *hi = b;
}

static history_t describe(const draw_item_t *it) {
    history_t result;
    int x0 = it->x, y0 = it->y, x1 = x0, y1 = y0;
    uint32_t h = 2166136261u;
    int16_t fields[10];
    fields[0] = it->x; fields[1] = it->y;
    fields[2] = it->w; fields[3] = it->h;
    fields[4] = (int16_t)it->color; fields[5] = it->kind;
    fields[6] = (int16_t)(it->opacity | (it->scale << 8));
    fields[7] = (int16_t)it->tf_s;
    fields[8] = it->tf_ox; fields[9] = it->tf_oy;
    h = hash_bytes(h, fields, sizeof fields);
    if (it->kind == KIND_TEXT) {
        const char *s = strs + it->str_off;
        int pen = x0;
        h = hash_bytes(h, s, (unsigned)strlen(s));
        while (*s) {
            const ui_glyph_t *g = ui_font_find(utf8_next(&s));
            int w = g ? g->w : 8;
            int height = g ? g->h : 16;
            extend(&x0, &x1, pen, pen + w * it->scale);
            extend(&y0, &y1, it->y, it->y + height * it->scale);
            pen += (g ? g->advance : 8) * it->scale;
        }
    } else if (it->kind == KIND_LINE) {
        extend(&x0, &x1, it->w, it->w);
        extend(&y0, &y1, it->h, it->h);
        x1 += 2;
        y1++;
    } else if (it->kind == KIND_SVG) {
        const ui_svg_cmd_t *cmd = pic_ref[it->str_off];
        int i;
        h = hash_bytes(h, cmd, (unsigned)it->w * sizeof *cmd);
        for (i = 0; i < it->w; i++) {
            int x = it->x + cmd[i].x0 * it->scale;
            int y = it->y + cmd[i].y0 * it->scale;
            if (cmd[i].r > 0) {
                int r = cmd[i].r * it->scale;
                extend(&x0, &x1, x - r, x + r + 1);
                extend(&y0, &y1, y - r, y + r + 1);
            } else {
                int ex = it->x + cmd[i].x1 * it->scale;
                int ey = it->y + cmd[i].y1 * it->scale;
                extend(&x0, &x1, x, x + 2);
                extend(&x0, &x1, ex, ex + 2);
                extend(&y0, &y1, y, y + 1);
                extend(&y0, &y1, ey, ey + 1);
            }
        }
    } else {
        int scale = it->kind == KIND_IMAGE ? it->scale : 1;
        x1 += it->w * scale;
        y1 += it->h * scale;
        if (it->kind == KIND_MASK) {
            h = hash_bytes(h, pic_ref[it->str_off], ((unsigned)it->w * (unsigned)it->h + 1u) / 2u);
        } else if (it->kind == KIND_IMAGE) {
            h = hash_bytes(h, &pic_ref[it->str_off], sizeof(void *));
        }
    }
    x0 = it->tf_ox + x0 * it->tf_s / 256 - 1;
    y0 = it->tf_oy + y0 * it->tf_s / 256 - 1;
    x1 = it->tf_ox + x1 * it->tf_s / 256 +
         ((it->kind == KIND_LINE || it->kind == KIND_SVG) ? 2 : 1);
    y1 = it->tf_oy + y1 * it->tf_s / 256 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > UI_W) x1 = UI_W;
    if (y1 > UI_H) y1 = UI_H;
    if (x0 >= x1 || y0 >= y1) x0 = x1 = y0 = y1 = 0;
    result.signature = h;
    result.box.x0 = (int16_t)x0; result.box.x1 = (int16_t)x1;
    result.box.y0 = (int16_t)y0; result.box.y1 = (int16_t)y1;
    return result;
}

static void damage(const bounds_t *b) {
    int first, last, i;
    if (b->x0 >= b->x1 || b->y0 >= b->y1) return;
    first = b->y0 / UI_BAND_H;
    last = (b->y1 - 1) / UI_BAND_H;
    for (i = first; i <= last; i++) {
        if (dirty[i].x0 > b->x0) dirty[i].x0 = b->x0;
        if (dirty[i].x1 < b->x1) dirty[i].x1 = b->x1;
    }
}

static void prepare(int full) {
    int i;
    for (i = 0; i < BAND_N; i++) {
        dirty[i].x0 = UI_W;
        dirty[i].x1 = 0;
    }
    for (i = 0; i < item_n; i++) {
        history_t next = describe(&items[i]);
        if (i >= history_n || next.signature != history[i].signature ||
            memcmp(&next.box, &history[i].box, sizeof next.box) || items[i].kind == KIND_IMAGE) {
            if (i < history_n) damage(&history[i].box);
            damage(&next.box);
        }
        history[i] = next;
    }
    for (i = item_n; i < history_n; i++) damage(&history[i].box);
    history_n = item_n;
    if (full || invalidated) {
        for (i = 0; i < BAND_N; i++) {
            dirty[i].x0 = 0;
            dirty[i].x1 = UI_W;
        }
    }
    invalidated = 0;
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

static void wait_display(void) {
    uint32_t start = hal_tick_us();
    while (hal_display_busy()) {
        ui_service();
        hal_idle();
    }
    stats.wait_us += hal_tick_us() - start;
}

static void flush_gif(void) {
    int x0, x1, y0, y1, y;
    if (!output_preempt && !gif_px) return;
    wait_display();
    invalidated = 1;
    x0 = gif_x < 0 ? 0 : gif_x;
    y0 = gif_y < 0 ? 0 : gif_y;
    x1 = gif_x + gif_w;
    y1 = gif_y + gif_h;
    if (x1 > UI_W) x1 = UI_W;
    if (y1 > UI_H) y1 = UI_H;
    if (gif_px && gif_w > 0 && gif_h > 0 && x1 > x0) {
        for (y = y0; y < y1; y++) {
            hal_display_flush((int16_t)x0, (int16_t)y, (int16_t)(x1 - x0), 1,
                              gif_px + (y - gif_y) * gif_w + x0 - gif_x);
            wait_display();
        }
    }
    gif_px = 0;
    gif_w = 0;
    output_preempt = 0;
}

void ui_draw_flush(void) {
    int y, n, row, next_buffer = 0;
    int snapshot;
    uint32_t start = hal_tick_us(), t0;
    memset(&stats, 0, sizeof stats);
    stats.items = (uint16_t)item_n;
    stats.dropped = dropped;
    if (!buffers[0] || !buffers[1] || !items || !history || !dirty) {
        stats.dropped++;
        flush_gif();
        return;
    }
    link_snap_begin(UI_W, UI_H);
    snapshot = link_snap_active();
    t0 = hal_tick_us();
    prepare(snapshot);
    stats.prepare_us = hal_tick_us() - t0;
    for (y = 0; y < UI_H; y += UI_BAND_H) {
        int first = 0;
        int w, h = UI_H - y;
        if (output_preempt && !snapshot) {
            invalidated = 1;
            break;
        }
        if (h > UI_BAND_H) h = UI_BAND_H;
        clip_x0 = dirty[y / UI_BAND_H].x0;
        clip_x1 = dirty[y / UI_BAND_H].x1;
        clip_y1 = y + h;
        w = clip_x1 - clip_x0;
        if (w <= 0) continue;
        band = buffers[next_buffer];
        t0 = hal_tick_us();
        for (row = 0; row < h; row++) {
            memset(band + row * UI_W + clip_x0, 0, (unsigned)w * 2u);
        }
        for (n = 0; n < item_n; n++) {
            const draw_item_t *it = &items[n];
            if (it->kind == KIND_FILL && it->opacity == 255 &&
                it->tf_ox + it->x * it->tf_s / 256 <= clip_x0 &&
                it->tf_oy + it->y * it->tf_s / 256 <= y &&
                it->tf_ox + (it->x + it->w - 1) * it->tf_s / 256 + 1 >= clip_x1 &&
                it->tf_oy + (it->y + it->h - 1) * it->tf_s / 256 + 1 >= clip_y1) first = n;
        }
        for (n = first; n < item_n; n++) {
            const bounds_t *b = &history[n].box;
            if (b->x0 >= clip_x1 || b->x1 <= clip_x0 || b->y0 >= clip_y1 || b->y1 <= y) continue;
            switch (items[n].kind) {
            case KIND_FILL: raster_fill(&items[n], y); break;
            case KIND_ROUND: raster_round(&items[n], y); break;
            case KIND_LINE: raster_line(&items[n], y); break;
            case KIND_TEXT: raster_text(&items[n], y); break;
            case KIND_SVG: raster_svg(&items[n], y); break;
            case KIND_IMAGE: raster_image(&items[n], y); break;
            case KIND_MASK: raster_mask(&items[n], y); break;
            }
        }
        for (row = 0; row < h; row++) {
            memmove(band + row * w, band + row * UI_W + clip_x0, (unsigned)w * 2u);
        }
        stats.raster_us += hal_tick_us() - t0;
        wait_display();
        hal_display_flush((int16_t)clip_x0, (int16_t)y, (int16_t)w, (int16_t)h, band);
        stats.pixels += (uint32_t)(w * h);
        stats.bands++;
        if (snapshot) link_snap_pixels(band, w * h);
        next_buffer ^= 1;
        ui_service();
    }
    wait_display();
    link_snap_end();
    flush_gif();
    stats.frame_us = hal_tick_us() - start;
}
