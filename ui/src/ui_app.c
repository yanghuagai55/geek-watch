#include "ui_internal.h"
#include "ui_menu.h"

#include "hal.h"

#include <stdio.h>

/* 最外层左右翻的是页面本身，文字和图标直接画在背景上。
   进入下一级时，新页面从右往左盖上来，上一页缩小并略向左退。 */

enum {
    HOME_N = 4,
    SET_N = 3,
    DETAIL_N = 3,
    MSG_N = 3,
    M_BADGE_W = 680,
    M_BADGE_H = 220,
    M_BADGE_GAP = 16,
    M_DOT_LIFT = 50,
    M_DOT_GAP = 110,
    M_HEADER = 150,
    M_CARD_W = 840,
    M_CARD_H = 210,
    M_PITCH = 240
};

typedef struct app {
    int focus;
    ui_obj_t scroll;
    ui_menu_t settings;
    ui_menu_t messages;
    ui_obj_t overlay;
    int debug;
    uint8_t prev_keys;
    uint32_t key2_down;
    int key2_long;
    uint32_t dot_ms;
    int dot_page;
} app_t;

static app_t *st;

static uint16_t col_bg;
static uint16_t col_text;
static uint16_t col_dim;
static uint16_t col_blue;
static uint16_t col_blue_dim;
static uint16_t col_ring;
static uint16_t col_badge;
static uint16_t col_dot;

static const char *const set_name[SET_N] = {"调试模式", "亮度", "关于"};
static const char *const bright_name[DETAIL_N] = {"低", "中", "高"};
static const char *const about_name[DETAIL_N] = {"版本", "表盘", "运动"};

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3));
}

static int badge_h(void) {
    int h = UI_MY(M_BADGE_H);
    return h < 28 ? 28 : h;
}

static int badge_y(void) {
    return UI_H - badge_h() - UI_MY(M_BADGE_GAP);
}

static int dot_h(void) {
    int h = UI_MY(40);
    return h < 6 ? 6 : h;
}

static int dots_y(void) {
    int gap = UI_MY(40);
    int base;

    if (gap < 6) {
        gap = 6;
    }
    /* 调试角标没打开时，页面点贴着底边。打开后才抬到角标上面。 */
    base = (st && st->debug) ? badge_y() : UI_H - 4;
    return base - gap - dot_h();
}

static int content_bottom(void) {
    int y;

    if (st && st->debug) {
        y = dots_y() - 4;
        return y > 0 ? y : 0;
    }
    return UI_H - 6;
}

static int title_header_h(void) {
    int h = ui_song_height(UI_SONG_TITLE) + UI_MY(20);
    int min_h = UI_MY(M_HEADER);
    return h > min_h ? h : min_h;
}

static void list_place(int header, int *y0, int *pitch) {
    *pitch = UI_MY(M_PITCH);
    if (*pitch < 24) {
        *pitch = 24;
    }
    *y0 = header + (UI_H - header) / 2;
}

static int mail_scale(void) {
    int s = UI_MX(750) / 48;
    return s < 1 ? 1 : s;
}

static void home_icon_rect(int *x, int *y, int *w, int *h) {
    int s = mail_scale();
    *w = 48 * s;
    *h = 28 * s;
    *x = (UI_W - *w) / 2;
    *y = (content_bottom() - *h - ui_song_height(UI_SONG_TITLE) - UI_MY(28)) / 2;
    if (*y < UI_MY(16)) {
        *y = UI_MY(16);
    }
}

static int vis_index(int scroll, int n) {
    int vis = (scroll + 128) / 256;
    if (vis < 0) {
        vis = 0;
    }
    if (vis >= n) {
        vis = n - 1;
    }
    return vis;
}

static void slide_to(ui_obj_t *obj, int *focus, int dir, int n) {
    int next = *focus + dir;
    if (next < 0 || next >= n) {
        return;
    }
    ui_anim_play_range(UI_ANIM_STACK, obj, obj->x, (int16_t)(next * 256));
    *focus = next;
}

