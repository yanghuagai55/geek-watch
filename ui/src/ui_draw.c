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
    KIND_MASK1 = 8,
    KIND_RIPPLE = 9,
    KIND_EXT_IMAGE = 10,
    ITEM_MAX = 80,
    STR_MAX = 640,
    PIC_MAX = 24,
    EXT_SRC_MAX = 4
};

typedef struct draw_item {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint16_t color;
    uint16_t str_off;
    uint16_t pal_off;
    uint8_t pal_n;
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
static int heal_pending;
static int clip_x0, clip_x1, clip_y1;
static ui_render_stats_t stats;
static uint16_t dropped;
static int tf_s;
static int tf_ox;
static int tf_oy;

static void stamp(draw_item_t *it) {
    it->pal_off = 0;
    it->pal_n = 0;
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
typedef struct ext_src_entry {
    const uint8_t *mem_ptr;
    uint32_t ext_off;
    uint16_t gen;
} ext_src_entry_t;
static ext_src_entry_t ext_srcs[EXT_SRC_MAX];
static int ext_src_n;
static const uint8_t *pal_cache_ptr;
static uint32_t pal_cache_off;
static uint16_t pal_cache_gen;
static uint8_t pal_cache_valid;
static uint16_t pal_cache[16];
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

uint32_t ui_utf8_next(const char **ps) {
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
        *ps += 4;
        return 0xFFFDu;
    }
    *ps += 1;
    return 0xFFFDu;
}
#define utf8_next ui_utf8_next

void ui_draw_init(void) {
    items = ui_mem_alloc(sizeof(draw_item_t) * ITEM_MAX, 4);
    strs = ui_mem_alloc(STR_MAX, 1);
    buffers[0] = ui_mem_alloc((uint32_t)UI_W * UI_BAND_H * 2u, 4);
    buffers[1] = ui_mem_alloc((uint32_t)UI_W * UI_BAND_H * 2u, 4);
    history = ui_mem_alloc(sizeof(history_t) * ITEM_MAX, 4);
    dirty = ui_mem_alloc(sizeof(dirty_band_t) * BAND_N, 2);
    history_n = 0;
    invalidated = 1;
    heal_pending = 0;
    output_preempt = 0;
    gif_px = 0;
    gif_w = gif_h = 0;
    memset(&stats, 0, sizeof stats);
}

void ui_draw_invalidate(void) {
    invalidated = 1;
    pal_cache_valid = 0;
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
    ext_src_n = 0;
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

void ui_draw_get_xform(int *scale256, int *ox, int *oy) {
    if (scale256) *scale256 = tf_s;
    if (ox) *ox = tf_ox;
    if (oy) *oy = tf_oy;
}

static draw_item_t *alloc_item(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color, uint16_t str_off,
                               uint8_t kind, uint8_t opacity, uint8_t scale) {
    draw_item_t *it;

    if (!items || !item_space() || opacity == 0) {
        return 0;
    }
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->str_off = str_off;
    it->kind = kind;
    it->opacity = opacity;
    it->scale = scale;
    stamp(it);
    return it;
}

void ui_draw_fill(int16_t x, int16_t y, int16_t w, int16_t h,
                  uint16_t color, uint8_t opacity) {
    if (w <= 0 || h <= 0) {
        return;
    }
    (void)alloc_item(x, y, w, h, color, 0u, KIND_FILL, opacity, 0u);
}

void ui_draw_round(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius,
                   uint16_t color, uint8_t opacity) {
    if (w <= 0 || h <= 0) {
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
    (void)alloc_item(x, y, w, h, color, 0u, KIND_ROUND, opacity, (uint8_t)radius);
}

void ui_draw_ripple(int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t radius, int16_t cy, int16_t rip_r,
                    uint16_t color, uint8_t opacity) {
    draw_item_t *it;

    if (w <= 0 || h <= 0 || rip_r <= 0) {
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
    if (rip_r > 2048) {
        rip_r = 2048;
    }
    it = alloc_item(x, y, w, h, color, (uint16_t)rip_r, KIND_RIPPLE, opacity, (uint8_t)radius);
    if (it) {
        it->pal_off = (uint16_t)cy;
    }
}

static void line_item_add(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          uint8_t width, uint16_t color, uint8_t opacity) {
    (void)alloc_item(x0, y0, x1, y1, color, 0u, KIND_LINE, opacity, width);
}

void ui_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  uint16_t color, uint8_t opacity) {
    line_item_add(x0, y0, x1, y1, 0u, color, opacity);
}

void ui_draw_line_w(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    uint8_t width, uint16_t color, uint8_t opacity) {
    line_item_add(x0, y0, x1, y1, width, color, opacity);
}

void ui_draw_text(int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity, uint8_t scale) {
    int len = 0;

    if (!strs || !utf8 || opacity == 0) {
        return;
    }
    while (utf8[len]) {
        len++;
    }
    if (str_n + len + 1 > STR_MAX) {
        dropped++;
        return;
    }
    if (!alloc_item(x, y, 0, 0, color, (uint16_t)str_n, KIND_TEXT, opacity, scale ? scale : 1u)) {
        return;
    }
    while (*utf8) {
        strs[str_n++] = *utf8++;
    }
    strs[str_n++] = 0;
}

#if !defined(AT32_SLIM_ROM)
void ui_svg_draw_p(int16_t x, int16_t y, uint8_t scale,
                   const ui_svg_cmd_t *cmd, int count,
                   const uint16_t *pal, uint8_t npal,
                   uint16_t color, uint8_t opacity, uint8_t width) {
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
    it->h = (int16_t)width;
    it->color = color;
    it->str_off = (uint16_t)ref;
    it->kind = KIND_SVG;
    it->opacity = opacity;
    it->scale = scale ? scale : 1;
    stamp(it);
    if (pal && npal) {
        int pref = pic_add(pal);
        if (pref >= 0) {
            it->pal_off = (uint16_t)(pref + 1);
            it->pal_n = npal;
        }
    }
}

void ui_svg_draw_w(int16_t x, int16_t y, uint8_t scale,
                   const ui_svg_cmd_t *cmd, int count,
                   uint16_t color, uint8_t opacity, uint8_t width) {
    ui_svg_draw_p(x, y, scale, cmd, count, 0, 0, color, opacity, width);
}

void ui_svg_draw(int16_t x, int16_t y, uint8_t scale,
                 const ui_svg_cmd_t *cmd, int count,
                 uint16_t color, uint8_t opacity) {
    ui_svg_draw_w(x, y, scale, cmd, count, color, opacity, 0);
}
#endif

static void pic_item_add(int16_t x, int16_t y, int16_t w, int16_t h,
                         const void *ptr, uint16_t color,
                         uint8_t kind, uint8_t opacity, uint8_t scale) {
    int ref;

    if (!items || !ptr || w <= 0 || h <= 0 || item_n >= ITEM_MAX || opacity == 0) {
        if (items && ptr && w > 0 && h > 0 && opacity != 0 && item_n >= ITEM_MAX) {
            dropped++;
        }
        return;
    }
    ref = pic_add(ptr);
    if (ref < 0) {
        return;
    }
    (void)alloc_item(x, y, w, h, color, (uint16_t)ref, kind, opacity, scale ? scale : 1u);
}

void ui_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint8_t scale, const uint16_t *rgb565, uint8_t opacity) {
    pic_item_add(x, y, w, h, rgb565, 0u, KIND_IMAGE, opacity, scale);
}

void ui_mask_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint8_t *cover4, uint16_t color, uint8_t opacity) {
    pic_item_add(x, y, w, h, cover4, color, KIND_MASK, opacity, 1u);
}

void ui_mask_rle_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                      const uint8_t *rle4, uint16_t color, uint8_t opacity) {
    int prev = item_n;
    ui_mask_draw(x, y, w, h, rle4, color, opacity);
    if (item_n > prev) {
        items[prev].pal_n = 1;
    }
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
        int adv = g ? g->w : 8;
        width += adv * scale;
    }
    return (int16_t)width;
}

