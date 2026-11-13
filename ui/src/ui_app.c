#include "ui_internal.h"
#include "ui_menu.h"
#include "ui_icons.h"

#include "hal.h"
#include "sport.h"
#include "music.h"
#include "settings.h"

#include <stdio.h>

/* 最外层左右翻的是页面本身，文字和图标直接画在背景上。
   进入下一级时，新页面从右往左盖上来，上一页缩小并略向左退。 */

enum {
    HOME_N = 5,
    SET_N = 6,
    DETAIL_N = 3,
    ABOUT_N = 6,
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
    ui_menu_t sport;
    ui_menu_t music;
    ui_obj_t overlay;
    int debug;
    uint8_t prev_keys;
    uint32_t key2_down;
    int key2_long;
    uint32_t dot_ms;
    int dot_page;
    uint32_t fps_window;
    uint16_t fps_frames;
    uint8_t fps;
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
static uint16_t col_accent;

static const char *const set_name[SET_N] = {"调试模式", "抬腕亮屏", "按键音", "亮度", "蓝牙", "关于"};
static const char *const bright_name[DETAIL_N] = {"低", "中", "高"};

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3));
}

static int badge_h(void) {
    return 18;   /* 单行，只压页面点，不顶内容 */
}

static int badge_y(void) {
    return UI_H - badge_h() - 2;
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
    /* 布局与调试角标无关：角标是盖在上面的，绝不推动内容。 */
    base = UI_H - 4;
    return base - gap - dot_h();
}