static void blue_card(int x, int y, int w, int h, int hot, int radius, uint8_t a) {
    if (w < 8 || h < 8 || a == 0) {
        return;
    }
    if (hot) {
        ui_draw_round((int16_t)(x - 3), (int16_t)(y - 3),
                      (int16_t)(w + 6), (int16_t)(h + 6),
                      (int16_t)(radius + 3), col_ring, a);
    }
    ui_draw_round((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                  (int16_t)radius, hot ? col_blue : col_blue_dim, a);
}

static int run_frame(void);
static void draw_face_app(uint8_t a);
static void draw_sport_app(uint8_t a);
static void draw_msg_app(uint8_t a);
static void draw_settings_app(uint8_t a);
static void draw_detail(uint8_t a);
static void draw_mail_icon(void);
static void draw_gear_icon(void);

static void draw_outer_pages(int scale, int extra_dx) {
    int i;
    int margin_x = (UI_W - UI_W * scale / 256) / 2;
    int margin_y = (UI_H - UI_H * scale / 256) / 2;

    for (i = 0; i < HOME_N; i++) {
        int page_ox = (i * 256 - st->scroll.x) * UI_W / 256;
        if (page_ox <= -UI_W || page_ox >= UI_W) {
            continue;
        }
        ui_draw_xform(scale, extra_dx + margin_x + page_ox * scale / 256, margin_y);
        if (i == UI_PAGE_SPORT) {
            draw_sport_app(255);
        } else if (i == UI_PAGE_MSG) {
            draw_mail_icon();
        } else if (i == UI_PAGE_SETTINGS) {
            draw_gear_icon();
        } else {
            draw_face_app(255);
        }
    }
}

static void draw_dots(uint8_t alpha) {
    int selected = vis_index(st->scroll.x, HOME_N);
    int gap = UI_MX(M_DOT_GAP);
    int h = dot_h();
    int x0;
    int i;

    if (alpha == 0) {
        return;
    }
    if (gap < 8) {
        gap = 8;
    }
    x0 = (UI_W - (HOME_N - 1) * gap) / 2;
    for (i = 0; i < HOME_N; i++) {
        int hot = i == selected;
        int w = hot ? UI_MX(62) : UI_MX(32);
        int x = x0 + i * gap - (hot ? UI_MX(16) : 0);
        if (w < 3) {
            w = 3;
        }
        ui_draw_round((int16_t)x, (int16_t)dots_y(), (int16_t)w, (int16_t)h,
                      (int16_t)(h / 2), hot ? col_ring : col_dot, alpha);
    }
}

static void stack_box(int scroll, int index, int y0, int pitch,
                      int *x, int *y, int *w, int *h) {
    int delta = index * 256 - scroll;
    int ad = delta < 0 ? -delta : delta;
    int inset = ad / 40;

    *w = UI_MX(M_CARD_W) - inset;
    *h = UI_MY(M_CARD_H) - inset / 3;
    if (*h < UI_MY(150)) {
        *h = UI_MY(150);
    }
    if (*w < UI_MX(600)) {
        *w = UI_MX(600);
    }
    *x = (UI_W - *w) / 2;
    *y = y0 + (delta * pitch) / 256 - *h / 2;
}

static void draw_labeled_card(int x, int y, int w, int h, int hot,
                              const char *name, const char *side, uint8_t a) {
    int16_t tw;

    blue_card(x, y, w, h, hot, 10, a);
    if (side) {
        int16_t sw = ui_text_width(side, 1);
        ui_draw_text((int16_t)(x + UI_MX(60)), (int16_t)(y + (h - 16) / 2),
                     name, col_text, a, 1);
        ui_draw_text((int16_t)(x + w - sw - UI_MX(60)), (int16_t)(y + (h - 16) / 2),
                     side, col_text, a, 1);
        return;
    }
    tw = ui_text_width(name, 1);
    ui_draw_text((int16_t)(x + (w - tw) / 2), (int16_t)(y + (h - 16) / 2),
                 name, col_text, a, 1);
}

static void draw_stack(int scroll, int n, int focus_vis, int y0, int pitch,
                       const char *const *names, const char *side0, uint8_t a) {
    int i;
    int x, y, w, h;

    for (i = 0; i < n; i++) {
        if (i == focus_vis) {
            continue;
        }
        stack_box(scroll, i, y0, pitch, &x, &y, &w, &h);
        draw_labeled_card(x, y, w, h, 0, names[i], 0, a);
    }
    stack_box(scroll, focus_vis, y0, pitch, &x, &y, &w, &h);
    draw_labeled_card(x, y, w, h, 1, names[focus_vis],
                      focus_vis == 0 ? side0 : 0, a);
}

static void draw_header(const char *title, uint8_t a, int song) {
    int band = song ? title_header_h() : UI_MY(M_HEADER);
    int y = song ? (band - ui_song_height(UI_SONG_TITLE)) / 2 : UI_MY(30);

    if (y < 0) {
        y = 0;
    }
    ui_draw_fill(0, 0, UI_W, (int16_t)band, col_bg, a);
    if (song) {
        ui_song_draw(UI_SONG_TITLE, (int16_t)UI_MX(40), (int16_t)y, title, col_text, a);
        return;
    }
    ui_draw_text((int16_t)UI_MX(60), (int16_t)y, title, col_text, a, 1);
}

static void draw_face_pair(int x, int y, int value, uint8_t a) {
    char text[3];

    text[0] = (char)('0' + (value / 10) % 10);
    text[1] = (char)('0' + value % 10);
    text[2] = 0;
    ui_song_draw(UI_SONG_CLOCK, (int16_t)x, (int16_t)y, text, col_text, a);
}

static void draw_face_app(uint8_t a) {
    hal_clock_t clock;
    int gap_y = UI_MY(70);
    int row_w = ui_song_width(UI_SONG_CLOCK, "00");
    int row_h = ui_song_height(UI_SONG_CLOCK);
    int block_h;
    int x0;
    int y0;
    int area_top = UI_MY(20);
    int area_bot = content_bottom();
    int dot;
    int cx;
    int mid;

    if (gap_y < 10) {
        gap_y = 10;
    }
    block_h = row_h * 2 + gap_y;
    x0 = (UI_W - row_w) / 2;
    if (area_top + block_h > area_bot) {
        gap_y = area_bot - area_top - row_h * 2;
        if (gap_y < 4) {
            gap_y = 4;
        }
        block_h = row_h * 2 + gap_y;
    }
    y0 = area_top + (area_bot - area_top - block_h) / 2;
    if (y0 < 0) {
        y0 = 0;
    }
    dot = gap_y / 4;
    if (dot < 3) {
        dot = 3;
    }
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    hal_clock_get(&clock);
    draw_face_pair(x0, y0, clock.hour, a);
    draw_face_pair(x0, y0 + row_h + gap_y, clock.minute, a);
    if (link_media_lyric()[0]) {
        int text_y = y0 + block_h + 2;
        char line[40];
        if (text_y + 16 < content_bottom()) {
            snprintf(line, sizeof line, "%s %s",
                     link_media_playing() ? "Play" : "Pause",
                     link_media_lyric());
            ui_text_draw(4, (int16_t)text_y, line, col_dim, a, 1);
        }
    }
    cx = x0 + row_w / 2 - dot / 2;
    mid = y0 + row_h + gap_y / 2;
    ui_draw_round((int16_t)cx, (int16_t)(mid - dot - 2), (int16_t)dot, (int16_t)dot,
                  (int16_t)(dot / 2), col_text, a);
    ui_draw_round((int16_t)cx, (int16_t)(mid + 2), (int16_t)dot, (int16_t)dot,
                  (int16_t)(dot / 2), col_text, a);
}

/* 八拍跑步，关节和 ui/run.svg 一致。脚着地时 y=42。 */
#define SVG_LINE(x0, y0, x1, y1) { (int8_t)(x0), (int8_t)(y0), (int8_t)(x1), (int8_t)(y1), 0 }
#define SVG_DOT(x, y, r) { (int8_t)(x), (int8_t)(y), 0, 0, (int8_t)(r) }
#define RUN_POSE(hx, hy, sx, sy, px, py, k1x, k1y, f1x, f1y, k2x, k2y, f2x, f2y, e1x, e1y, h1x, h1y, e2x, e2y, h2x, h2y) \
    SVG_LINE(sx, sy, px, py), SVG_LINE(px, py, k1x, k1y), SVG_LINE(k1x, k1y, f1x, f1y), \
    SVG_LINE(px, py, k2x, k2y), SVG_LINE(k2x, k2y, f2x, f2y), \
    SVG_LINE(sx, sy, e1x, e1y), SVG_LINE(e1x, e1y, h1x, h1y), \
    SVG_LINE(sx, sy, e2x, e2y), SVG_LINE(e2x, e2y, h2x, h2y), \
    SVG_DOT(hx, hy, 3)

static const ui_svg_cmd_t run_svg[8][10] = {
    { RUN_POSE(20, 6, 19, 12, 16, 22, 24, 32, 30, 42, 10, 30, 4, 40, 26, 16, 28, 22, 12, 18, 6, 26) },
    { RUN_POSE(19, 8, 18, 14, 16, 25, 22, 34, 28, 42, 12, 28, 8, 36, 24, 18, 26, 26, 14, 18, 10, 26) },
    { RUN_POSE(18, 10, 18, 16, 16, 28, 20, 36, 26, 42, 14, 30, 12, 34, 22, 20, 22, 28, 14, 20, 12, 28) },
    { RUN_POSE(19, 7, 19, 12, 16, 23, 25, 26, 21, 34, 7, 30, 2, 41, 23, 15, 25, 20, 13, 17, 8, 25) },
    { RUN_POSE(20, 5, 20, 11, 17, 21, 27, 23, 24, 32, 5, 27, 0, 37, 24, 13, 26, 18, 14, 16, 9, 24) },
    { RUN_POSE(21, 4, 21, 10, 18, 18, 28, 21, 30, 31, 8, 22, 2, 30, 28, 11, 32, 15, 11, 14, 4, 21) },
    { RUN_POSE(22, 3, 22, 9, 18, 17, 28, 22, 26, 32, 8, 20, 4, 28, 28, 10, 32, 14, 10, 14, 4, 22) },
    { RUN_POSE(21, 5, 20, 11, 17, 21, 26, 30, 30, 40, 10, 26, 6, 36, 28, 14, 30, 20, 12, 16, 6, 24) }
};

#undef RUN_POSE
#undef SVG_DOT
#undef SVG_LINE

#define SCENE_SKY 0x551B
#define SCENE_SUN 0xFE84
#define SCENE_CLD 0xDF7F
#define SCENE_HILL 0x3668
#define SCENE_GRN 0x2C66
#define S16(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) \
    a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p
static const uint16_t scene_px[16 * 12] = {
    S16(SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY),
    S16(SCENE_SKY, SCENE_SKY, SCENE_CLD, SCENE_CLD, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SUN, SCENE_SUN, SCENE_SUN, SCENE_SKY, SCENE_SKY, SCENE_SKY),
    S16(SCENE_SKY, SCENE_SKY, SCENE_CLD, SCENE_CLD, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SUN, SCENE_SUN, SCENE_SUN, SCENE_SKY, SCENE_SKY, SCENE_SKY),
    S16(SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SUN, SCENE_SUN, SCENE_SUN, SCENE_SKY, SCENE_SKY, SCENE_SKY),
    S16(SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY),
    S16(SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY, SCENE_SKY),
    S16(SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL),
    S16(SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL, SCENE_HILL),
    S16(SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN),
    S16(SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN),
    S16(SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN),
    S16(SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN, SCENE_GRN)
};
#undef S16
#undef SCENE_GRN
#undef SCENE_HILL
#undef SCENE_CLD
#undef SCENE_SUN
#undef SCENE_SKY

static const ui_svg_cmd_t mail_svg[] = {
    {0, 0, 48, 0, 0},
    {48, 0, 48, 28, 0},
    {48, 28, 0, 28, 0},
    {0, 28, 0, 0, 0},
    {0, 0, 24, 16, 0},
    {48, 0, 24, 16, 0}
};

static int run_frame(void) {
    return (int)((hal_tick_ms() / 70u) % 8u);
}

static void draw_sport_app(uint8_t a) {
    int frame = run_frame();
    int scale = UI_MY(560) / 42;
    int ground = content_bottom() - 2;
    int shift;
    int x;
    int ox;
    int oy;

    if (scale < 1) {
        scale = 1;
    }
    shift = (frame * UI_MX(80)) & 15;
    {
        int top = UI_MY(8) + ui_song_height(UI_SONG_TITLE) + UI_MY(12);
        int room = ground - top;
        int fit = room / 42;
        if (fit < 1) {
            fit = 1;
        }
        if (scale > fit) {
            scale = fit;
        }
    }
    ox = (UI_W - 32 * scale) / 2;
    oy = ground - 42 * scale;
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    ui_song_draw(UI_SONG_TITLE, (int16_t)UI_MX(40), (int16_t)UI_MY(8), "运动", col_text, a);
    ui_svg_draw((int16_t)ox, (int16_t)oy, (uint8_t)scale, run_svg[frame], 10, col_text, a);
    for (x = -shift; x < UI_W; x += UI_MX(125)) {
        ui_draw_line((int16_t)x, (int16_t)ground, (int16_t)(x + UI_MX(60)), (int16_t)ground, col_dim, a);
    }
}

static void draw_mail_icon(void) {
    int x, y, w, h;
    int16_t tw;

    home_icon_rect(&x, &y, &w, &h);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
    ui_svg_draw((int16_t)x, (int16_t)y, (uint8_t)mail_scale(), mail_svg, 6, col_text, 255);
    tw = ui_song_width(UI_SONG_TITLE, "消息");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)(y + h + UI_MY(12)),
                 "消息", col_text, 255);
}