int16_t ui_text_height(const char *utf8, uint8_t scale) {
    int height = 0;
    const char *s = utf8;

    if (!s) return 0;
    if (!scale) scale = 1;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const ui_glyph_t *g = ui_font_find(cp);
        int h = (g ? g->h : 16) * scale;
        if (h > height) height = h;
    }
    return (int16_t)height;
}

void ui_text_draw_center(int16_t cx, int16_t y, const char *utf8,
                         uint16_t color, uint8_t opacity, uint8_t scale) {
    ui_text_draw((int16_t)(cx - ui_text_width(utf8, scale) / 2), y,
                 utf8, color, opacity, scale);
}

void ui_text_draw_right(int16_t right, int16_t y, const char *utf8,
                        uint16_t color, uint8_t opacity, uint8_t scale) {
    ui_text_draw((int16_t)(right - ui_text_width(utf8, scale)), y,
                 utf8, color, opacity, scale);
}

void ui_text_fit(char *out, int capacity, const char *utf8,
                 int16_t max_width, uint8_t scale) {
    int n;
    int suffix = ui_text_width("...", scale);

    if (!out || capacity <= 0) return;
    out[0] = 0;
    if (!utf8) return;
    n = 0;
    while (*utf8) {
        const char *next = utf8;
        int bytes;
        utf8_next(&next);
        bytes = (int)(next - utf8);
        if (n + bytes >= capacity) break;
        memcpy(out + n, utf8, (size_t)bytes);
        n += bytes;
        utf8 = next;
    }
    out[n] = 0;
    if (ui_text_width(out, scale) <= max_width) return;
    while (n > 0) {
        int width;
        n--;
        while (n > 0 && (((unsigned char)out[n] & 0xC0) == 0x80)) n--;
        out[n] = 0;
        width = ui_text_width(out, scale) + suffix;
        if (width <= max_width) {
            if (n + 4 <= capacity) memcpy(out + n, "...", 4);
            return;
        }
    }
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

/* Render a circular ripple centered at (x+w/2, cy) clipped to a rounded box. */
static void raster_ripple(const draw_item_t *it, int band_y) {
    int x0, x1, y0, y1, crad, rip_r, r2, cx, cy, ry0, ry1, y, end;
    use_item_xform(it);
    x0 = tf_ox + it->x * tf_s / 256;
    x1 = tf_ox + (it->x + it->w - 1) * tf_s / 256 + 1;
    y0 = tf_oy + it->y * tf_s / 256;
    y1 = tf_oy + (it->y + it->h - 1) * tf_s / 256 + 1;
    crad = it->scale * tf_s / 256;
    if (crad > (x1 - x0) / 2) crad = (x1 - x0) / 2;
    if (crad > (y1 - y0) / 2) crad = (y1 - y0) / 2;
    rip_r = (int)it->str_off * tf_s / 256;
    if (rip_r <= 0) return;
    r2 = rip_r > 2 ? (rip_r * rip_r - rip_r) : (rip_r * rip_r);
    cx = tf_ox + (it->x + it->w / 2) * tf_s / 256;
    cy = tf_oy + (int)(int16_t)it->pal_off * tf_s / 256;
    ry0 = cy - rip_r;
    ry1 = cy + rip_r + 1;
    y = y0 > band_y ? y0 : band_y;
    if (ry0 > y) y = ry0;
    end = y1 < clip_y1 ? y1 : clip_y1;
    if (ry1 < end) end = ry1;
    for (; y < end; y++) {
        int left = x0, right = x1;
        int dy_card = y < y0 + crad ? y0 + crad - y :
                      y >= y1 - crad ? y - (y1 - crad) : 0;
        int dy_rip = y - cy;
        int rx, rleft, rright;
        if (dy_card && crad) {
            int dx = (int)isqrt((unsigned)(crad * crad - dy_card * dy_card));
            left = x0 + crad - dx;
            right = x1 - crad + dx + 1;
            if (right > x1) right = x1;
        }
        if (dy_rip * dy_rip > r2) continue;
        rx = (int)isqrt((unsigned)(r2 - dy_rip * dy_rip));
        rleft = cx - rx;
        rright = cx + rx + 1;
        if (rleft > left) left = rleft;
        if (rright < right) right = rright;
        if (left < right) {
            span(left, right, y, band_y, it->color, it->opacity);
        }
    }
}

static void stroke(int x0, int y0, int x1, int y1, int band_y,
                   uint16_t color, uint8_t opacity) {
    int dx, dy, step, k0, k1, k, value, error;
    uint32_t initial;
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
        int n0, n1;
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
        n0 = step > 0 ? band_y - y0 - 1 : y0 - clip_y1;
        n1 = step > 0 ? clip_y1 - y0 + 1 : y0 - band_y + 1;
        k0 = n0 <= 0 ? 0 : (n0 > dy ? dx + 1 : (int)((uint32_t)n0 * (uint32_t)dx / (uint32_t)dy));
        k1 = n1 < 0 ? -1 : (n1 >= dy ? dx : (int)((uint32_t)n1 * (uint32_t)dx / (uint32_t)dy));
        if (k0 < clip_x0 - 1 - x0) k0 = clip_x0 - 1 - x0;
        if (k1 > clip_x1 - x0) k1 = clip_x1 - x0;
        if (k0 < 0) k0 = 0;
        if (k1 > dx) k1 = dx;
        if (k0 > k1) return;
        initial = (uint32_t)dy * (uint32_t)k0 + (uint32_t)(dx / 2);
        value = (int)(initial / (uint32_t)dx);
        error = (int)(initial % (uint32_t)dx);
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
        if (k0 > k1) return;
        initial = (uint32_t)dx * (uint32_t)k0 + (uint32_t)(dy / 2);
        value = (int)(initial / (uint32_t)dy);
        error = (int)(initial % (uint32_t)dy);
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

static void stroke_w(int x0, int y0, int x1, int y1, int width,
                     int band_y, uint16_t color, uint8_t opacity) {
    int dx = x1 - x0, dy = y1 - y0;
    int r, o, len, nx, ny;

    if (width <= 2) {
        stroke(x0, y0, x1, y1, band_y, color, opacity);
        return;
    }
    r = (width - 1) / 2;
    len = (int)isqrt((unsigned)(dx * dx + dy * dy));
    if (len < 1) {
        len = 1;
    }
    nx = -dy * 256 / len;
    ny = dx * 256 / len;
    for (o = -r; o <= r; o++) {
        int vx = nx * o, vy = ny * o;
        int ox = (vx + (vx >= 0 ? 128 : -128)) / 256;
        int oy = (vy + (vy >= 0 ? 128 : -128)) / 256;
        stroke(x0 + ox, y0 + oy, x1 + ox, y1 + oy, band_y, color, opacity);
    }
    fill_disc(x0, y0, r, band_y, color, opacity);
    fill_disc(x1, y1, r, band_y, color, opacity);
}

static void raster_line(const draw_item_t *it, int band_y) {
    use_item_xform(it);
    if (it->scale > 2) {
        stroke_w(it->x, it->y, it->w, it->h, it->scale, band_y, it->color, it->opacity);
    } else {
        stroke(it->x, it->y, it->w, it->h, band_y, it->color, it->opacity);
    }
}

#if !defined(AT32_SLIM_ROM)
static void raster_svg(const draw_item_t *it, int band_y) {
    const ui_svg_cmd_t *cmd;
    const uint16_t *pal = 0;
    int scale = it->scale ? it->scale : 1;
    int width = it->h > 0 ? it->h : 2;
    int n = it->w;
    int i;

    if (it->str_off >= PIC_MAX) {
        return;
    }
    cmd = (const ui_svg_cmd_t *)pic_ref[it->str_off];
    if (!cmd) {
        return;
    }
    if (it->pal_off && it->pal_off <= PIC_MAX) {
        pal = (const uint16_t *)pic_ref[it->pal_off - 1];
    }
    use_item_xform(it);
    for (i = 0; i < n; i++) {
        uint16_t color = it->color;
        if (pal && cmd[i].ci >= 1 && cmd[i].ci <= it->pal_n) {
            color = pal[cmd[i].ci - 1];
        }
        if (cmd[i].r > 0) {
            fill_disc(it->x + cmd[i].x0 * scale, it->y + cmd[i].y0 * scale,
                      cmd[i].r * scale, band_y, color, it->opacity);
        } else {
            stroke_w(it->x + cmd[i].x0 * scale, it->y + cmd[i].y0 * scale,
                     it->x + cmd[i].x1 * scale, it->y + cmd[i].y1 * scale,
                     width, band_y, color, it->opacity);
        }
    }
}
#endif

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
    int rle = (kind == KIND_MASK && it->pal_n != 0);
    int box2;
    int stride_bytes = ((kind == KIND_MASK1 && it->pal_off ? (int)it->pal_off : sw) + 7) >> 3;
    const uint8_t *rle_p = (const uint8_t *)data;
    int cur_sy = -1, rem = 0;
    uint8_t val = 0;
    uint8_t rows[2][64];
    uint8_t alpha[16];
    sample_axis_t ay, ax0;
    use_item_xform(it);
    /* Integer 2x2 area/box downsampling keeps 1/2-scale STHupo strokes intact. */
    box2 = (kind == KIND_MASK && tf_s < 256);
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
        int sy1;
        const uint8_t *r0 = 0;
        const uint8_t *r1 = 0;
        if (sy < 0 || sy >= sh) continue;
        sy1 = (box2 && sy + 1 < sh) ? sy + 1 : sy;
        if (rle) {
            while (cur_sy < sy1) {
                int k;
                uint8_t *dst;
                cur_sy++;
                dst = rows[cur_sy & 1];
                for (k = 0; k < sw; k++) {
                    if (rem == 0) {
                        uint8_t b = *rle_p++;
                        rem = (b >> 2) + 1;
                        val = (b & 3) * 5;
                    }
                    if (k < 64) dst[k] = val;
                    rem--;
                }
            }
            r0 = rows[sy & 1];
            r1 = rows[sy1 & 1];
        }
        for (x = x0; x < x1; x++, sample_next(&ax)) {
            int sx = ax.value;
            if (sx < 0 || sx >= sw) continue;
#if !defined(AT32_SLIM_ROM)
            if (kind == KIND_IMAGE) {
                pixel(x, y, band_y, ((const uint16_t *)data)[sy * sw + sx], it->opacity);
            } else
#endif
            if (kind == KIND_MASK) {
                int a4;
                if (box2) {
                    int sx1 = sx + 1 < sw ? sx + 1 : sx;
                    int fx = (ax.error * 16) / ax.denominator;
                    int fy = (ay.error * 16) / ay.denominator;
                    int inv_fx = 16 - fx;
                    int inv_fy = 16 - fy;
                    int p00, p10, p01, p11;
                    if (rle) {
                        p00 = sx < 64 ? r0[sx] : 0;
                        p10 = sx1 < 64 ? r0[sx1] : 0;
                        p01 = sx < 64 ? r1[sx] : 0;
                        p11 = sx1 < 64 ? r1[sx1] : 0;
                    } else {
                        const uint8_t *m = (const uint8_t *)data;
                        int i00 = sy * sw + sx;
                        int i10 = sy * sw + sx1;
                        int i01 = sy1 * sw + sx;
                        int i11 = sy1 * sw + sx1;
                        p00 = (i00 & 1) ? (m[i00 >> 1] & 15) : (m[i00 >> 1] >> 4);
                        p10 = (i10 & 1) ? (m[i10 >> 1] & 15) : (m[i10 >> 1] >> 4);
                        p01 = (i01 & 1) ? (m[i01 >> 1] & 15) : (m[i01 >> 1] >> 4);
                        p11 = (i11 & 1) ? (m[i11 >> 1] & 15) : (m[i11 >> 1] >> 4);
                    }
                    a4 = (inv_fx * inv_fy * p00 + fx * inv_fy * p10 +
                          inv_fx * fy * p01 + fx * fy * p11 + 128) >> 8;
                } else if (rle) {
                    a4 = sx < 64 ? r0[sx] : 0;
                } else {
                    int i = sy * sw + sx;
                    int byte = ((const uint8_t *)data)[i >> 1];
                    a4 = (i & 1) ? (byte & 15) : (byte >> 4);
                }
                pixel(x, y, band_y, it->color, alpha[a4]);
            } else {
                int ink = data ? ((((const uint8_t *)data)[sy * stride_bytes + (sx >> 3)] >> (7 - (sx & 7))) & 1)
                               : (sx == 0 || sy == 0 || sx == sw - 1 || sy == sh - 1);
                if (ink) pixel(x, y, band_y, it->color, it->opacity);
            }
        }
    }
}