static int content_bottom(void) {
    /* 固定，不随调试角标变化 */
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

static int wrap_mod(int v, int m) {
    v %= m;
    if (v < 0) {
        v += m;
    }
    return v;
}

static int vis_index(int scroll, int n) {
    int idx = wrap_mod(scroll, n * 256);
    return ((idx + 128) / 256) % n;
}

static void slide_to(ui_obj_t *obj, int *focus, int dir, int n) {
    int next = *focus + dir;
    int span = n * 256;
    int target;
    if (next < 0) {
        next = n - 1;
    } else if (next >= n) {
        next = 0;
    }
    /* Aim at the wrapped copy of the destination nearest the current scroll so
       the seam crossing takes the short way; settling renormalises to focus. */
    target = next * 256;
    while (target - obj->x > span / 2) {
        target -= span;
    }
    while (obj->x - target > span / 2) {
        target += span;
    }
    ui_anim_play_range(UI_ANIM_STACK, obj, obj->x, (int16_t)target);
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
static void draw_sport_child(uint8_t a);
static void draw_msg_app(uint8_t a);
static void draw_settings_app(uint8_t a);
static void draw_detail(uint8_t a);
static void draw_mail_icon(void);
static void draw_gear_icon(void);
static void draw_sport_icon(void);
static void draw_music_icon(void);

static void draw_outer_pages(int scale, int extra_dx) {
    int i;
    int margin_x = (UI_W - UI_W * scale / 256) / 2;
    int margin_y = (UI_H - UI_H * scale / 256) / 2;

    for (i = 0; i < HOME_N; i++) {
        int delta = i * 256 - st->scroll.x;
        int page_ox;
        /* Nearest wrapped copy so the home ring is seamless in both directions. */
        delta = wrap_mod(delta + HOME_N * 128, HOME_N * 256) - HOME_N * 128;
        page_ox = delta * UI_W / 256;
        if (page_ox <= -UI_W || page_ox >= UI_W) {
            continue;
        }
        ui_draw_xform(scale, extra_dx + margin_x + page_ox * scale / 256, margin_y);
        if (i == UI_PAGE_SPORT) {
            draw_sport_icon();
        } else if (i == UI_PAGE_MSG) {
            draw_mail_icon();
        } else if (i == UI_PAGE_SETTINGS) {
            draw_gear_icon();
        } else if (i == UI_PAGE_MUSIC) {
            draw_music_icon();
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

static void draw_face_pair(int x, int y, int value, uint16_t color, uint8_t a) {
    char text[3];

    text[0] = (char)('0' + (value / 10) % 10);
    text[1] = (char)('0' + value % 10);
    text[2] = 0;
    ui_song_draw(UI_SONG_CLOCK, (int16_t)x, (int16_t)y, text, color, a);
}

static int face_weekday(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y--;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void draw_face_app(uint8_t a) {
    static const char *const wd[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    hal_clock_t clock;
    int row_w = ui_song_width(UI_SONG_CLOCK, "00");
    int row_h = ui_song_height(UI_SONG_CLOCK);
    int area_top = UI_MY(20);
    int area_bot = content_bottom();
    int top = area_top + 6;
    int bot = area_bot - 6;
    int gap_y;
    int block_h;
    int x0;
    int y0;
    int dot;
    int cx;
    int mid;
    uint32_t now = hal_tick_ms();
    uint32_t phase;
    uint32_t t;
    uint8_t colon_a;
    char line[40];
    int16_t tw;

    hal_clock_get(&clock);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);

    if (link_media_lyric()[0]) {
        snprintf(line, sizeof line, "%s %s",
                 link_media_playing() ? "Play" : "Pause", link_media_lyric());
    } else {
        snprintf(line, sizeof line, "%s %02u-%02u",
                 wd[face_weekday(clock.year, clock.month, clock.day)],
                 clock.month, clock.day);
    }
    tw = ui_text_width(line, 1);
    ui_text_draw((int16_t)((UI_W - tw) / 2), (int16_t)UI_MY(30), line, col_dim, a, 1);

    gap_y = UI_MY(60);
    if (gap_y < 10) {
        gap_y = 10;
    }
    block_h = row_h * 2 + gap_y;
    if (top + block_h > bot) {
        gap_y = bot - top - row_h * 2;
        if (gap_y < 4) {
            gap_y = 4;
        }
        block_h = row_h * 2 + gap_y;
    }
    y0 = top + (bot - top - block_h) / 2;
    if (y0 < 0) {
        y0 = 0;
    }
    x0 = (UI_W - row_w) / 2;
    draw_face_pair(x0, y0, clock.hour, col_text, a);
    draw_face_pair(x0, y0 + row_h + gap_y, clock.minute, col_ring, a);

    phase = now % 2000u;
    t = phase < 1000u ? phase * 256u / 1000u : (2000u - phase) * 256u / 1000u;
    colon_a = (uint8_t)(120 + (int)ui_ease_in_out(t) * (255 - 120) / 255);
    colon_a = (uint8_t)((int)colon_a * a / 255);
    dot = gap_y / 3;
    if (dot < 3) {
        dot = 3;
    }
    cx = x0 + row_w / 2 - dot / 2;
    mid = y0 + row_h + gap_y / 2;
    ui_draw_round((int16_t)cx, (int16_t)(mid - dot - 2), (int16_t)dot, (int16_t)dot,
                  (int16_t)(dot / 2), col_ring, colon_a);
    ui_draw_round((int16_t)cx, (int16_t)(mid + 2), (int16_t)dot, (int16_t)dot,
                  (int16_t)(dot / 2), col_ring, colon_a);
}

/* 跑步采用外部 AI 按 ui/ICON_SVG_PROMPT.md 产出的关键帧（已人工预览验收）。
   ci: 0=主色(橙) 1=白 2=橙。脚着地时 y=42。 */
static const uint16_t icon_pal[] = {0xEFBF, 0xFCE1, 0x969F, 0x0063, 0x039C};
static const ui_svg_cmd_t run_svg[5][10] = {
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

static int run_frame(void) {
    return (int)((hal_tick_ms() / 60u) % 5u);
}

static void draw_sport_app(uint8_t a) {
    sport_stats_t ss;
    uint32_t bars[SPORT_BARS];
    hal_clock_t clk;
    char line[40];
    int title_h = ui_song_height(UI_SONG_TITLE);
    int title_y = dots_y() - title_h - 2;
    int top = UI_MY(24);
    int chart_top = top + UI_MY(280);
    int chart_bot = title_y - UI_MY(110);
    int chart_h = chart_bot - chart_top;
    int x0 = UI_MX(60);
    int x1 = UI_W - UI_MX(60);
    int bw = (x1 - x0) / SPORT_BARS;
    uint32_t max = 1;
    int cur, i, bh, bx, by;
    int16_t tw;

    sport_get(&ss);
    sport_get_bars(bars, SPORT_BARS);
    hal_clock_get(&clk);
    cur = (clk.hour / 2) % SPORT_BARS;
    for (i = 0; i < SPORT_BARS; i++) {
        if (bars[i] > max) max = bars[i];
    }
    if (chart_h < 8) chart_h = 8;

    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    ui_text_draw((int16_t)UI_MX(60), (int16_t)top, "今日步数", col_dim, a, 1);
    snprintf(line, sizeof line, "%lu", (unsigned long)ss.steps);
    tw = ui_text_width(line, 2);
    ui_text_draw((int16_t)((UI_W - tw) / 2), (int16_t)(top + UI_MY(60)), line, col_ring, a, 2);

    for (i = 0; i < SPORT_BARS; i++) {
        bh = (int)(bars[i] * (uint32_t)chart_h / max);
        if (bars[i] > 0 && bh < 2) bh = 2;
        bx = x0 + i * bw + 1;
        by = chart_bot - bh;
        ui_draw_round((int16_t)bx, (int16_t)by, (int16_t)(bw - 2), (int16_t)bh,
                      2, i == cur ? col_accent : col_blue, a);
    }
    ui_draw_line((int16_t)x0, (int16_t)chart_bot, (int16_t)x1, (int16_t)chart_bot, col_dim, a);

    snprintf(line, sizeof line, "%lum  %lukcal",
             (unsigned long)ss.distance_m, (unsigned long)ss.calories);
    tw = ui_text_width(line, 1);
    ui_text_draw((int16_t)((UI_W - tw) / 2), (int16_t)(chart_bot + UI_MY(30)), line, col_dim, a, 1);

    tw = ui_song_width(UI_SONG_TITLE, "运动");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)title_y, "运动", col_text, a);
}

static void draw_mail_icon(void) {
    int x, y, w, h;
    int16_t tw;

    home_icon_rect(&x, &y, &w, &h);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
    ui_icon_mail((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                 hal_tick_ms(), col_ring, col_text, col_bg, 255);
    tw = ui_song_width(UI_SONG_TITLE, "消息");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)(y + h + UI_MY(40)),
                 "消息", col_text, 255);
}

static void draw_gear_icon(void) {
    int x, y, w, h;
    int cx, cy, rad;
    int16_t tw;

    home_icon_rect(&x, &y, &w, &h);
    cx = x + w / 2;
    cy = y + h / 2;
    rad = (w < h ? w : h) / 2;
    if (rad < 8) {
        rad = 8;
    }
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
    ui_icon_gear((int16_t)cx, (int16_t)cy, (int16_t)rad, hal_tick_ms(), col_ring, col_bg, 255);
    tw = ui_song_width(UI_SONG_TITLE, "设置");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)(y + h + UI_MY(40)),
                 "设置", col_text, 255);
}

static void draw_sport_icon(void) {
    int x, y, w, h;
    int scale;
    int16_t tw;

    home_icon_rect(&x, &y, &w, &h);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
    scale = w / 32;
    if (h / 42 < scale) {
        scale = h / 42;
    }
    if (scale < 1) {
        scale = 1;
    }
    ui_svg_draw_p((int16_t)(x + (w - 32 * scale) / 2), (int16_t)(y + (h - 42 * scale) / 2),
                  (uint8_t)scale, run_svg[run_frame()], 10, icon_pal, 5, col_accent, 255,
                  (uint8_t)(scale + 3 > 8 ? 8 : scale + 3));
    tw = ui_song_width(UI_SONG_TITLE, "运动");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)(y + h + UI_MY(40)),
                 "运动", col_text, 255);
}