static void draw_gear_icon(void) {
    int x, y, w, h;
    int cx, cy, rad, hole, tooth_d;
    int16_t tw;
    int i;
    static const int8_t tooth[8][2] = {
        {18, -4}, {14, 12}, {-4, 18}, {-16, 12},
        {-22, -4}, {-16, -16}, {-4, -22}, {14, -16}
    };

    home_icon_rect(&x, &y, &w, &h);
    cx = x + w / 2;
    cy = y + h / 2;
    rad = UI_MX(140);
    if (rad > h / 2 - UI_MY(20)) {
        rad = h / 2 - UI_MY(20);
    }
    if (rad < 8) {
        rad = 8;
    }
    hole = rad * 6 / 16;
    tooth_d = rad / 2;
    if (tooth_d < 4) {
        tooth_d = 4;
    }
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
    for (i = 0; i < 8; i++) {
        int tx = cx + tooth[i][0] * rad / 16;
        int ty = cy + tooth[i][1] * rad / 16;
        ui_draw_round((int16_t)(tx - tooth_d / 2), (int16_t)(ty - tooth_d / 2),
                      (int16_t)tooth_d, (int16_t)tooth_d, (int16_t)(tooth_d / 4), col_text, 255);
    }
    ui_draw_round((int16_t)(cx - rad), (int16_t)(cy - rad),
                  (int16_t)(rad * 2), (int16_t)(rad * 2), (int16_t)rad, col_text, 255);
    ui_draw_round((int16_t)(cx - hole), (int16_t)(cy - hole),
                  (int16_t)(hole * 2), (int16_t)(hole * 2), (int16_t)hole, col_bg, 255);
    tw = ui_song_width(UI_SONG_TITLE, "设置");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)(y + h + UI_MY(12)),
                 "设置", col_text, 255);
}