static void raster_image(const draw_item_t *it, int band_y) {
#if !defined(AT32_SLIM_ROM)
    bitmap(it, band_y, it->x, it->y, it->w, it->h, it->scale,
           pic_ref[it->str_off], KIND_IMAGE);
#else
    (void)it; (void)band_y;
#endif
}

static void raster_mask(const draw_item_t *it, int band_y) {
    bitmap(it, band_y, it->x, it->y, it->w, it->h, 1,
           pic_ref[it->str_off], KIND_MASK);
}

void ui_mask1_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                   const uint8_t *bits, uint16_t color, uint8_t opacity) {
    pic_item_add(x, y, w, h, bits, color, KIND_MASK1, opacity, 1u);
}

void ui_mask1_draw_clip(int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t src_w, const uint8_t *bits,
                        uint16_t color, uint8_t opacity) {
    int prev;

    if (w <= 0 || src_w <= 0) {
        return;
    }
    if (w > src_w) {
        w = src_w;
    }
    prev = item_n;
    ui_mask1_draw(x, y, w, h, bits, color, opacity);
    if (item_n > prev) {
        items[prev].pal_off = (uint16_t)src_w;
    }
}

static void raster_mask1(const draw_item_t *it, int band_y) {
    bitmap(it, band_y, it->x, it->y, it->w, it->h, 1,
           pic_ref[it->str_off], KIND_MASK1);
}