static void draw_music_icon(void) {
    int x, y, w, h;
    int cx, cy, r;
    int16_t tw;

    home_icon_rect(&x, &y, &w, &h);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
    cx = x + w / 2;
    cy = y + h / 2;
    cy += ((hal_tick_ms() / 250u) % 2u) ? 2 : -2;   /* 音符跳动动画 */
    r = (w < h ? w : h) / 5;
    if (r < 4) {
        r = 4;
    }
    /* 音符：符头+符干+符尾 */
    ui_draw_round((int16_t)(cx - r), (int16_t)cy, (int16_t)(r * 2), (int16_t)(r * 2), r, col_ring, 255);
    ui_draw_line((int16_t)(cx + r), (int16_t)(cy + r), (int16_t)(cx + r), (int16_t)(cy - r * 2), col_ring, 255);
    ui_draw_line((int16_t)(cx + r), (int16_t)(cy - r * 2), (int16_t)(cx + r * 2), (int16_t)(cy - r), col_ring, 255);
    tw = ui_song_width(UI_SONG_TITLE, "音乐");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)(y + h + UI_MY(40)),
                 "音乐", col_text, 255);
}

static void draw_sport_child(uint8_t a) {
    sport_stats_t ss;
    char l0[24];
    const char *names[2];
    int n = st->sport.child_count;
    int scroll = st->sport.child_scroll.x;
    int vis = vis_index(scroll, n);
    int y0, pitch, i, x, y, w, h;

    sport_get(&ss);
    snprintf(l0, sizeof l0, "清零今日 %lu步", (unsigned long)ss.steps);
    names[0] = l0;
    names[1] = "+1000步";
    list_place(UI_MY(M_HEADER), &y0, &pitch);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    for (i = 0; i < n; i++) {
        if (i == vis) {
            continue;
        }
        stack_box(scroll, i, y0, pitch, &x, &y, &w, &h);
        draw_labeled_card(x, y, w, h, 0, names[i], 0, a);
    }
    stack_box(scroll, vis, y0, pitch, &x, &y, &w, &h);
    draw_labeled_card(x, y, w, h, 1, names[vis], 0, a);
    draw_header("运动", a, 0);
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

static void settings_side(int idx, char *buf, int cap) {
    settings_t *s = settings_get();
    const char *v = 0;

    switch (idx) {
    case 0: v = s->debug ? "开" : "关"; break;
    case 1: v = s->raise_wrist ? "开" : "关"; break;
    case 2: v = s->key_beep ? "开" : "关"; break;
    case 3: v = bright_name[s->brightness <= 2 ? s->brightness : 1]; break;
    case 4: v = s->bluetooth ? "开" : "关"; break;
    default: break;
    }
    if (v) {
        snprintf(buf, (size_t)cap, "%s", v);
    } else {
        buf[0] = 0;
    }
}

static void draw_settings_app(uint8_t a) {
    int y0;
    int pitch;
    int vis = vis_index(st->settings.scroll.x, SET_N);
    int i;
    int x, y, w, h;
    char side[8];

    list_place(title_header_h(), &y0, &pitch);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    for (i = 0; i < SET_N; i++) {
        if (i == vis) {
            continue;
        }
        stack_box(st->settings.scroll.x, i, y0, pitch, &x, &y, &w, &h);
        settings_side(i, side, sizeof side);
        draw_labeled_card(x, y, w, h, 0, set_name[i], side[0] ? side : 0, a);
    }
    stack_box(st->settings.scroll.x, vis, y0, pitch, &x, &y, &w, &h);
    settings_side(vis, side, sizeof side);
    draw_labeled_card(x, y, w, h, 1, set_name[vis], side[0] ? side : 0, a);
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

static void draw_about_page(uint8_t a) {
    about_info_t info;
    hal_imu_t imu;
    char lines[ABOUT_N][24];
    const char *names[ABOUT_N];
    int scroll = st->settings.child_scroll.x;
    int vis = vis_index(scroll, ABOUT_N);
    int y0;
    int pitch;
    int i;
    int x, y, w, h;

    about_fill(&info);
    snprintf(lines[0], sizeof lines[0], "版本 %s", info.version);
    snprintf(lines[1], sizeof lines[1], "%s", info.mcu);
    snprintf(lines[2], sizeof lines[2], "%s", info.clock_src);
    if (info.battery_pct == 0xFF) {
        snprintf(lines[3], sizeof lines[3], "USB 供电");
    } else {
        snprintf(lines[3], sizeof lines[3], "%umV %u%%",
                 (unsigned)info.battery_mv, (unsigned)info.battery_pct);
    }
    snprintf(lines[4], sizeof lines[4], "%.11s", info.build);
    snprintf(lines[5], sizeof lines[5], "MPU6050 %s", hal_imu_read(&imu) ? "OK" : "--");
    for (i = 0; i < ABOUT_N; i++) {
        names[i] = lines[i];
    }

    list_place(UI_MY(M_HEADER), &y0, &pitch);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    for (i = 0; i < ABOUT_N; i++) {
        if (i == vis) {
            continue;
        }
        stack_box(scroll, i, y0, pitch, &x, &y, &w, &h);
        draw_labeled_card(x, y, w, h, 0, names[i], 0, a);
    }
    stack_box(scroll, vis, y0, pitch, &x, &y, &w, &h);
    draw_labeled_card(x, y, w, h, 1, names[vis], 0, a);
    draw_header("关于", a, 0);
}

static void draw_detail(uint8_t a) {
    int kind = st->settings.level == UI_MENU_CHILD ? st->settings.child : -1;
    int scroll = st->settings.child_scroll.x;
    int y0;
    int pitch;

    if (a == 0) {
        return;
    }
    if (st->messages.level == UI_MENU_CHILD) {
        draw_message_page(a);
        return;
    }
    if (kind == 5) {
        draw_about_page(a);
        return;
    }
    list_place(UI_MY(M_HEADER), &y0, &pitch);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    draw_stack(scroll, DETAIL_N, vis_index(scroll, DETAIL_N), y0, pitch, bright_name, 0, a);
    draw_header("亮度", a, 0);
}

static void draw_badge(void) {
    uint32_t used = 0;
    uint32_t total = 0;
    uint16_t cpu;
    char ram[32];
    int16_t y;

    if (!st || st->overlay.opacity == 0) {
        return;
    }
    ui_mem_get(&used, &total);
    cpu = hal_cpu_tenths();
    if (cpu > 1000) {
        cpu = 1000;
    }
    snprintf(ram, sizeof ram, "R%lu C%u.%u%% %u",
             (unsigned long)used, cpu / 10u, cpu % 10u, (unsigned)st->fps);
    y = (int16_t)badge_y();
    {
        int bx = UI_MX(16);
        int bw = ui_text_width(ram, 1) + 10;
        int maxw = UI_W - 2 * bx;
        if (bw > maxw) {
            bw = maxw;
        }
        ui_draw_round((int16_t)bx, y, (int16_t)bw, (int16_t)badge_h(),
                      5, col_badge, st->overlay.opacity);
        ui_draw_text((int16_t)(bx + 5), (int16_t)(y + 5), ram,
                     col_ring, st->overlay.opacity, 1);
    }
}

static ui_menu_t *live_menu(void) {
    if (ui_menu_level(&st->settings) > UI_MENU_ICON || st->settings.zoom.scale > 0) {
        return &st->settings;
    }
    if (ui_menu_level(&st->messages) > UI_MENU_ICON || st->messages.zoom.scale > 0) {
        return &st->messages;
    }
    if (ui_menu_level(&st->sport) > UI_MENU_ICON || st->sport.zoom.scale > 0) {
        return &st->sport;
    }
    if (ui_menu_level(&st->music) > UI_MENU_ICON || st->music.zoom.scale > 0) {
        return &st->music;
    }
    if (st->focus == UI_PAGE_SETTINGS) {
        return &st->settings;
    }
    if (st->focus == UI_PAGE_MSG) {
        return &st->messages;
    }
    if (st->focus == UI_PAGE_SPORT) {
        return &st->sport;
    }
    if (st->focus == UI_PAGE_MUSIC) {
        return &st->music;
    }
    return 0;
}

static void on_confirm(ui_menu_t *menu) {
    settings_t *s = settings_get();
    int is_settings = menu == &st->settings;
    int result;

    if (is_settings && menu->level == UI_MENU_CHILD && menu->child == 3) {
        s->brightness = (uint8_t)menu->child_focus;
        hal_backlight_set(s->brightness == 0 ? 80 : (s->brightness == 1 ? 160 : 255));
        ui_menu_back(menu);
        return;
    }
    if (menu == &st->sport && menu->level == UI_MENU_CHILD) {
        if (menu->child_focus == 0) {
            sport_reset_today();
            ui_menu_back(menu);
        } else if (menu->child_focus == 1) {
            sport_debug_add_steps(1000);
        }
        return;
    }
    result = ui_menu_confirm(menu);
    if (result != UI_MENU_ROW) {
        return;
    }
    if (is_settings) {
        switch (menu->focus) {
        case 0:
            ui_settings_set_debug(!ui_settings_get_debug());
            break;
        case 1:
            s->raise_wrist = (uint8_t)!s->raise_wrist;
            break;
        case 2:
            s->key_beep = (uint8_t)!s->key_beep;
            break;
        case 3:
            ui_menu_enter_child(menu, DETAIL_N);
            break;
        case 4:
            s->bluetooth = (uint8_t)!s->bluetooth;
            break;
        case 5:
            ui_menu_enter_child(menu, ABOUT_N);
            break;
        default:
            break;
        }
        return;
    }
    if (menu == &st->sport) {
        ui_menu_enter_child(menu, ui_settings_get_debug() ? 2 : 1);
        return;
    }
    if (menu == &st->music) {
        music_page_key(HAL_KEY2);
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
    if (settings_get()->key_beep) {
        hal_beep(2000, 30);
    }
    if (menu == &st->music && ui_menu_level(menu) > UI_MENU_ICON) {
        music_page_key(pressed);
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
            on_confirm(menu);
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
        on_confirm(menu);
    }
}

void ui_init(void) {
    ui_core_init();
    st = ui_mem_alloc(sizeof *st, 8);
    if (!st) {
        ui_trace_emit("ui_init: RAM budget exceeded");
        return;
    }

    col_bg = rgb565(6, 12, 28);
    col_text = rgb565(236, 244, 255);
    col_dim = rgb565(150, 190, 230);
    col_blue = rgb565(0, 114, 230);
    col_blue_dim = rgb565(14, 42, 84);
    col_ring = rgb565(150, 210, 255);
    col_badge = rgb565(4, 18, 40);
    col_dot = rgb565(110, 160, 210);
    col_accent = rgb565(255, 159, 10);
    ui_menu_init(&st->settings, SET_N);
    ui_menu_init(&st->messages, MSG_N);
    ui_menu_init(&st->sport, 1);
    ui_menu_init(&st->music, 1);
    link_init();
    sport_init();
    settings_init();
    {
        uint8_t b = settings_get()->brightness;
        hal_backlight_set(b == 0 ? 80 : (b == 1 ? 160 : 255));
    }
    st->fps_window = hal_tick_ms();
    ui_trace_emit("ui_init");
}

void ui_nav_goto(ui_page_id_t page) {
    if (!st || ui_menu_level(&st->settings) > UI_MENU_ICON ||
        ui_menu_level(&st->messages) > UI_MENU_ICON ||
        ui_menu_level(&st->sport) > UI_MENU_ICON ||
        ui_menu_level(&st->music) > UI_MENU_ICON || (unsigned)page >= HOME_N) {
        return;
    }
    ui_anim_stop(&st->scroll, 0);
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
    settings_get()->debug = (uint8_t)enabled;
    ui_trace_emit(enabled ? "ui_settings_set_debug(1)" : "ui_settings_set_debug(0)");
    ui_anim_play(enabled ? UI_ANIM_FADE_IN : UI_ANIM_FADE_OUT, &st->overlay);
}

int ui_settings_get_debug(void) {
    return st ? st->debug : 0;
}

static void draw_app_content(ui_menu_t *menu, uint8_t a) {
    if (menu == &st->settings) {
        draw_settings_app(a);
    } else if (menu == &st->sport) {
        draw_sport_app(a);
    } else if (menu == &st->music) {
        music_page_draw(a);
    } else {
        draw_msg_app(a);
    }
}

static void draw_open_window(ui_menu_t *menu) {
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
    draw_app_content(menu, 255);
}

static void draw_pushed(ui_menu_t *menu) {
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
    draw_app_content(menu, 255);
    ui_draw_xform(parent_s, parent_dx + margin_x, margin_y);
    ui_draw_fill(0, 0, UI_W, UI_H, 0, (uint8_t)(t * 100 / 256));
    ui_draw_xform(256, UI_W - t * UI_W / 256, 0);
    if (menu == &st->sport) {
        draw_sport_child(255);
    } else {
        draw_detail(255);
    }
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
    st->fps_frames++;
    if (now - st->fps_window >= 1000u) {
        uint32_t rate = (uint32_t)st->fps_frames * 1000u / (now - st->fps_window);
        st->fps = (uint8_t)(rate > 99u ? 99u : rate);
        st->fps_frames = 0;
        st->fps_window = now;
    }
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
    {
        static int raise_prev;
        int rw = sport_raise_wrist();
        if (settings_get()->raise_wrist && rw && !raise_prev) {
            hal_backlight_set(255);
        }
        raise_prev = rw;
    }
    ui_anim_tick(now);
    if (!ui_anim_busy(&st->scroll)) {
        st->scroll.x = (int16_t)wrap_mod(st->scroll.x, HOME_N * 256);
    }
    ui_menu_tick(&st->settings);
    ui_menu_tick(&st->messages);
    ui_menu_tick(&st->sport);
    ui_menu_tick(&st->music);

    menu = 0;
    if (ui_menu_level(&st->settings) > UI_MENU_ICON || st->settings.zoom.scale > 0) {
        menu = &st->settings;
    } else if (ui_menu_level(&st->messages) > UI_MENU_ICON || st->messages.zoom.scale > 0) {
        menu = &st->messages;
    } else if (ui_menu_level(&st->sport) > UI_MENU_ICON || st->sport.zoom.scale > 0) {
        menu = &st->sport;
    } else if (ui_menu_level(&st->music) > UI_MENU_ICON || st->music.zoom.scale > 0) {
        menu = &st->music;
    }
    ui_draw_begin();
    if (menu && menu->level == UI_MENU_CHILD) {
        draw_pushed(menu);
    } else if (menu && menu->zoom.scale > 0) {
        draw_open_window(menu);
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