static void draw_msg_app(uint8_t a) {
    int y0;
    int pitch;
    int vis = vis_index(st->messages.scroll.x, MSG_N);

    const char *names[MSG_N];
    int i;

    list_place(title_header_h(), &y0, &pitch);
    for (i = 0; i < MSG_N; i++) {
        names[i] = link_message(i);
    }
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    draw_stack(st->messages.scroll.x, MSG_N, vis, y0, pitch, names, 0, a);
    draw_header("消息", a, 1);
}

static void draw_settings_app(uint8_t a) {
    int y0;
    int pitch;
    int vis = vis_index(st->settings.scroll.x, SET_N);
    const char *side = st->debug ? "开" : "关";

    list_place(title_header_h(), &y0, &pitch);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    draw_stack(st->settings.scroll.x, SET_N, vis, y0, pitch, set_name, side, a);
    draw_header("设置", a, 1);
}

static void draw_message_page(uint8_t a) {
    enum { PW = 16, PH = 12, PS = 4 };
    int x = (UI_W - PW * PS) / 2;
    int y = UI_MY(280);
    const char *caption = link_message(st->messages.child);
    int16_t tw = ui_text_width(caption, 1);

    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    ui_image_draw((int16_t)x, (int16_t)y, PW, PH, PS, scene_px, a);
    ui_text_draw((int16_t)((UI_W - tw) / 2), (int16_t)(y + PH * PS + UI_MY(40)),
                 caption, col_text, a, 1);
    draw_header("消息", a, 0);
}