static draw_item_t *ext_item_add(int16_t x, int16_t y, int16_t w, int16_t h,
                                 const uint8_t *mem_ptr, uint32_t ext_offset,
                                 uint8_t fmt, uint16_t color, uint8_t opacity,
                                 uint16_t gen_tag) {
    draw_item_t *it;
    int idx;

    if (!items || w <= 0 || h <= 0 || !item_space() || opacity == 0 ||
        fmt < UI_EXT_FMT_1BPP || fmt > UI_EXT_FMT_RGB565_RLE ||
        ext_src_n >= EXT_SRC_MAX) {
        return 0;
    }
    idx = ext_src_n++;
    ext_srcs[idx].mem_ptr = mem_ptr;
    ext_srcs[idx].ext_off = ext_offset;
    ext_srcs[idx].gen = gen_tag;
    it = &items[item_n++];
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->str_off = (uint16_t)idx;
    it->kind = KIND_EXT_IMAGE;
    it->opacity = opacity;
    it->scale = 1;
    stamp(it);
    it->pal_n = fmt;
    return it;
}

void ui_ext_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint32_t ext_offset, uint8_t fmt,
                       uint16_t color, uint8_t opacity) {
    if (ext_offset >= HAL_EXT_FLASH_SIZE) {
        return;
    }
    (void)ext_item_add(x, y, w, h, 0, ext_offset, fmt, color, opacity, 0u);
}

void ui_ext_image_draw_fast(int16_t x, int16_t y, int16_t w, int16_t h,
                            const uint8_t *mem_ptr, uint32_t ext_offset,
                            uint8_t fmt, uint16_t color, uint8_t opacity,
                            uint16_t gen_tag) {
    if (!mem_ptr && ext_offset >= HAL_EXT_FLASH_SIZE) {
        return;
    }
    (void)ext_item_add(x, y, w, h, mem_ptr, mem_ptr ? 0u : ext_offset,
                       fmt, color, opacity, gen_tag);
}

void ui_mem_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint8_t *pal4_rle, uint8_t transparent0, uint8_t opacity) {
    if (!pal4_rle) {
        return;
    }
    (void)ext_item_add(x, y, w, h, pal4_rle, 0u, UI_EXT_FMT_PAL4_RLE,
                       transparent0 ? 1u : 0u, opacity, 0u);
}

static const uint8_t *ext_mem_ptr;

static int read_img_src(uint32_t off, uint8_t *dst, uint32_t len) {
    if (ext_mem_ptr) {
        memcpy(dst, ext_mem_ptr + off, (size_t)len);
        return 1;
    }
    return hal_ext_flash_read(off, dst, len);
}

typedef struct ext_stream {
    uint32_t pos;
    uint32_t end;
    uint8_t off;
    uint8_t len;
    uint8_t eof;
    uint8_t buf[48];
} ext_stream_t;

static int ext_stream_getc(ext_stream_t *s) {
    if (s->off >= s->len) {
        uint32_t rem = s->pos < s->end ? (s->end - s->pos) : 0u;
        uint32_t n = rem > sizeof s->buf ? sizeof s->buf : rem;
        if (n == 0u || !read_img_src(s->pos, s->buf, n)) {
            s->eof = 1;
            return 0;
        }
        s->pos += n;
        s->off = 0;
        s->len = (uint8_t)n;
    }
    return s->buf[s->off++];
}

static int decode_ext_rle_row(uint8_t fmt, uint32_t base_off,
                              int sw, int sh, int sy, uint8_t *row_buf) {
    uint32_t tbl_off = (fmt == UI_EXT_FMT_PAL4_RLE) ? (base_off + 32u) : base_off;
    uint32_t data_off = tbl_off + (uint32_t)sh * 2u;
    uint8_t b[4];
    uint16_t r_start, r_end;
    ext_stream_t s;
    int px = 0;

    if (sy == 0) {
        if ((!ext_mem_ptr && tbl_off + 2u > HAL_EXT_FLASH_SIZE) ||
            !read_img_src(tbl_off, b, 2u)) {
            return 0;
        }
        r_start = 0u;
        r_end = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    } else {
        uint32_t boff = tbl_off + (uint32_t)(sy - 1) * 2u;
        if ((!ext_mem_ptr && boff + 4u > HAL_EXT_FLASH_SIZE) ||
            !read_img_src(boff, b, 4u)) {
            return 0;
        }
        r_start = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
        r_end = (uint16_t)(b[2] | ((uint16_t)b[3] << 8));
    }
    if (r_end <= r_start || (!ext_mem_ptr && data_off + (uint32_t)r_end > HAL_EXT_FLASH_SIZE)) {
        return 0;
    }
    s.pos = data_off + (uint32_t)r_start;
    s.end = data_off + (uint32_t)r_end;
    s.off = 0;
    s.len = 0;
    s.eof = 0;

    while (px < sw && !s.eof) {
        int hdr = ext_stream_getc(&s);
        int is_rep = (hdr & 0x80) != 0;
        int n = (hdr & 0x7F) + (is_rep ? 2 : 1);
        uint8_t p0 = 0, p1 = 0;
        int i, nib = 0;
        if (s.eof) break;
        if (is_rep) {
            p0 = (uint8_t)ext_stream_getc(&s);
            if (fmt == UI_EXT_FMT_RGB565_RLE) {
                p1 = (uint8_t)ext_stream_getc(&s);
            } else {
                p0 &= 0x0Fu;
            }
        }
        for (i = 0; i < n && px < sw && !s.eof; i++) {
            if (fmt == UI_EXT_FMT_PAL4_RLE) {
                uint8_t idx = p0;
                if (!is_rep) {
                    if ((i & 1) == 0) {
                        nib = ext_stream_getc(&s);
                        idx = (uint8_t)((nib >> 4) & 0x0Fu);
                    } else {
                        idx = (uint8_t)(nib & 0x0Fu);
                    }
                }
                row_buf[px++] = idx;
            } else {
                uint8_t lo = is_rep ? p0 : (uint8_t)ext_stream_getc(&s);
                uint8_t hi = is_rep ? p1 : (uint8_t)ext_stream_getc(&s);
                row_buf[px * 2] = lo;
                row_buf[px * 2 + 1] = hi;
                px++;
            }
        }
    }
    return !s.eof && px == sw;
}