static void draw_detail(uint8_t a) {
    int kind = st->settings.level == UI_MENU_CHILD ? st->settings.child : -1;
    int scroll = st->settings.child_scroll.x;
    int y0;
    int pitch;
    const char *const *names = kind == 1 ? bright_name : about_name;
    const char *title = kind == 1 ? "亮度" : "关于";

    if (a == 0) {
        return;
    }
    if (st->messages.level == UI_MENU_CHILD) {
        draw_message_page(a);
        return;
    }
    list_place(UI_MY(M_HEADER), &y0, &pitch);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    draw_stack(scroll, DETAIL_N, vis_index(scroll, DETAIL_N), y0, pitch, names, 0, a);
    draw_header(title, a, 0);
}

static void draw_badge(void) {
    uint32_t used = 0;
    uint32_t total = 0;
    uint16_t cpu;
    char ram[24];
    char cpu_line[24];
    int16_t y;

    if (!st || st->overlay.opacity == 0) {
        return;
    }
    ui_mem_get(&used, &total);
    cpu = hal_cpu_tenths();
    if (cpu > 1000) {
        cpu = 1000;
    }
    snprintf(ram, sizeof ram, "RAM %lu", (unsigned long)used);
    snprintf(cpu_line, sizeof cpu_line, "CPU %u.%u%%", cpu / 10u, cpu % 10u);
    y = (int16_t)badge_y();
    ui_draw_round((int16_t)UI_MX(16), y, (int16_t)UI_MX(M_BADGE_W), (int16_t)badge_h(),
                  6, col_badge, st->overlay.opacity);
    ui_draw_text((int16_t)(UI_MX(16) + 4), (int16_t)(y + 2), ram, col_ring, st->overlay.opacity, 1);
    ui_draw_text((int16_t)(UI_MX(16) + 4), (int16_t)(y + 18), cpu_line, col_ring, st->overlay.opacity, 1);
}

static ui_menu_t *live_menu(void) {
    if (ui_menu_level(&st->settings) > UI_MENU_ICON || st->settings.zoom.scale > 0) {
        return &st->settings;
    }
    if (ui_menu_level(&st->messages) > UI_MENU_ICON || st->messages.zoom.scale > 0) {
        return &st->messages;
    }
    if (st->focus == UI_PAGE_SETTINGS) {
        return &st->settings;
    }
    if (st->focus == UI_PAGE_MSG) {
        return &st->messages;
    }
    return 0;
}

static void on_confirm(ui_menu_t *menu, int is_settings) {
    int result = ui_menu_confirm(menu);

    if (result != UI_MENU_ROW) {
        return;
    }
    if (is_settings) {
        if (menu->focus == 0) {
            ui_settings_set_debug(!ui_settings_get_debug());
        } else {
            ui_menu_enter_child(menu, DETAIL_N);
            ui_trace_emit(menu->child == 1 ? "ui_nav_goto(bright)" : "ui_nav_goto(about)");
        }
        return;
    }
    ui_menu_enter_child(menu, 1);
    ui_trace_emit("ui_nav_goto(message)");
}

static void on_key(uint8_t pressed, int long_enter) {
    ui_menu_t *menu;

    if (!st) {
        return;
    }
    menu = live_menu();
    if (long_enter) {
        if (menu && ui_menu_level(menu) > UI_MENU_ICON) {
            ui_menu_back(menu);
        }
        return;
    }
    if (!pressed) {
        return;
    }
    if (menu && ui_menu_level(menu) > UI_MENU_ICON) {
        if (pressed & HAL_KEY1) {
            ui_menu_prev(menu);
        }
        if (pressed & HAL_KEY3) {
            ui_menu_next(menu);
        }
        if (pressed & HAL_KEY2) {
            on_confirm(menu, menu == &st->settings);
        }
        return;
    }
    if (pressed & HAL_KEY1) {
        slide_to(&st->scroll, &st->focus, -1, HOME_N);
    }
    if (pressed & HAL_KEY3) {
        slide_to(&st->scroll, &st->focus, 1, HOME_N);
    }
    if ((pressed & HAL_KEY2) && !menu && st->focus == UI_PAGE_FACE) {
        link_media_toggle();
        return;
    }
    if (pressed & HAL_KEY2) {
        on_confirm(menu, menu == &st->settings);
    }
}