static void raster_ext_image(const draw_item_t *it, int band_y) {
    int x0, x1, y0, y1, x, y;
    int sw = it->w, sh = it->h;
    int scale = it->scale ? it->scale : 1;
    uint8_t fmt = it->pal_n;
    int row_bytes = 0;
    int loaded_sy = -1;
    uint8_t row_buf[256];
    uint16_t *pal = pal_cache;
    uint8_t skip0;
    const ext_src_entry_t *src = &ext_srcs[it->str_off < EXT_SRC_MAX ? it->str_off : 0];
    uint32_t base_off = src->ext_off;
    sample_axis_t ay, ax0;

    ext_mem_ptr = src->mem_ptr;
    skip0 = (uint8_t)(it->color != 0u);
    use_item_xform(it);
    if (fmt == UI_EXT_FMT_1BPP) {
        row_bytes = (sw + 7) >> 3;
#if !defined(AT32_SLIM_ROM)
    } else if (fmt == UI_EXT_FMT_4BPP) {
        row_bytes = (sw + 1) >> 1;
#endif
    } else if (fmt == UI_EXT_FMT_PAL4_RLE) {
        row_bytes = sw;
    } else {
        row_bytes = sw * 2;
    }
    if (row_bytes <= 0 || row_bytes > (int)sizeof row_buf) {
        return;
    }
    if (fmt == UI_EXT_FMT_PAL4_RLE) {
        if (!pal_cache_valid || pal_cache_ptr != ext_mem_ptr ||
            (!ext_mem_ptr && pal_cache_off != base_off) ||
            pal_cache_gen != src->gen) {
            uint8_t raw_pal[32];
            int k;
            if ((!ext_mem_ptr && base_off + 32u > HAL_EXT_FLASH_SIZE) ||
                !read_img_src(base_off, raw_pal, 32u)) {
                return;
            }
            for (k = 0; k < 16; k++) {
                pal_cache[k] = (uint16_t)(raw_pal[k * 2] | ((uint16_t)raw_pal[k * 2 + 1] << 8));
            }
            pal_cache_ptr = ext_mem_ptr;
            pal_cache_off = base_off;
            pal_cache_gen = src->gen;
            pal_cache_valid = 1u;
        }
    }
    x0 = tf_ox + it->x * tf_s / 256;
    x1 = tf_ox + (it->x + sw * scale - 1) * tf_s / 256 + 1;
    y0 = tf_oy + it->y * tf_s / 256;
    y1 = tf_oy + (it->y + sh * scale - 1) * tf_s / 256 + 1;
    if (x0 < clip_x0) x0 = clip_x0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y0 < band_y) y0 = band_y;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    /* Fast path: 1:1 unscaled opaque PAL4_RLE directly from on-chip XIP Flash/RAM (zero row_buf copy) */
    if (ext_mem_ptr && tf_s == 256 && scale == 1 && it->opacity == 255 && !skip0 &&
        fmt == UI_EXT_FMT_PAL4_RLE) {
        const uint8_t *tbl = ext_mem_ptr + 32u;
        const uint8_t *dat = tbl + (uint32_t)sh * 2u;
        int ox = tf_ox + it->x;
        int oy = tf_oy + it->y;
        int sx0 = x0 - ox;
        int sx1 = x1 - ox;
        if (sx0 >= 0 && sx1 <= sw) {
            for (y = y0; y < y1; y++) {
                int sy = y - oy;
                uint16_t r_start, r_end;
                const uint8_t *rp;
                const uint8_t *rend;
                uint16_t *row_dst = band + (y - band_y) * UI_W;
                int px = 0;
                if (sy < 0 || sy >= sh) continue;
                r_start = sy == 0 ? 0u : (uint16_t)(tbl[(sy - 1) * 2] | ((uint16_t)tbl[(sy - 1) * 2 + 1] << 8));
                r_end = (uint16_t)(tbl[sy * 2] | ((uint16_t)tbl[sy * 2 + 1] << 8));
                if (r_end <= r_start) continue;
                rp = dat + r_start;
                rend = dat + r_end;
                while (px < sx1 && rp < rend) {
                    uint8_t hdr = *rp++;
                    int is_rep = (hdr & 0x80u) != 0;
                    int n = (hdr & 0x7Fu) + (is_rep ? 2 : 1);
                    if (is_rep) {
                        uint16_t c;
                        int end_px = px + n;
                        int s_a, s_b;
                        if (rp >= rend) break;
                        c = pal[*rp++ & 0x0Fu];
                        s_a = px > sx0 ? px : sx0;
                        s_b = end_px < sx1 ? end_px : sx1;
                        for (; s_a < s_b; s_a++) row_dst[ox + s_a] = c;
                        px = end_px;
                    } else {
                        int i, nib = 0;
                        for (i = 0; i < n && px < sw && rp <= rend; i++) {
                            uint8_t ci;
                            if ((i & 1) == 0) {
                                if (rp >= rend) break;
                                nib = *rp++;
                                ci = (uint8_t)(nib >> 4);
                            } else {
                                ci = (uint8_t)(nib & 0x0Fu);
                            }
                            if (px >= sx0 && px < sx1) row_dst[ox + px] = pal[ci];
                            px++;
                        }
                    }
                }
            }
            return;
        }
    }
    ax0 = sample_begin((x0 - (tf_ox + it->x * tf_s / 256)) * 256, tf_s * scale);
    ay = sample_begin((y0 - (tf_oy + it->y * tf_s / 256)) * 256, tf_s * scale);
    for (y = y0; y < y1; y++, sample_next(&ay)) {
        sample_axis_t ax = ax0;
        int sy = ay.value;
        if (sy < 0 || sy >= sh) {
            continue;
        }
        if (sy != loaded_sy) {
            if (fmt == UI_EXT_FMT_PAL4_RLE || fmt == UI_EXT_FMT_RGB565_RLE) {
                if (!decode_ext_rle_row(fmt, base_off, sw, sh, sy, row_buf)) {
                    return;
                }
            } else {
                uint32_t off = base_off + (uint32_t)sy * (uint32_t)row_bytes;
                if (!read_img_src(off, row_buf, (uint32_t)row_bytes)) {
                    return;
                }
            }
            loaded_sy = sy;
        }
        for (x = x0; x < x1; x++, sample_next(&ax)) {
            int sx = ax.value;
            if (sx < 0 || sx >= sw) {
                continue;
            }
            if (fmt == UI_EXT_FMT_1BPP) {
                if ((row_buf[sx >> 3] >> (7 - (sx & 7))) & 1) {
                    pixel(x, y, band_y, it->color, it->opacity);
                }
#if !defined(AT32_SLIM_ROM)
            } else if (fmt == UI_EXT_FMT_4BPP) {
                uint8_t b = row_buf[sx >> 1];
                uint8_t a4 = (sx & 1) ? (b & 15u) : (b >> 4);
                if (a4) {
                    pixel(x, y, band_y, it->color, (uint8_t)((int)it->opacity * a4 / 15));
                }
#endif
            } else if (fmt == UI_EXT_FMT_PAL4_RLE) {
                uint8_t ci = (uint8_t)(row_buf[sx] & 15u);
                if (!skip0 || ci != 0u) {
                    pixel(x, y, band_y, pal[ci], it->opacity);
                }
            } else {
                uint16_t c = (uint16_t)(row_buf[sx * 2] | ((uint16_t)row_buf[sx * 2 + 1] << 8));
                pixel(x, y, band_y, c, it->opacity);
            }
        }
    }
}