void ui_init(void) {
    ui_mem_init();
    ui_anim_init();
    ui_draw_init();
    st = ui_mem_alloc(sizeof *st, 8);

    col_bg = rgb565(6, 12, 28);
    col_text = rgb565(236, 244, 255);
    col_dim = rgb565(150, 190, 230);
    col_blue = rgb565(0, 114, 230);
    col_blue_dim = rgb565(14, 42, 84);
    col_ring = rgb565(150, 210, 255);
    col_badge = rgb565(4, 18, 40);
    col_dot = rgb565(110, 160, 210);
    ui_menu_init(&st->settings, SET_N);
    ui_menu_init(&st->messages, MSG_N);
    link_init();
    ui_trace_emit("ui_init");
}

void ui_nav_goto(ui_page_id_t page) {
    if (!st || ui_menu_level(&st->settings) > UI_MENU_ICON ||
        ui_menu_level(&st->messages) > UI_MENU_ICON || (unsigned)page >= HOME_N) {
        return;
    }
    st->focus = (int)page;
    st->scroll.x = (int16_t)((int)page * 256);
}

void ui_settings_set_debug(int enabled) {
    if (!st) {
        return;
    }
    enabled = enabled ? 1 : 0;
    if (enabled == st->debug) {
        return;
    }
    st->debug = enabled;
    ui_trace_emit(enabled ? "ui_settings_set_debug(1)" : "ui_settings_set_debug(0)");
    ui_anim_play(enabled ? UI_ANIM_FADE_IN : UI_ANIM_FADE_OUT, &st->overlay);
}

int ui_settings_get_debug(void) {
    return st ? st->debug : 0;
}