static void unpack_font_rle(const uint8_t *src, uint8_t *dst, int dst_len, int bpr) {
    uint8_t h0 = *src++;
    uint8_t h1;
    int pos, end;

    memset(dst, 0, (size_t)dst_len);
    if (h0 == 0xFFu) {
        return;
    }
    h1 = *src++;
    pos = (int)h0 * bpr;
    end = pos + ((int)(h1 & 0x0Fu) + 1) * bpr;
    if (end > dst_len) {
        end = dst_len;
    }
    if (!(h1 & 0x80u)) {
        while (pos < end) {
            dst[pos++] = *src++;
        }
        return;
    }
    while (pos < end) {
        uint8_t ctrl = *src++;
        if (ctrl & 0x80u) {
            int cnt = (int)(ctrl & 0x7Fu) + 2;
            uint8_t v = *src++;
            while (cnt-- > 0 && pos < end) {
                dst[pos++] = v;
            }
        } else {
            int cnt = (int)ctrl + 1;
            while (cnt-- > 0 && pos < end) {
                dst[pos++] = *src++;
            }
        }
    }
}

static void raster_text(const draw_item_t *it, int band_y) {
    use_item_xform(it);
    const char *s = strs + it->str_off;
    int pen = it->x;
    int scale = it->scale ? it->scale : 1;
    int rle = ui_font_is_rle();

    while (*s) {
        uint32_t cp = utf8_next(&s);
        const ui_glyph_t *g = ui_font_find(cp);
        int gw = g ? g->w : 8;
        int gh = g ? g->h : 16;
        uint8_t raw[32];
        const void *bmp_data = 0;

        if (g) {
            if (g->offset & 0x8000u) {
                bmp_data = link_font_ext_bits((uint16_t)(g->offset & 0x7FFFu));
            } else {
                bmp_data = ui_font_bits() + g->offset;
            }
            if (bmp_data && rle) {
                int bpr = (gw + 7) >> 3;
                int need = bpr * gh;
                if (need > (int)sizeof raw) {
                    need = (int)sizeof raw;
                }
                unpack_font_rle((const uint8_t *)bmp_data, raw, need, bpr);
                bmp_data = raw;
            }
        }
        bitmap(it, band_y, pen, it->y, gw, gh, scale, bmp_data, KIND_TEXT);
        pen += gw * scale;
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
    h = hash_bytes(h, &it->pal_off, sizeof it->pal_off);
    h = hash_bytes(h, &it->pal_n, sizeof it->pal_n);
    if (it->kind == KIND_RIPPLE) {
        h = hash_bytes(h, &it->str_off, sizeof it->str_off);
    } else if (it->kind == KIND_EXT_IMAGE) {
        const ext_src_entry_t *es = &ext_srcs[it->str_off < EXT_SRC_MAX ? it->str_off : 0];
        h = hash_bytes(h, es, sizeof *es);
    }
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
            pen += w * it->scale;
        }
    } else if (it->kind == KIND_LINE) {
        if (it->scale > 2) {
            int half = it->scale / 2 + 1;
            extend(&x0, &x1, x0 - half, x0 + half);
            extend(&x0, &x1, it->w - half, it->w + half);
            extend(&y0, &y1, y0 - half, y0 + half);
            extend(&y0, &y1, it->h - half, it->h + half);
        } else {
            extend(&x0, &x1, it->w, it->w);
            extend(&y0, &y1, it->h, it->h);
            x1 += 2;
            y1++;
        }
#if !defined(AT32_SLIM_ROM)
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
                if (it->h > 2) {
                    int half = it->h / 2 + 1;
                    extend(&x0, &x1, x - half, x + half);
                    extend(&x0, &x1, ex - half, ex + half);
                    extend(&y0, &y1, y - half, y + half);
                    extend(&y0, &y1, ey - half, ey + half);
                } else {
                    extend(&x0, &x1, x, x + 2);
                    extend(&x0, &x1, ex, ex + 2);
                    extend(&y0, &y1, y, y + 1);
                    extend(&y0, &y1, ey, ey + 1);
                }
            }
        }
#endif
    } else {
        int scale = it->kind == KIND_IMAGE ? it->scale : 1;
        x1 += it->w * scale;
        y1 += it->h * scale;
        if (it->kind == KIND_MASK && !it->pal_n) {
            h = hash_bytes(h, pic_ref[it->str_off], ((unsigned)it->w * (unsigned)it->h + 1u) / 2u);
        } else if (it->kind == KIND_MASK1 && pic_ref[it->str_off]) {
            int stride_bytes = ((it->pal_off ? (int)it->pal_off : it->w) + 7) >> 3;
            h = hash_bytes(h, pic_ref[it->str_off], (unsigned)stride_bytes * (unsigned)it->h);
        } else if (it->kind == KIND_IMAGE || (it->kind == KIND_MASK && it->pal_n)) {
            h = hash_bytes(h, &pic_ref[it->str_off], sizeof(void *));
            if (it->kind == KIND_MASK && pic_ref[it->str_off]) {
                unsigned sample_n = (it->w >= 16 && it->h >= 8) ? 16u : 2u;
                h = hash_bytes(h, pic_ref[it->str_off], sample_n);
            }
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

#if !defined(AT32_SLIM_ROM)
void ui_gif_post(int16_t x, int16_t y, int16_t w, int16_t h,
                 const uint16_t *rgb565) {
    gif_x = x;
    gif_y = y;
    gif_w = w;
    gif_h = h;
    gif_px = rgb565;
    output_preempt = 1;
}
#endif

static void wait_display(void) {
    uint32_t start = hal_tick_us();
    while (hal_display_busy()) {
        ui_service();
        hal_idle();
    }
    stats.wait_us += hal_tick_us() - start;
}

static void flush_gif(void) {
#if defined(AT32_SLIM_ROM)
    (void)gif_x; (void)gif_y; (void)gif_w; (void)gif_h; (void)gif_px;
    if (!output_preempt) return;
    wait_display();
    invalidated = 1;
    output_preempt = 0;
#else
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
    if (gif_px && gif_w > 0 && gif_h > 0 && x1 > x0 && buffers[0]) {
        /* Full-width rows: narrow column windows mis-address this panel. */
        for (y = y0; y < y1; y++) {
            memset(buffers[0], 0, (unsigned)UI_W * 2u);
            memcpy(buffers[0] + x0, gif_px + (y - gif_y) * gif_w + x0 - gif_x,
                   (unsigned)(x1 - x0) * 2u);
            hal_display_flush(0, (int16_t)y, UI_W, 1, buffers[0]);
            wait_display();
        }
    }
    gif_px = 0;
    gif_w = 0;
    output_preempt = 0;
#endif
}

void ui_draw_flush(void) {
    int y, n, next_buffer = 0;
    int snapshot, heal = 0;
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
    if (!snapshot && heal_pending) {
        int any = 0;
        for (y = 0; y < BAND_N; y++) {
            if (dirty[y].x1 > dirty[y].x0) { any = 1; break; }
        }
        if (!any) {
            /* One full repaint when motion settles, so panel write errors
               cannot persist in regions that stop updating. */
            prepare(1);
            heal = 1;
        }
    }
    for (y = 0; y < UI_H; y += UI_BAND_H) {
        int first = 0;
        int w, h = UI_H - y;
        if (output_preempt && !snapshot) {
            invalidated = 1;
            break;
        }
        if (h > UI_BAND_H) h = UI_BAND_H;
        if (dirty[y / UI_BAND_H].x1 <= dirty[y / UI_BAND_H].x0) continue;
        /* Full-width bands: narrow column windows mis-address this panel. */
        clip_x0 = 0;
        clip_x1 = UI_W;
        clip_y1 = y + h;
        w = UI_W;
        int skip_clear = 0;
        band = buffers[next_buffer];
        t0 = hal_tick_us();
        for (n = 0; n < item_n; n++) {
            const draw_item_t *it = &items[n];
            int opaque_cov = (it->kind == KIND_FILL && it->opacity == 255) ||
                (it->kind == KIND_EXT_IMAGE && it->opacity == 255 && !it->color &&
                 it->pal_n >= UI_EXT_FMT_RGB565);
            if (opaque_cov &&
                it->tf_ox + it->x * it->tf_s / 256 <= clip_x0 &&
                it->tf_oy + it->y * it->tf_s / 256 <= y &&
                it->tf_ox + (it->x + it->w - 1) * it->tf_s / 256 + 1 >= clip_x1 &&
                it->tf_oy + (it->y + it->h - 1) * it->tf_s / 256 + 1 >= clip_y1) {
                first = n;
                skip_clear = 1;
            }
        }
        if (!skip_clear) {
            memset(band, 0, (unsigned)h * (unsigned)UI_W * 2u);
        }
        for (n = first; n < item_n; n++) {
            const bounds_t *b = &history[n].box;
            if (b->x0 >= clip_x1 || b->x1 <= clip_x0 || b->y0 >= clip_y1 || b->y1 <= y) continue;
            switch (items[n].kind) {
            case KIND_FILL: raster_fill(&items[n], y); break;
            case KIND_ROUND: raster_round(&items[n], y); break;
            case KIND_LINE: raster_line(&items[n], y); break;
            case KIND_TEXT: raster_text(&items[n], y); break;
#if !defined(AT32_SLIM_ROM)
            case KIND_SVG: raster_svg(&items[n], y); break;
#endif
            case KIND_IMAGE: raster_image(&items[n], y); break;
            case KIND_MASK: raster_mask(&items[n], y); break;
            case KIND_MASK1: raster_mask1(&items[n], y); break;
            case KIND_RIPPLE: raster_ripple(&items[n], y); break;
            case KIND_EXT_IMAGE: raster_ext_image(&items[n], y); break;
            }
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
    heal_pending = (stats.bands > 0 && !heal && !snapshot);
    stats.frame_us = hal_tick_us() - start;
}