static void draw_open_window(ui_menu_t *menu, int is_settings) {
    int z = menu->zoom.scale;
    int ix, iy, iw, ih;
    int x, y, w, h;
    home_icon_rect(&ix, &iy, &iw, &ih);
    x = ix + (0 - ix) * z / 255;
    y = iy + (0 - iy) * z / 255;
    w = iw + (UI_W - iw) * z / 255;
    h = ih + (UI_H - ih) * z / 255;
    int sc = w * 256 / UI_W;

    if (z <= 0) {
        return;
    }
    if (sc < 1) {
        sc = 1;
    }
    if (z < 255) {
        draw_outer_pages(256, 0);
    }
    ui_draw_xform(256, 0, 0);
    ui_draw_round((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                  (int16_t)(16 * (255 - z) / 255), col_bg, 255);
    ui_draw_xform(sc, x, y);
    if (is_settings) {
        draw_settings_app(255);
    } else {
        draw_msg_app(255);
    }
}

static void draw_pushed(ui_menu_t *menu, int is_settings) {
    int t = menu->push.x;
    int parent_s;
    int parent_dx;
    int margin_x;
    int margin_y;

    if (t < 0) {
        t = 0;
    }
    if (t > 256) {
        t = 256;
    }
    parent_s = 256 - t * 36 / 256;
    parent_dx = -t * 28 / 256;
    margin_x = (UI_W - UI_W * parent_s / 256) / 2;
    margin_y = (UI_H - UI_H * parent_s / 256) / 2;
    ui_draw_xform(parent_s, parent_dx + margin_x, margin_y);
    if (is_settings) {
        draw_settings_app(255);
    } else {
        draw_msg_app(255);
    }
    ui_draw_xform(parent_s, parent_dx + margin_x, margin_y);
    ui_draw_fill(0, 0, UI_W, UI_H, 0, (uint8_t)(t * 100 / 256));
    ui_draw_xform(256, UI_W - t * UI_W / 256, 0);
    draw_detail(255);
}

static int dot_alpha(uint32_t now) {
    int page = vis_index(st->scroll.x, HOME_N);
    int moving = ui_anim_busy(&st->scroll);
    uint32_t idle;

    if (moving || page != st->dot_page) {
        st->dot_ms = now;
        st->dot_page = page;
    }
    if (!moving && page == UI_PAGE_FACE) {
        return 0;
    }
    idle = now - st->dot_ms;
    if (idle < 1400u) {
        return 255;
    }
    if (idle >= 1800u) {
        return 0;
    }
    return (int)(255u - (idle - 1400u) * 255u / 400u);
}

void ui_frame(void) {
    uint8_t level;
    uint8_t pressed;
    uint8_t released;
    uint8_t extra_down = 0;
    uint8_t extra_up = 0;
    uint32_t now;
    int long_enter = 0;
    ui_menu_t *menu;

    if (!st) {
        return;
    }
    hal_meter_begin();
    link_poll();
    now = hal_tick_ms();
    level = hal_key_level();
    if (st->debug) {
        level = (uint8_t)(level | link_key_level());
        link_key_edges(&extra_down, &extra_up);
    }
    pressed = (uint8_t)((level & (uint8_t)~st->prev_keys) | extra_down);
    released = (uint8_t)((st->prev_keys & (uint8_t)~level) | extra_up);
    st->prev_keys = level;
    if (pressed & HAL_KEY2) {
        st->key2_down = now;
        st->key2_long = 0;
    }
    if ((level & HAL_KEY2) && !st->key2_long && now - st->key2_down >= 450u) {
        st->key2_long = 1;
        long_enter = 1;
    }
    if ((released & HAL_KEY2) && !st->key2_long) {
        pressed = (uint8_t)(pressed | HAL_KEY2);
    } else {
        pressed = (uint8_t)(pressed & (0xFFu ^ HAL_KEY2));
    }
    on_key(pressed, long_enter);
    ui_anim_tick(now);
    ui_menu_tick(&st->settings);
    ui_menu_tick(&st->messages);

    menu = 0;
    if (ui_menu_level(&st->settings) > UI_MENU_ICON || st->settings.zoom.scale > 0) {
        menu = &st->settings;
    } else if (ui_menu_level(&st->messages) > UI_MENU_ICON || st->messages.zoom.scale > 0) {
        menu = &st->messages;
    }
    ui_draw_begin();
    if (menu && menu->level == UI_MENU_CHILD) {
        draw_pushed(menu, menu == &st->settings);
    } else if (menu && menu->zoom.scale > 0) {
        draw_open_window(menu, menu == &st->settings);
    } else {
        draw_outer_pages(256, 0);
        draw_dots((uint8_t)dot_alpha(now));
    }
    ui_draw_xform(256, 0, 0);
    draw_badge();
    ui_draw_flush();
    link_ack_flush();
    hal_meter_end();
    hal_frame_pace();
}
