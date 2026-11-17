#include "ui_internal.h"
#include "ui_menu.h"
#include "ui_icons.h"
#include "compass3d.h"
#include "pic_mod.h"

#include "hal.h"
#include "sport.h"
#include "imu9.h"
#include "music.h"
#include "settings.h"

#include <stdio.h>

/* 最外层左右翻的是页面本身，文字和图标直接画在背景上。
   进入下一级时，新页面从右往左盖上来，上一页缩小并略向左退。 */

enum {
    HOME_N = 5,
    SET_N = 9,
    DETAIL_N = 3,
    ABOUT_N = 1,
    MSG_N = LINK_EXT_MSG_MAX + 1,
    M_BADGE_W = 680,
    M_BADGE_H = 220,
    M_BADGE_GAP = 16,
    M_DOT_LIFT = 50,
    M_DOT_GAP = 110,
    M_MARGIN = UI_LAYOUT_MARGIN_M,
    M_HEADER = 200,
    M_CARD_W = 860,
    M_CARD_H = 175,
    M_PITCH = 195,
    NAV_HOLD_DELAY_MS = 320,
    NAV_REPEAT_MS = 115,
    RIP_DOT_MS = 100,
    RIP_EXPAND_MS = 260
};

enum {
    RIP_NONE = 0,
    RIP_HOLD = 1,
    RIP_EXPAND = 2
};

typedef struct app {
    int focus;
    ui_obj_t scroll;
    ui_menu_t settings;
    ui_menu_t messages;
    ui_menu_t sport;
    ui_menu_t music;
    ui_obj_t overlay;
    int sensor_open;
    int screen_asleep;
    uint32_t active_ms;
    int debug;
    uint8_t prev_keys;
    uint32_t key2_down;
    int key2_long;
    uint32_t dot_ms;
    int dot_page;
    uint32_t reveal_ms;
    uint32_t fps_window;
    uint32_t badge_sig;
    uint16_t fps_frames;
    uint8_t fps;
    uint8_t badge_op;
    uint8_t badge_heal;
    uint32_t nav_down_ms;
    uint32_t nav_step_ms;
    const ui_menu_t *rip_menu;
    uint32_t rip_t0;
    uint32_t rip_dot_t0;
    int16_t rip_dot_from_y;
    int16_t rip_dot_last_y;
    uint8_t nav_key;
    uint8_t nav_repeat;
    uint8_t rip_mode;
    uint8_t rip_level;
    int8_t rip_idx;
    uint8_t rip_from_dot;
    uint8_t msg_was_open;
    uint8_t msg_prev_level;
    uint8_t music_was_open;
    uint8_t compass_was_open;
    uint8_t about_was_open;
    uint8_t sport_open_was;
    uint8_t sport_pic_was;
    uint8_t analog_open_was;
    uint8_t sys_open_was;
    uint8_t sport_tab;
    ui_obj_t sport_scroll;
    uint8_t prev_mag_cal;
    uint32_t i2c_scan_ms;
    char i2c_scan_str[18];
    uint8_t dial_edit;
    uint8_t dial_pick;
    int16_t dial_pick_steps;
    ui_obj_t dial_zoom;
    ui_obj_t dial_slide;
} app_t;

static app_t *st;

static uint16_t col_bg;
static uint16_t col_text;
static uint16_t col_dim;
static uint16_t col_blue_dim;
static uint16_t col_ring;
static uint16_t col_badge;
static uint16_t col_dot;
static uint16_t col_accent;
static uint16_t col_line;

static const char *const set_name[SET_N] = {"调试模式", "抬腕亮屏", "震动", "亮度",
                                            "蓝牙", "状态灯", "熄屏", "SPI", "关于"};
static const char *const bright_name[DETAIL_N] = {"低", "中", "高"};
static const char *const screen_off_name[5] = {"关", "30秒", "1分钟", "2分钟", "5分钟"};
static const uint16_t screen_off_sec[5] = {0, 30, 60, 120, 300};

static void vib_play(uint16_t pat, uint16_t ms) {
    if (settings_get()->key_beep) {
        hal_beep(pat, ms);
    }
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

static void header_metrics(int scroll, int *band, int *scale, int *title_y) {
    int title_h = ui_song_height(UI_SONG_TITLE);
    int max_band = title_h + UI_MY(50);
    int min_scale = 136;
    int min_title_h = title_h * min_scale / 256;
    int min_band = min_title_h + UI_MY(45);
    int collapse = scroll <= 0 ? 0 : (scroll >= 256 ? 256 : scroll);
    int max_y = UI_MY(25);
    int min_y = (min_band - 1 - min_title_h) / 2;

    if (min_y < 1) {
        min_y = 1;
    }
    *band = max_band - (max_band - min_band) * collapse / 256;
    *scale = 256 - (256 - min_scale) * collapse / 256;
    *title_y = max_y - (max_y - min_y) * collapse / 256;
}

static int header_band_h(int collapse) {
    int band, scale, title_y;
    header_metrics(collapse, &band, &scale, &title_y);
    return band;
}

static int list_pitch(void) {
    int pitch = UI_MY(M_PITCH);
    return pitch < 24 ? 24 : pitch;
}

static int list_cam(int scroll, int n) {
    int cam;
    int max_cam;

    if (n <= 4 || scroll <= 256) {
        return 0;
    }
    cam = scroll - 256;
    max_cam = (n - 4) * 256;
    if (cam > max_cam) {
        cam = max_cam;
    }
    return cam;
}

static void home_icon_rect(int *x, int *y, int *w, int *h) {
    *w = UI_MX(500);
    *h = UI_MY(400);
    if (*w < 48) *w = 48;
    if (*h < 48) *h = 48;
    *x = (UI_W - *w) / 2;
    *y = UI_MY(100);
}

#if !defined(AT32_SLIM_ROM)
static void home_art_rect(int *x, int *y, int *w, int *h) {
    int tx, ty, tw, th;
    home_icon_rect(&tx, &ty, &tw, &th);
    *w = tw * 3 / 4;
    *h = th * 2 / 3;
    *x = tx + (tw - *w) / 2;
    *y = ty + (th - *h) / 2;
}
#endif

static void blue_card_bg(int x, int y, int w, int h, int hot, int radius, uint8_t a);

static void home_tile(uint8_t a) {
    int x, y, w, h;
    home_icon_rect(&x, &y, &w, &h);
    blue_card_bg(x, y, w, h, 0, h / 4, a);
}

static const char *const page_names[HOME_N] = {
    "", "运动", "消息", "设置", "音乐"
};

static void home_caption_y(const char *name, int cy, uint8_t a) {
    int16_t tw;
    if (a == 0 || cy >= UI_H) {
        return;
    }
    tw = ui_song_width(UI_SONG_TITLE, name);
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)cy,
                 name, col_text, a);
    ui_draw_round((int16_t)(UI_W / 2 - UI_MX(60)),
                  (int16_t)(cy + ui_song_height(UI_SONG_TITLE) + UI_MY(25)),
                  (int16_t)UI_MX(120), (int16_t)UI_MY(16),
                  1, col_accent, a);
}

static void home_caption(const char *name, uint8_t a) {
    int x, y, w, h;
    home_icon_rect(&x, &y, &w, &h);
    home_caption_y(name, y + h + UI_MY(60), a);
}

static int wrap_mod(int v, int m) {
    v %= m;
    if (v < 0) {
        v += m;
    }
    return v;
}

static int vis_index(int scroll, int n) {
    int idx;
    if (n <= 0) {
        return 0;
    }
    idx = wrap_mod(scroll, n * 256);
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

#if defined(AT32_SLIM_ROM)
__attribute__((noinline))
#endif
static void blue_card_bg(int x, int y, int w, int h, int hot, int radius, uint8_t a) {
    if (w < 8 || h < 8 || a == 0) {
        return;
    }
    ui_draw_round((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                  (int16_t)radius, hot ? col_ring : col_line, a);
    ui_draw_round((int16_t)(x + 1), (int16_t)(y + 1),
                  (int16_t)(w - 2), (int16_t)(h - 2),
                  (int16_t)(radius - 1), col_blue_dim, a);
}

#if defined(AT32_SLIM_ROM)
__attribute__((noinline))
#endif
static void blue_card_fg(int x, int y, int w, int h, int hot, uint8_t a) {
    if (w < 8 || h < 8 || a == 0 || !hot) {
        return;
    }
    ui_draw_round((int16_t)(x + UI_MX(35)),
                  (int16_t)(y + h / 4), (int16_t)UI_MX(20),
                  (int16_t)(h / 2), 1, col_ring, a);
}

#if !defined(AT32_SLIM_ROM)
static int run_frame(void);
#endif
static void draw_face_app(uint8_t a);
static void draw_sport_app(uint8_t a);
static void draw_sport_child(uint8_t a);
static void draw_sensor_page(uint8_t a);
static void draw_battery_badge(void);
static void draw_msg_app(uint8_t a);
static void draw_settings_app(uint8_t a);
static void draw_detail(uint8_t a);
static void draw_home_art(int page, uint8_t a);

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
        if (i == UI_PAGE_FACE) {
            draw_face_app(255);
        } else {
            ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
            home_tile(255);
            draw_home_art(i, 255);
            home_caption(page_names[i], 255);
        }
    }
    ui_draw_xform(256, 0, 0);
}

static void draw_dots(uint8_t alpha) {
    int wp = wrap_mod(st->scroll.x, HOME_N * 256);
    int selected = vis_index(st->scroll.x, HOME_N);
    int gap = UI_MX(M_DOT_GAP);
    int h = dot_h();
    int dx = 0;
    int x0;
    int i;

    if (alpha == 0) {
        return;
    }
    if (wp < 256) {
        dx = (256 - wp) * UI_W / 256;
    } else if (wp > (HOME_N - 1) * 256) {
        dx = ((HOME_N - 1) * 256 - wp) * UI_W / 256;
    }
    if (dx <= -UI_W || dx >= UI_W) {
        return;
    }
    if (gap < 8) {
        gap = 8;
    }
    /* 表盘(页0)为特殊页，不计入页面数点；只显示 4 个功能页并随功能页滑出/滑入屏幕。 */
    x0 = (UI_W - (HOME_N - 2) * gap) / 2 + dx;
    for (i = 0; i < HOME_N - 1; i++) {
        int page = i + 1;
        int delta = page * 256 - wp;
        int hot = (page == selected) || (page == 1 && wp < 256) || (page == HOME_N - 1 && wp > (HOME_N - 1) * 256);
        int w, x;
        if (delta < 0) {
            delta = -delta;
        }
        if (delta >= 256) {
            w = UI_MX(32);
            x = x0 + i * gap;
        } else {
            int f = 256 - delta;
            w = UI_MX(32) + (UI_MX(62) - UI_MX(32)) * f / 256;
            x = x0 + i * gap - UI_MX(16) * f / 256;
        }
        if (w < 3) {
            w = 3;
        }
        ui_draw_round((int16_t)x, (int16_t)dots_y(), (int16_t)w, (int16_t)h,
                      (int16_t)(h / 2), hot ? col_ring : col_dot, alpha);
    }
}

/* 右上角常驻电量：电池图标。图标左边界也用于表盘日期居中让位。 */
static int battery_icon_x(void) {
    int icon_w = UI_MX(110);
    int tip_w = UI_MX(16);
    int right = UI_W - UI_MX(30);

    if (icon_w < 12) {
        icon_w = 12;
    }
    if (tip_w < 2) {
        tip_w = 2;
    }
    return right - icon_w - tip_w;
}

/* 设置里的亮度 0/1/2 -> PWM。熄屏后唤醒也用它恢复。 */
static uint8_t backlight_level(void) {
    uint8_t b = settings_get()->brightness;
    return (uint8_t)(b == 0 ? 80 : (b == 1 ? 160 : 255));
}

static void draw_battery_badge(void) {
    uint8_t pct = hal_battery_pct();
    int icon_w = UI_MX(110);
    int icon_h = UI_MY(55);
    int tip_w = UI_MX(16);
    int tip_h;
    int x = battery_icon_x();
    int y = UI_MY(18);
    int fill;
    uint16_t color = col_ring;

    if (icon_w < 12) icon_w = 12;
    if (icon_h < 7) icon_h = 7;
    if (tip_w < 2) tip_w = 2;
    if (y < 1) y = 1;
    tip_h = icon_h / 2;
    if (tip_h < 3) tip_h = 3;

    if (pct == 0xFF) {
        color = col_dim;
    } else {
        if (pct > 100) {
            pct = 100;
        }
        if (pct <= 20) {
            color = col_accent;
        }
    }
    ui_draw_round((int16_t)x, (int16_t)y, (int16_t)icon_w, (int16_t)icon_h,
                  2, col_dim, 255);
    ui_draw_round((int16_t)(x + 1), (int16_t)(y + 1),
                  (int16_t)(icon_w - 2), (int16_t)(icon_h - 2), 1, col_bg, 255);
    ui_draw_round((int16_t)(x + icon_w), (int16_t)(y + (icon_h - tip_h) / 2),
                  (int16_t)tip_w, (int16_t)tip_h, 1, col_dim, 255);
    if (pct != 0xFF && pct > 0) {
        fill = (icon_w - 4) * (int)pct / 100;
        if (fill < 2) {
            fill = 2;
        }
        ui_draw_round((int16_t)(x + 2), (int16_t)(y + 2),
                      (int16_t)fill, (int16_t)(icon_h - 4), 1, color, 255);
    }
}

static void list_card_rect(int scroll, int n, int index,
                           int *x, int *y, int *w, int *h) {
    int collapse = scroll <= 0 ? 0 : (scroll >= 256 ? 256 : scroll);
    int band = header_band_h(collapse);
    int top0 = band + UI_MY(25);
    int cam = list_cam(scroll, n);
    int pitch = list_pitch();

    *w = UI_MX(M_CARD_W);
    *h = UI_MY(M_CARD_H);
    if (*h < 20) {
        *h = 20;
    }
    *x = (UI_W - *w) / 2;
    *y = top0 + (index * 256 - cam) * pitch / 256;
}

static int list_card_cy(int scroll, int n, int idx) {
    int x, y, w, h;
    list_card_rect(scroll, n, idx, &x, &y, &w, &h);
    return y + h / 2;
}

static void rip_trigger_move(const ui_menu_t *menu, int level, int old_idx,
                             int new_idx, int old_scroll, int n) {
    uint32_t now = hal_tick_ms();
    int was_same_hold = (st->rip_mode == RIP_HOLD &&
                         st->rip_menu == menu &&
                         (int)st->rip_level == level);

    if (was_same_hold && st->rip_idx == new_idx) {
        return;
    }
    if (was_same_hold) {
        st->rip_dot_from_y = st->rip_dot_last_y;
    } else {
        st->rip_dot_from_y = (int16_t)list_card_cy(old_scroll, n, old_idx);
        st->rip_dot_last_y = st->rip_dot_from_y;
    }
    st->rip_menu = menu;
    st->rip_level = (uint8_t)level;
    st->rip_idx = (int8_t)new_idx;
    st->rip_mode = RIP_HOLD;
    st->rip_dot_t0 = now;
    st->rip_from_dot = 1;
}

static void rip_trigger_expand(const ui_menu_t *menu, int level, int idx, int from_dot) {
    st->rip_menu = menu;
    st->rip_level = (uint8_t)level;
    st->rip_idx = (int8_t)idx;
    st->rip_mode = RIP_EXPAND;
    st->rip_t0 = hal_tick_ms();
    st->rip_from_dot = (uint8_t)(from_dot ? 1 : 0);
}

static void draw_list_ripple(const ui_menu_t *menu, int level, int scroll,
                             int n, uint8_t a) {
    int collapse;
    int band;
    int x, y, w, h;
    uint32_t now;
    uint8_t alpha;

    if (!st || st->rip_mode == RIP_NONE || st->rip_menu != menu ||
        (int)st->rip_level != level || n <= 0 ||
        st->rip_idx < 0 || st->rip_idx >= n || a == 0) {
        return;
    }
    collapse = scroll <= 0 ? 0 : (scroll >= 256 ? 256 : scroll);
    band = header_band_h(collapse);
    list_card_rect(scroll, n, st->rip_idx, &x, &y, &w, &h);
    now = hal_tick_ms();

    if (st->rip_mode == RIP_HOLD) {
        int dot_r = h * 34 / 100;
        int target_cy = y + h / 2;
        uint32_t el = now - st->rip_dot_t0;
        int u = el >= (uint32_t)RIP_DOT_MS ? 255 :
                ui_ease_in_out(el * 256u / (uint32_t)RIP_DOT_MS);
        int cy = (int)st->rip_dot_from_y +
                 (target_cy - (int)st->rip_dot_from_y) * u / 255;
        int clip_y0;
        int clip_y1;
        if (dot_r < 5) {
            dot_r = 5;
        }
        st->rip_dot_last_y = (int16_t)cy;
        clip_y0 = cy - dot_r;
        clip_y1 = cy + dot_r + 1;
        if (clip_y0 < band) {
            clip_y0 = band;
        }
        if (clip_y1 > UI_H) {
            clip_y1 = UI_H;
        }
        if (clip_y1 <= clip_y0) {
            return;
        }
        alpha = (uint8_t)(160 * (int)a / 255);
        ui_draw_ripple((int16_t)(x + 1), (int16_t)clip_y0,
                       (int16_t)(w - 2), (int16_t)(clip_y1 - clip_y0),
                       0, (int16_t)cy, (int16_t)dot_r, UI_COL_RIPPLE, alpha);
        return;
    }

    if (y + h <= band || y >= UI_H) {
        return;
    }
    {
        uint32_t el = now - st->rip_t0;
        uint32_t t;
        int e, r0, max_r, rip_r, fade;
        if (el >= (uint32_t)RIP_EXPAND_MS) {
            st->rip_mode = RIP_NONE;
            return;
        }
        t = el * 256u / (uint32_t)RIP_EXPAND_MS;
        e = (int)((t * 3u + (t * (512u - t)) / 256u) / 4u);
        if (e > 256) {
            e = 256;
        }
        r0 = st->rip_from_dot ? (h * 34 / 100) : 3;
        if (st->rip_from_dot && r0 < 5) {
            r0 = 5;
        }
        max_r = w / 2 + h / 6 + 2;
        rip_r = r0 + (max_r - r0) * e / 256;
        fade = 256 - (int)(t * t / 256u);
        if (fade < 0) {
            fade = 0;
        }
        alpha = (uint8_t)((170 * fade / 256) * (int)a / 255);
        ui_draw_ripple((int16_t)(x + 1), (int16_t)(y + 1),
                       (int16_t)(w - 2), (int16_t)(h - 2),
                       (int16_t)(h / 4 > 1 ? h / 4 - 1 : 0),
                       (int16_t)(y + h / 2), (int16_t)rip_r,
                       UI_COL_RIPPLE, alpha);
    }
}

static void draw_labeled_card_fg(int x, int y, int w, int h, int hot,
                                 const char *name, const char *side, uint8_t a) {
    char label[40];
    int pad = UI_MX(55);
    int right = x + w - pad;
    int ty = y + (h - ui_text_height(name, 1)) / 2;
    int max_w;

    blue_card_fg(x, y, w, h, hot, a);
    if (side && side[0]) {
        ui_text_draw_right((int16_t)right, (int16_t)ty, side,
                           hot ? col_ring : col_dim, a, 1);
        right -= ui_text_width(side, 1) + UI_MX(35);
    } else {
        ui_text_draw_right((int16_t)right, (int16_t)ty, ">", col_dim, a, 1);
        right -= ui_text_width(">", 1) + UI_MX(30);
    }
    max_w = right - (x + pad);
    ui_text_fit(label, sizeof label, name, (int16_t)max_w, 1);
    ui_draw_text((int16_t)(x + pad), (int16_t)ty, label,
                 hot ? col_text : col_dim, a, 1);
}

typedef void (*list_side_fn)(int idx, char *buf, int cap, const void *ctx);

static void draw_stack_ex(const ui_menu_t *menu, int level, int scroll, int n,
                          int focus_vis, const char *const *names,
                          list_side_fn side_fn, const void *ctx, uint8_t a) {
    int collapse = scroll <= 0 ? 0 : (scroll >= 256 ? 256 : scroll);
    int band = header_band_h(collapse);
    int pass;
    int i;
    int x, y, w, h;
    char side[16];

    for (pass = 0; pass < 2; pass++) {
        int k;
        for (k = 0; k <= n; k++) {
            int hot = (k == n);
            i = hot ? focus_vis : k;
            if (!hot && i == focus_vis) {
                continue;
            }
            list_card_rect(scroll, n, i, &x, &y, &w, &h);
            if (y + h <= band || y >= UI_H) {
                continue;
            }
            if (pass == 0) {
                blue_card_bg(x, y, w, h, hot, h / 4, a);
            } else {
                side[0] = 0;
                if (side_fn) {
                    side_fn(i, side, sizeof side, ctx);
                }
                draw_labeled_card_fg(x, y, w, h, hot, names[i], side[0] ? side : 0, a);
            }
        }
        if (pass == 0) {
            draw_list_ripple(menu, level, scroll, n, a);
        }
    }
}

static void side0_cb(int idx, char *buf, int cap, const void *ctx) {
    const char *s = (const char *)ctx;
    if (idx == 0 && s && s[0]) {
        snprintf(buf, (size_t)cap, "%s", s);
    } else {
        buf[0] = 0;
    }
}

static void draw_stack(const ui_menu_t *menu, int level, int scroll, int n,
                       int focus_vis, const char *const *names,
                       const char *side0, uint8_t a) {
    draw_stack_ex(menu, level, scroll, n, focus_vis, names,
                  side0 ? side0_cb : 0, side0, a);
}

static void draw_header_anim(const char *title, int scroll, uint8_t a) {
    int band, scale, title_y;
    int x = UI_MX(M_MARGIN);
    int tx = x + UI_MX(70);
    int title_h = ui_song_height(UI_SONG_TITLE);
    int scaled_h;
    int bar_h;
    int bar_y;
    int outer_s, outer_ox, outer_oy;
    int comp_s, comp_ox, comp_oy;

    if (a == 0) {
        return;
    }
    header_metrics(scroll, &band, &scale, &title_y);
    scaled_h = title_h * scale / 256;
    bar_h = scaled_h * 3 / 4;
    if (bar_h < 8) {
        bar_h = 8;
    }
    bar_y = title_y + (scaled_h - bar_h) / 2;

    ui_draw_fill(0, 0, UI_W, (int16_t)band, col_bg, a);
    ui_draw_line((int16_t)x, (int16_t)(band - 1),
                 (int16_t)(UI_W - x), (int16_t)(band - 1), col_line, a);
    ui_draw_round((int16_t)x, (int16_t)bar_y, (int16_t)UI_MX(24), (int16_t)bar_h,
                  1, col_accent, a);

    ui_draw_get_xform(&outer_s, &outer_ox, &outer_oy);
    comp_s = outer_s * scale / 256;
    if (comp_s < 1) {
        comp_s = 1;
    }
    comp_ox = outer_ox + tx * outer_s / 256;
    comp_oy = outer_oy + title_y * outer_s / 256;
    ui_draw_xform(comp_s, comp_ox, comp_oy);
    ui_song_draw(UI_SONG_TITLE, 0, 0, title, col_text, a);
    ui_draw_xform(outer_s, outer_ox, outer_oy);
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

static void poll_imu9_now(imu9_state_t *out, uint32_t now);
#ifndef AT32_SLIM_ROM
static void compass_vec(int deg_cw, int *dx_q8, int *dy_q8);
static void compass_disc(int cx, int cy, int r, uint16_t col, uint8_t a);
#endif
static const char *compass_dir(uint16_t heading);

static void face_comp_text(uint8_t kind, const hal_clock_t *clk,
                           const sport_stats_t *sp, const imu9_state_t *s9,
                           int allow_media, char *out, int cap) {
    static const char *const wd[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    out[0] = 0;
    if (allow_media && link_media_lyric()[0]) {
        snprintf(out, (size_t)cap, "%s %s",
                 link_media_playing() ? "Play" : "Pause", link_media_lyric());
        return;
    }
    switch (kind) {
    case UI_DIAL_COMP_DATE:
        snprintf(out, (size_t)cap, "%s  %02u.%02u",
                 wd[face_weekday(clk->year, clk->month, clk->day)],
                 clk->month, clk->day);
        break;
    case UI_DIAL_COMP_STEP:
        snprintf(out, (size_t)cap, "%lu/%lu 步",
                 (unsigned long)sp->steps, (unsigned long)(sp->goal ? sp->goal : 8000u));
        break;
    case UI_DIAL_COMP_HDG:
        if (s9 && s9->fused_axes) {
            snprintf(out, (size_t)cap, "%03u度 %s",
                     (unsigned)s9->heading_deg, compass_dir(s9->heading_deg));
        } else {
            snprintf(out, (size_t)cap, "---度 --");
        }
        break;
    case UI_DIAL_COMP_BAT: {
        uint8_t pct = hal_battery_pct();
        uint16_t mv = hal_battery_mv();
        if (pct != 0xFFu) {
            snprintf(out, (size_t)cap, "BAT %u%%", (unsigned)pct);
        } else if (mv > 0u) {
            snprintf(out, (size_t)cap, "%u.%02uV", (unsigned)(mv / 1000u),
                     (unsigned)((mv % 1000u) / 10u));
        } else {
            snprintf(out, (size_t)cap, "USB PWR");
        }
        break;
    }
    default:
        break;
    }
}

static void draw_face_digital(const settings_t *s, const hal_clock_t *clock,
                              const sport_stats_t *sport, const imu9_state_t *s9,
                              uint16_t fg, uint16_t ac, uint16_t bg, uint8_t a) {
    int row_w = ui_song_width(UI_SONG_CLOCK, "00");
    int row_h = ui_song_height(UI_SONG_CLOCK);
    int gap_y = UI_MY(45);
    int x0 = (UI_W - row_w) / 2;
    int y0 = UI_MY(220);
    int mid = y0 + row_h + gap_y / 2;
    int dot = UI_MX(24);
    int cx = (UI_W - dot) / 2;
    int bar_x = UI_MX(M_MARGIN);
    int bar_w = UI_W - bar_x * 2;
    int bar_y = UI_MY(875);
    int disp_hr = s->use_24h ? (int)clock->hour
                             : (((int)clock->hour % 12) ? ((int)clock->hour % 12) : 12);
    int percent;
    uint8_t colon_a;
    char line[40];
    char fitted[40];
    char value[20];

    if (gap_y < 5) gap_y = 5;
    if (dot < 2) dot = 2;
    ui_draw_fill(0, 0, UI_W, UI_H, bg, a);

    face_comp_text(s->dial_top, clock, sport, s9, 1, line, (int)sizeof line);
    if (line[0]) {
        ui_text_fit(fitted, sizeof fitted, line,
                    (int16_t)((battery_icon_x() - UI_W / 2) * 2 - UI_MX(16)), 1);
        ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)UI_MY(60), fitted, col_dim, a, 1);
    }

    draw_face_pair(x0, y0, disp_hr, fg, a);
    draw_face_pair(x0, y0 + row_h + gap_y, clock->minute, ac, a);
    if (!s->use_24h) {
        ui_draw_text((int16_t)bar_x, (int16_t)(y0 + 2),
                     clock->hour >= 12 ? "PM" : "AM", col_dim, a, 1);
    }
    colon_a = (s->dial_flags & UI_DIAL_FLAG_SECONDS)
              ? (uint8_t)((100 + ui_anim_pulse(hal_tick_ms(), 2000u) * 155 / 255) * a / 255)
              : a;
    ui_draw_round((int16_t)cx, (int16_t)(mid - dot), (int16_t)dot, (int16_t)dot,
                  (int16_t)(dot / 2), ac, colon_a);
    ui_draw_round((int16_t)cx, (int16_t)(mid + dot / 2), (int16_t)dot, (int16_t)dot,
                  (int16_t)(dot / 2), ac, colon_a);

    if (s->dial_bot == UI_DIAL_COMP_STEP) {
        percent = sport->goal ? (int)(sport->steps * 100u / sport->goal) : 0;
        if (percent > 100) percent = 100;
        ui_draw_round((int16_t)bar_x, (int16_t)bar_y, (int16_t)bar_w,
                      (int16_t)UI_MY(16), 1, col_blue_dim, a);
        if (percent > 0) {
            int fill = bar_w * percent / 100;
            if (fill < 2) fill = 2;
            ui_draw_round((int16_t)bar_x, (int16_t)bar_y, (int16_t)fill,
                          (int16_t)UI_MY(16), 1, ac, a);
        }
        snprintf(value, sizeof value, "%d%%", percent);
        ui_draw_text((int16_t)bar_x, (int16_t)UI_MY(890), "STEPS", col_dim, a, 1);
        ui_text_draw_right((int16_t)(bar_x + bar_w), (int16_t)UI_MY(890),
                           value, fg, a, 1);
    } else {
        face_comp_text(s->dial_bot, clock, sport, s9, 0, line, (int)sizeof line);
        if (line[0]) {
            ui_text_fit(fitted, sizeof fitted, line, (int16_t)bar_w, 1);
            ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)UI_MY(880),
                                fitted, fg, a, 1);
        }
    }
}

#ifdef AT32_SLIM_ROM
static void pic_api_fill(pic_mod_api_t *api, const settings_t *s,
                         uint16_t fg, uint16_t ac, uint16_t bg,
                         const char *top_line, const char *bot_line,
                         const char *scan_line) {
    static const pic_mod_api_t s_tmpl = {
        .ui_w = UI_W,
        .ui_h = UI_H,
        .fill = ui_draw_fill,
        .round = ui_draw_round,
        .line = ui_draw_line,
        .text = ui_draw_text,
        .text_center = ui_text_draw_center,
        .text_right = ui_text_draw_right,
        .text_fit = ui_text_fit,
        .text_w = ui_text_width,
        .text_h = ui_text_height,
        .song_draw = ui_song_draw,
        .song_w = ui_song_width,
        .song_h = ui_song_height,
        .ease_in_out = ui_ease_in_out,
        .clock_get = hal_clock_get,
        .sport_get = sport_get,
        .sport_bars = sport_get_bars,
        .imu_get = imu9_get,
        .baro_read = hal_baro_read,
        .mag_name = hal_mag_name,
        .get_fps = ui_app_get_fps,
        .get_cpu_tenths = hal_cpu_tenths,
        .line_w = ui_draw_line_w,
    };
    *api = s_tmpl;
    api->col_bg = col_bg;
    api->col_line = col_line;
    api->col_accent = col_accent;
    api->col_ring = col_ring;
    api->col_text = col_text;
    api->col_dim = col_dim;
    api->col_surface = col_blue_dim;
    api->col_blue_dim = col_blue_dim;
    api->dial_fg = fg;
    api->dial_ac = ac;
    api->dial_bg = bg;
    api->now_ms = hal_tick_ms();
    api->reveal_ms = st ? st->reveal_ms : 0u;
    api->dial_top = s->dial_top;
    api->dial_bot = s->dial_bot;
    api->dial_flags = s->dial_flags;
    api->use_24h = s->use_24h;
    api->bat_pct = hal_battery_pct();
    api->bat_mv = hal_battery_mv();
    api->lyric = link_media_lyric();
    api->media_playing = (uint8_t)link_media_playing();
    api->comp_top = top_line;
    api->comp_bot = bot_line;
    api->i2c_scan = scan_line;
}

__attribute__((noinline))
static int pic_mod_exec(const uint8_t *blob, uint16_t entry_off,
                        const settings_t *s, uint16_t fg, uint16_t ac, uint16_t bg,
                        const char *top_line, const char *bot_line,
                        const char *scan_line, uint8_t page_id,
                        uint8_t mode, uint8_t a, int clear_bg) {
    pic_mod_api_t api;
    if (!blob) {
        return 0;
    }
    pic_api_fill(&api, s, fg, ac, bg, top_line, bot_line, scan_line);
    api.page_id = page_id;
    __asm volatile("dsb\n\tisb" ::: "memory");
    ((pic_mod_entry_t)((uintptr_t)(blob + entry_off) | 1u))(&api, mode, a, clear_bg);
    return 1;
}

__attribute__((noinline))
static int pic_mod_exec_std(const uint8_t *blob, uint16_t entry_off,
                            const char *scan_line, uint8_t page_id,
                            uint8_t mode, uint8_t a, int clear_bg) {
    int ok = pic_mod_exec(blob, entry_off, settings_get(), col_text, col_accent, col_bg,
                          0, 0, scan_line, page_id, mode, a, clear_bg);
    if (!ok) {
        if (clear_bg && a == 255u) {
            ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
        }
        ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)(UI_H / 2),
                            "加载中", col_dim, a, 1);
    }
    return ok;
}

__attribute__((noinline))
static void draw_face_clock_stub(const settings_t *s, const hal_clock_t *clock,
                                 uint16_t fg, uint16_t bg, uint8_t a) {
    char tbuf[8];
    int disp_hr = s->use_24h ? (int)clock->hour
                             : (((int)clock->hour % 12) ? ((int)clock->hour % 12) : 12);
    ui_draw_fill(0, 0, UI_W, UI_H, bg, a);
    snprintf(tbuf, sizeof tbuf, "%02d:%02u", disp_hr, clock->minute);
    ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)(UI_H / 2 - 12), tbuf, fg, a, 2);
}
#endif

static void draw_face_analog(const settings_t *s, const hal_clock_t *clock,
                             const sport_stats_t *sport, const imu9_state_t *s9,
                             uint16_t fg, uint16_t ac, uint16_t bg, uint8_t a) {
#ifdef AT32_SLIM_ROM
    char top_line[36], bot_line[36];
    uint16_t entry_off = 0u;
    const uint8_t *blob = link_pic_analog_blob(&entry_off);

    face_comp_text(s->dial_top, clock, sport, s9, 1, top_line, (int)sizeof top_line);
    face_comp_text(s->dial_bot, clock, sport, s9, 0, bot_line, (int)sizeof bot_line);
    if (pic_mod_exec(blob, entry_off, s, fg, ac, bg, top_line, bot_line, 0, 0u, 0u, a, 1)) {
        return;
    }
    draw_face_clock_stub(s, clock, fg, bg, a);
#else
    int cx = UI_W / 2;
    int cy = UI_H / 2 - UI_MY(15);
    int r = (UI_W < UI_H ? UI_W : UI_H) * 34 / 100;
    int k, vx, vy, hr_deg, min_deg, sec_deg;
    char line[36];
    char fitted[36];

    ui_draw_fill(0, 0, UI_W, UI_H, bg, a);
    face_comp_text(s->dial_top, clock, sport, s9, 1, line, (int)sizeof line);
    if (line[0]) {
        ui_text_fit(fitted, sizeof fitted, line,
                    (int16_t)((battery_icon_x() - UI_W / 2) * 2 - UI_MX(16)), 1);
        ui_text_draw_center((int16_t)cx, (int16_t)UI_MY(50), fitted, col_dim, a, 1);
    }

    compass_disc(cx, cy, r, col_line, a);
    compass_disc(cx, cy, r - 2, bg, a);
    for (k = 0; k < 12; k++) {
        int r0 = r - ((k % 3 == 0) ? 7 : 4);
        int r1 = r - 3;
        compass_vec(k * 30, &vx, &vy);
        ui_draw_line((int16_t)(cx + ((vx * r0) >> 8)), (int16_t)(cy + ((vy * r0) >> 8)),
                     (int16_t)(cx + ((vx * r1) >> 8)), (int16_t)(cy + ((vy * r1) >> 8)),
                     (k % 3 == 0) ? ac : col_dim, a);
    }

    hr_deg = (clock->hour % 12) * 30 + clock->minute / 2;
    min_deg = clock->minute * 6;
    compass_vec(hr_deg, &vx, &vy);
    ui_draw_line((int16_t)cx, (int16_t)cy,
                 (int16_t)(cx + ((vx * (r * 52 / 100)) >> 8)),
                 (int16_t)(cy + ((vy * (r * 52 / 100)) >> 8)), fg, a);
    compass_vec(min_deg, &vx, &vy);
    ui_draw_line((int16_t)cx, (int16_t)cy,
                 (int16_t)(cx + ((vx * (r * 76 / 100)) >> 8)),
                 (int16_t)(cy + ((vy * (r * 76 / 100)) >> 8)), ac, a);
    if (s->dial_flags & UI_DIAL_FLAG_SECONDS) {
        sec_deg = clock->second * 6;
        compass_vec(sec_deg, &vx, &vy);
        ui_draw_line((int16_t)cx, (int16_t)cy,
                     (int16_t)(cx + ((vx * (r * 84 / 100)) >> 8)),
                     (int16_t)(cy + ((vy * (r * 84 / 100)) >> 8)), col_accent, a);
    }
    compass_disc(cx, cy, 4, ac, a);
    compass_disc(cx, cy, 2, bg, a);

    face_comp_text(s->dial_bot, clock, sport, s9, 0, line, (int)sizeof line);
    if (line[0]) {
        ui_text_fit(fitted, sizeof fitted, line, (int16_t)(UI_W - UI_MX(140)), 1);
        ui_text_draw_center((int16_t)cx, (int16_t)UI_MY(885), fitted, fg, a, 1);
    }
#endif
}

static void draw_face_imu(const settings_t *s, const hal_clock_t *clock,
                          const sport_stats_t *sport, const imu9_state_t *s9,
                          uint16_t fg, uint16_t ac, uint16_t bg, uint8_t a) {
#ifdef AT32_SLIM_ROM
    char bot_line[36];
    uint16_t entry_off = 0u;
    const uint8_t *blob = link_pic_analog_blob(&entry_off);

    face_comp_text(s->dial_bot == UI_DIAL_COMP_HDG ? UI_DIAL_COMP_STEP : s->dial_bot,
                   clock, sport, s9, 0, bot_line, (int)sizeof bot_line);
    if (pic_mod_exec(blob, entry_off, s, fg, ac, bg, 0, bot_line, 0, 0u, 1u, a, 1)) {
        return;
    }
    draw_face_clock_stub(s, clock, fg, bg, a);
#else
    int margin = UI_MX(M_MARGIN);
    int w = UI_W - margin * 2;
    int sc = UI_W >= 200 ? 3 : 2;
    int disp_hr = s->use_24h ? (int)clock->hour
                             : (((int)clock->hour % 12) ? ((int)clock->hour % 12) : 12);
    int pct = sport->goal ? (int)(sport->steps * 100u / sport->goal) : 0;
    int horizon_y = UI_MY(540);
    int tilt, fill;
    char tbuf[16];
    char line[36];

    if (pct > 100) pct = 100;
    ui_draw_fill(0, 0, UI_W, UI_H, bg, a);
    snprintf(line, sizeof line, "%s %03u度",
             s9->fused_axes == 9 ? "9DOF" : (s9->fused_axes == 6 ? "6DOF" : "IMU"),
             (unsigned)s9->heading_deg);
    ui_draw_text((int16_t)margin, (int16_t)UI_MY(55), line, ac, a, 1);

    snprintf(tbuf, sizeof tbuf,
             (s->dial_flags & UI_DIAL_FLAG_SECONDS) ? "%02d:%02u:%02u" : "%02d:%02u",
             disp_hr, clock->minute, clock->second);
    ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)UI_MY(240), tbuf, fg, a, (uint8_t)sc);

    ui_draw_round((int16_t)margin, (int16_t)horizon_y, (int16_t)w,
                  (int16_t)UI_MY(150), 6, col_blue_dim, a);
    {
        int mid_y = (int)horizon_y + (int)UI_MY(45);
        int max_t = (int)UI_MY(36);
        int max_p = (int)UI_MY(15);
        int pitch_off = (int)s9->pitch_d10 / 50;
        int is_level = (s9->roll_d10 >= -20 && s9->roll_d10 <= 20 &&
                        s9->pitch_d10 >= -20 && s9->pitch_d10 <= 20);
        uint16_t ref_c = is_level ? UI_COL_MINT : UI_COL_MUTED;
        if (max_t < 4) max_t = 4;
        if (max_p < 2) max_p = 2;
        tilt = (int)s9->roll_d10 / 30;
        if (tilt > max_t) tilt = max_t;
        if (tilt < -max_t) tilt = -max_t;
        if (pitch_off > max_p) pitch_off = max_p;
        if (pitch_off < -max_p) pitch_off = -max_p;
        /* Fixed level reference ticks at edges & center */
        ui_draw_line((int16_t)(margin + 3), (int16_t)mid_y,
                     (int16_t)(margin + 6), (int16_t)mid_y, ref_c, a);
        ui_draw_line((int16_t)(margin + w - 7), (int16_t)mid_y,
                     (int16_t)(margin + w - 4), (int16_t)mid_y, ref_c, a);
        ui_draw_line((int16_t)(UI_W / 2 - 3), (int16_t)mid_y,
                     (int16_t)(UI_W / 2 + 3), (int16_t)mid_y, ref_c, a);
        /* Horizon line: watch left tilt (roll<0) lifts left end and drops right end (tilts right). */
        ui_draw_line((int16_t)(margin + 8), (int16_t)(mid_y + pitch_off + tilt),
                     (int16_t)(margin + w - 8), (int16_t)(mid_y + pitch_off - tilt),
                     is_level ? UI_COL_MINT : ac, a);
    }
    snprintf(line, sizeof line, "R %+d  P %+d",
             (int)(s9->roll_d10 / 10), (int)(s9->pitch_d10 / 10));
    ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)(horizon_y + UI_MY(95)), line, fg, a, 1);

    ui_draw_round((int16_t)margin, (int16_t)UI_MY(770), (int16_t)w,
                  (int16_t)UI_MY(20), 2, col_blue_dim, a);
    if (pct > 0) {
        fill = w * pct / 100;
        if (fill < 2) fill = 2;
        ui_draw_round((int16_t)margin, (int16_t)UI_MY(770), (int16_t)fill,
                      (int16_t)UI_MY(20), 2, ac, a);
    }
    face_comp_text(s->dial_bot == UI_DIAL_COMP_HDG ? UI_DIAL_COMP_STEP : s->dial_bot,
                   clock, sport, s9, 0, line, (int)sizeof line);
    if (line[0]) {
        ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)UI_MY(840), line, col_dim, a, 1);
    }
#endif
}

static void draw_face_photo(const settings_t *s, const hal_clock_t *clock,
                            const sport_stats_t *sport, const imu9_state_t *s9,
                            uint16_t fg, uint16_t ac, uint16_t bg, uint8_t a) {
    int pos = (s->dial_flags & UI_DIAL_FLAG_POS_MASK) >> 1;
    int disp_hr = s->use_24h ? (int)clock->hour
                             : (((int)clock->hour % 12) ? ((int)clock->hour % 12) : 12);
    int sc = UI_W >= 200 ? 3 : 2;
    int th = ui_text_height("0", (uint8_t)sc);
    int cap_h = th + ui_text_height("0", 1) + 14;
    int cap_y = pos == 1 ? UI_MY(90) : (pos == 2 ? UI_H - cap_h - UI_MY(70) : (UI_H - cap_h) / 2);
    char tbuf[12];
    char line[36];
    char fitted[36];

    int has_img = s->img_ready && (s->img_on_chip || hal_ext_flash_present()) &&
                  s->img_w > 0 && s->img_h > 0;
    if (!has_img || a < 255 || s->img_w < UI_W || s->img_h < UI_H ||
        s->img_fmt < UI_EXT_FMT_RGB565) {
        ui_draw_fill(0, 0, UI_W, UI_H, bg, a);
    }
    if (has_img) {
        ui_ext_image_draw_fast((int16_t)((UI_W - s->img_w) / 2),
                               (int16_t)((UI_H - s->img_h) / 2),
                               s->img_w, s->img_h,
                               s->img_on_chip ? hal_int_img_ptr() : 0,
                               link_ext_img_offset(),
                               s->img_fmt,
                               s->img_fmt == UI_EXT_FMT_1BPP ? ac : 0u,
                               a, link_img_gen());
    } else {
        ui_draw_round((int16_t)UI_MX(120), (int16_t)UI_MY(140),
                      (int16_t)(UI_W - UI_MX(240)), (int16_t)(UI_H - UI_MY(280)),
                      12, col_blue_dim, a);
        ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)UI_MY(860),
                            "加载中", col_dim, a, 1);
    }
    if (s->dial_flags & UI_DIAL_FLAG_SCRIM) {
        ui_draw_round((int16_t)UI_MX(80), (int16_t)cap_y,
                      (int16_t)(UI_W - UI_MX(160)), (int16_t)cap_h,
                      8, UI_COL_BG, (uint8_t)((int)a * 170 / 255));
    }
    snprintf(tbuf, sizeof tbuf, "%02d:%02u", disp_hr, clock->minute);
    ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)(cap_y + 6), tbuf, fg, a, (uint8_t)sc);
    face_comp_text(s->dial_top == UI_DIAL_COMP_NONE ? s->dial_bot : s->dial_top,
                   clock, sport, s9, 1, line, (int)sizeof line);
    if (line[0]) {
        ui_text_fit(fitted, sizeof fitted, line, (int16_t)(UI_W - UI_MX(200)), 1);
        ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)(cap_y + th + 9),
                            fitted, ac, a, 1);
    }
}

static void draw_face_content(uint8_t dial_id, uint8_t a) {
    const settings_t *s = settings_get();
    hal_clock_t clock;
    sport_stats_t sport;
    imu9_state_t s9;
    uint16_t fg = (s->dial_text || s->cfg_loaded) ? s->dial_text : col_text;
    uint16_t ac = (s->dial_accent || s->cfg_loaded) ? s->dial_accent : col_ring;
    uint16_t bg = s->cfg_loaded ? s->dial_bg : col_bg;

    hal_clock_get(&clock);
    sport_get(&sport);
    if (dial_id == UI_DIAL_IMU || s->dial_top == UI_DIAL_COMP_HDG ||
        s->dial_bot == UI_DIAL_COMP_HDG) {
        poll_imu9_now(&s9, hal_tick_ms());
    } else {
        imu9_get(&s9);
    }

    switch (dial_id) {
    case UI_DIAL_ANALOG:
        draw_face_analog(s, &clock, &sport, &s9, fg, ac, bg, a);
        break;
    case UI_DIAL_IMU:
        draw_face_imu(s, &clock, &sport, &s9, fg, ac, bg, a);
        break;
    case UI_DIAL_PHOTO:
        draw_face_photo(s, &clock, &sport, &s9, fg, ac, bg, a);
        break;
    default:
        draw_face_digital(s, &clock, &sport, &s9, fg, ac, bg, a);
        break;
    }
}

static void draw_face_app(uint8_t a) {
    static const char *const dial_toast_name[UI_DIAL_COUNT] = {
        "数字表盘", "指针表盘", "9DOF 表盘", "自定义表"
    };
    const settings_t *s = settings_get();
    int zoom_t = st->dial_zoom.scale;
    int active = st->dial_edit || zoom_t > 0;
    int os = 256, ox = 0, oy = 0;
    int sc = 256, pw = UI_W, ph = UI_H, px = 0, py = 0, frac = 0;
    uint8_t left_id = s->dial_id, right_id = s->dial_id;
    int pass;

    if (active) {
        int wp = wrap_mod(st->dial_slide.x, UI_DIAL_COUNT * 256);
        int sc_edit = 256 - 90;
        int pw_edit = UI_W * sc_edit / 256;
        int ph_edit = UI_H * sc_edit / 256;
        int px_edit = (UI_W - pw_edit) / 2;
        int cy_edit = 22 + ph_edit / 2 - 6;
        int tw = UI_MX(680);
        int ty = UI_H - 22;
        char hdr[4];
        const char *title = dial_toast_name[st->dial_pick % UI_DIAL_COUNT];

        sc = 256 - 90 * zoom_t / 255;
        pw = UI_W * sc / 256;
        ph = UI_H * sc / 256;
        px = (UI_W - pw) / 2;
        py = 22 * zoom_t / 255;
        frac = wp & 255;
        left_id = (uint8_t)(wp >> 8);
        right_id = (uint8_t)((left_id + 1) % UI_DIAL_COUNT);
        ui_draw_get_xform(&os, &ox, &oy);

        /* 底层 Chrome：序号 / 左右提示 / 表盘名称置底，选中放大或滑动时由上层表盘自然覆盖遮挡。 */
        ui_draw_fill(0, 0, UI_W, UI_H, UI_COL_BG, a);
        hdr[0] = (char)('1' + (st->dial_pick % UI_DIAL_COUNT));
        hdr[1] = '/';
        hdr[2] = '4';
        hdr[3] = 0;
        ui_text_draw_center((int16_t)(UI_W / 2), 4, hdr, col_ring, a, 1);
        if (px_edit >= 8) {
            ui_text_draw_center((int16_t)(px_edit / 2), (int16_t)cy_edit, "<", col_ring, a, 1);
            ui_text_draw_center((int16_t)(px_edit + pw_edit + (UI_W - px_edit - pw_edit) / 2),
                                (int16_t)cy_edit, ">", col_ring, a, 1);
        }
        ui_draw_round((int16_t)((UI_W - tw) / 2), (int16_t)ty,
                      (int16_t)tw, 16, 8, col_badge, a);
        ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)(ty + 2), title, col_text, a, 1);
    }
    for (pass = 0; pass < (active && frac > 0 ? 2 : 1); pass++) {
        uint8_t card_id = pass == 0 ? left_id : right_id;
        if (active) {
            int dx = pass == 0 ? (-frac * UI_W / 256) : ((256 - frac) * UI_W / 256);
            int cx = px + dx;
            if (zoom_t > 0) {
                ui_draw_xform(os, ox, oy);
                ui_draw_fill((int16_t)(cx - 2), (int16_t)(py - 2), (int16_t)(pw + 4), 2, col_ring, a);
                ui_draw_fill((int16_t)(cx - 2), (int16_t)(py + ph), (int16_t)(pw + 4), 2, col_ring, a);
                ui_draw_fill((int16_t)(cx - 2), (int16_t)py, 2, (int16_t)ph, col_ring, a);
                ui_draw_fill((int16_t)(cx + pw), (int16_t)py, 2, (int16_t)ph, col_ring, a);
            }
            ui_draw_xform(sc * os / 256,
                          ox + cx * os / 256,
                          oy + py * os / 256);
        }
        draw_face_content(card_id, a);
    }
    if (active) {
        ui_draw_xform(os, ox, oy);
    }
}

#if !defined(AT32_SLIM_ROM)
/* 跑步采用外部 AI 按 ui/ICON_SVG_PROMPT.md 产出的关键帧（已人工预览验收）。
   ci: 0=主色(橙) 1=白 2=橙。脚着地时 y=42。 */
static const uint16_t icon_pal[] = {
    UI_COL_TEXT, UI_COL_AMBER, UI_COL_MINT, UI_COL_BG, UI_COL_MINT
};
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


static int run_frame(void) {
    return (int)((hal_tick_ms() / 60u) % 5u);
}
#endif

static void poll_imu9_now(imu9_state_t *out, uint32_t now) {
#if !defined(__ARM_ARCH) && !defined(__thumb__)
    static uint32_t poll_ms;

    if ((now - poll_ms) >= 100u || poll_ms == 0u) {
        poll_ms = now;
        if (imu9_is_stale(now, 150u)) {
            hal_imu_t imu;
            hal_mag_t mag;
            int io = hal_imu_read(&imu);
            int mo = hal_mag_read(&mag);
            if (io || mo) {
                imu9_feed(io ? &imu : NULL, mo ? &mag : NULL, now);
            }
        }
    }
#else
    /* AT32 实机由 port_service() 在主循环与帧间隙唯一调度 50Hz I2C 采集，UI 渲染期绝不重复访问 I2C。 */
    (void)now;
#endif
    imu9_get(out);
}

#if !defined(AT32_SLIM_ROM)
/* Quarter-wave sin(0..90deg) * 255 in 5-degree steps for compact ROM trig. */
static const uint8_t compass_sin5[19] = {
    0, 22, 44, 66, 87, 108, 128, 146, 164, 180,
    195, 209, 221, 231, 240, 246, 251, 254, 255
};

static int compass_sin_q8(int deg) {
    int q, a, i, r, v;

    deg = wrap_mod(deg, 360);
    q = deg / 90;
    a = deg % 90;
    if (q == 1 || q == 3) {
        a = 90 - a;
    }
    i = a / 5;
    r = a % 5;
    v = compass_sin5[i] + (compass_sin5[i + 1] - compass_sin5[i]) * r / 5;
    return (q >= 2) ? -v : v;
}

static void compass_vec(int deg_cw, int *dx_q8, int *dy_q8) {
    /* Clockwise from 12 o'clock on screen: x = +sin, y = -cos. */
    *dx_q8 = compass_sin_q8(deg_cw);
    *dy_q8 = -compass_sin_q8(deg_cw + 90);
}

static const char *compass_dir_zh(uint16_t heading) {
    static const char *const dirs[8] = {
        "北", "东北", "东", "东南", "南", "西南", "西", "西北"
    };
    return dirs[((heading + 22u) % 360u) / 45u];
}
#endif

static void draw_sport_pager(int tab, uint8_t a) {
    int y = UI_MY(24);
    int w_on = UI_MX(85);
    int w_off = UI_MX(32);
    int h = UI_MY(24);
    int gap = UI_MX(30);
    int tot, x0, x1;

    if (w_on < 8) w_on = 8;
    if (w_off < 3) w_off = 3;
    if (h < 3) h = 3;
    if (gap < 3) gap = 3;
    if (y < 2) y = 2;
    tot = w_on + gap + w_off;
    x0 = (UI_W - tot) / 2;
    x1 = tab ? (x0 + w_off + gap) : (x0 + w_on + gap);
    ui_draw_round((int16_t)x0, (int16_t)y,
                  (int16_t)(tab ? w_off : w_on), (int16_t)h,
                  (int16_t)(h / 2), tab ? col_line : col_ring, a);
    ui_draw_round((int16_t)x1, (int16_t)y,
                  (int16_t)(tab ? w_on : w_off), (int16_t)h,
                  (int16_t)(h / 2), tab ? col_ring : col_line, a);
}

static void draw_sport_steps(uint8_t a, int clear_bg) {
#ifdef AT32_SLIM_ROM
    uint16_t entry_off = 0u;
    const uint8_t *blob = link_pic_sport_blob(&entry_off);
    if (pic_mod_exec_std(blob, entry_off, 0, 0u, 0u, a, clear_bg)) {
        return;
    }
#else
    sport_stats_t ss;
    uint32_t bars[SPORT_BARS];
    hal_clock_t clk;
    char line[40];
    char goal[32];
    int margin = UI_MX(M_MARGIN);
    int clock_h = ui_song_height(UI_SONG_CLOCK);
    int number_y = UI_MY(75);
    int label_y = number_y + clock_h + UI_MY(15);
    int label_h = ui_text_height("今日步数", 1);
    int footer_h = ui_text_height("0 m   0 kcal", 1);
    int footer_y = UI_H - footer_h - UI_MY(30);
    int chart_bot = footer_y - UI_MY(50);
    int chart_top = label_y + label_h + UI_MY(35);
    int chart_h = chart_bot - chart_top;
    int pitch = (UI_W - margin * 2) / SPORT_BARS;
    int bw = pitch - UI_MX(24);
    int bar_x0;
    int percent;
    uint32_t max = 1;
    uint32_t elapsed = hal_tick_ms() - st->reveal_ms;
    int cur, i;

    sport_get(&ss);
    sport_get_bars(bars, SPORT_BARS);
    hal_clock_get(&clk);
    cur = (clk.hour / 2) % SPORT_BARS;
    for (i = 0; i < SPORT_BARS; i++) {
        if (bars[i] > max) max = bars[i];
    }
    if (chart_h < 8) chart_h = 8;
    if (bw < 2) bw = 2;
    bar_x0 = (UI_W - ((SPORT_BARS - 1) * pitch + bw)) / 2;
    percent = ss.goal ? (int)(ss.steps * 100u / ss.goal) : 0;
    if (percent > 100) percent = 100;

    if (clear_bg && a == 255) {
        ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    }
    /* 运动一级页去掉顶部大标题，顶部直接让位给步数字号并避开右上角电池图标。 */
    snprintf(line, sizeof line, "%lu", (unsigned long)ss.steps);
    if (ui_song_width(UI_SONG_CLOCK, line) <= UI_W - margin * 2) {
        int16_t nw = ui_song_width(UI_SONG_CLOCK, line);
        ui_song_draw(UI_SONG_CLOCK, (int16_t)((UI_W - nw) / 2),
                     (int16_t)number_y, line, col_text, a);
    } else {
        int th;
        ui_text_fit(goal, sizeof goal, line,
                    (int16_t)(UI_W - margin * 2), 2);
        th = ui_text_height(goal, 2);
        ui_text_draw_center((int16_t)(UI_W / 2),
                            (int16_t)(number_y + (clock_h - th) / 2),
                            goal, col_text, a, 2);
    }
    ui_draw_text((int16_t)margin, (int16_t)label_y, "今日步数", col_dim, a, 1);
    snprintf(goal, sizeof goal, "%d%%", percent);
    ui_text_draw_right((int16_t)(UI_W - margin), (int16_t)label_y, goal, col_ring, a, 1);

    for (i = 0; i < SPORT_BARS; i++) {
        uint32_t local = elapsed > (uint32_t)i * 28u ? elapsed - (uint32_t)i * 28u : 0;
        int grow = ui_ease_in_out(local * 256u / 420u);
        int bh = (int)(bars[i] * (uint32_t)chart_h / max);
        int bx = bar_x0 + i * pitch;
        uint8_t bar_a = (uint8_t)((int)a * (i == cur ? 255 : 155) / 255);
        bh = bh * grow / 255;
        if (bars[i] > 0 && grow > 160 && bh < 2) bh = 2;
        if (bars[i] == 0) {
            ui_draw_fill((int16_t)bx, (int16_t)(chart_bot - 1),
                         (int16_t)bw, 1, col_line, a);
        } else if (bh > 0) {
            ui_draw_round((int16_t)bx, (int16_t)(chart_bot - bh),
                          (int16_t)bw, (int16_t)bh, 2,
                          i == cur ? col_accent : col_ring, bar_a);
        }
    }
    ui_draw_line((int16_t)margin, (int16_t)chart_bot,
                 (int16_t)(UI_W - margin), (int16_t)chart_bot, col_line, a);

    snprintf(line, sizeof line, "%lu m   %lu kcal",
             (unsigned long)ss.distance_m, (unsigned long)ss.calories);
    ui_text_fit(goal, sizeof goal, line, (int16_t)(UI_W - margin * 2), 1);
    ui_text_draw_center((int16_t)(UI_W / 2), (int16_t)footer_y, goal, col_dim, a, 1);
#endif
}

#if !defined(AT32_SLIM_ROM)
static void compass_disc(int cx, int cy, int r, uint16_t col, uint8_t a) {
    ui_draw_round((int16_t)(cx - r), (int16_t)(cy - r),
                  (int16_t)(r * 2), (int16_t)(r * 2),
                  (int16_t)r, col, a);
}

static void compass_needle(int cx, int cy, int dx, int dy, int len,
                           int pw, uint16_t col, uint8_t a) {
    int tx = cx + ((dx * len) >> 8);
    int ty = cy + ((dy * len) >> 8);
    int k;

    for (k = -pw; k <= pw; k++) {
        ui_draw_line((int16_t)(cx + ((-dy * k) >> 8)),
                     (int16_t)(cy + ((dx * k) >> 8)),
                     (int16_t)tx, (int16_t)ty, col, a);
    }
}
#endif

/* 运动一级第二屏：本地 ROM 九轴指南针，全分辨率自适应并压缩点阵显示。 */
static void draw_sport_compass(uint8_t a, int clear_bg) {
#if defined(AT32_SLIM_ROM)
    imu9_state_t s9;
    poll_imu9_now(&s9, hal_tick_ms());
    if (link_compass3d_ready()) {
        uint16_t entry_off = 0u, total_sz = 0u;
        const uint8_t *c3d = link_compass3d_blob(&entry_off, &total_sz);
        if (c3d) {
            static const compass3d_api_t s_c3d_tmpl = {
                .ui_w = UI_W,
                .ui_h = UI_H,
                .fill = ui_draw_fill,
                .round = ui_draw_round,
                .line = ui_draw_line,
                .text = ui_draw_text,
                .text_center = ui_text_draw_center,
                .text_h = ui_text_height,
                .imu_get = imu9_get,
                .imu_rot = imu9_get_rot,
            };
            compass3d_api_t api = s_c3d_tmpl;
            api.col_bg = col_bg;
            api.col_line = col_line;
            api.col_accent = col_accent;
            api.col_ring = col_ring;
            api.col_text = col_text;
            api.col_dim = col_dim;
            api.now_ms = hal_tick_ms();
#if defined(__ARM_ARCH) || defined(__thumb__)
            __asm volatile("dsb\n\tisb" ::: "memory");
            ((compass3d_entry_t)((uintptr_t)(c3d + entry_off) | 1u))(&api, a, clear_bg);
#else
            compass3d_render_c(&api, a, clear_bg);
#endif
            return;
        }
    }
    {
        uint16_t entry_off = 0u;
        const uint8_t *sblob = link_pic_sport_blob(&entry_off);
        if (pic_mod_exec_std(sblob, entry_off, 0, 0u, 2u, a, clear_bg)) {
            return;
        }
    }
#else
    imu9_state_t s9;
    int sc = UI_W >= 200 ? 2 : 1;
    int margin = UI_MX(M_MARGIN);
    int title_y = UI_MY(55);
    int title_h = ui_text_height("指", 1);
    int hdg_h = title_h * sc;
    int sub_y = UI_H - UI_MY(45) - title_h;
    int hdg_y = sub_y - UI_MY(24) - hdg_h;
    int cx = UI_W >> 1;
    int dial_top = title_y + title_h + UI_MY(32);
    int dial_bot = hdg_y - UI_MY(24);
    int cy = dial_top + ((dial_bot - dial_top) >> 1);
    char line[24];
    int max_r = (dial_bot - dial_top) >> 1;
    int w_r = UI_W * 22 / 100;
    int r = (max_r < w_r) ? max_r : w_r;
    int ring_w = r >= 40 ? 3 : 2;
    int needle_deg, dx, dy, r_in, pw, hub_r, n_dot, k;
    if (r < 18) r = 18;
    r_in = r - ring_w;
    pw = r >= 40 ? 4 : 2;
    hub_r = r >= 40 ? 5 : 3;
    n_dot = r >= 40 ? 4 : 2;

    poll_imu9_now(&s9, hal_tick_ms());
    if (link_compass3d_ready()) {
        uint16_t entry_off = 0u, total_sz = 0u;
        const uint8_t *c3d = link_compass3d_blob(&entry_off, &total_sz);
        if (c3d) {
            static const compass3d_api_t s_c3d_tmpl = {
                .ui_w = UI_W,
                .ui_h = UI_H,
                .fill = ui_draw_fill,
                .round = ui_draw_round,
                .line = ui_draw_line,
                .text = ui_draw_text,
                .text_center = ui_text_draw_center,
                .text_h = ui_text_height,
                .imu_get = imu9_get,
                .imu_rot = imu9_get_rot,
            };
            compass3d_api_t api = s_c3d_tmpl;
            api.col_bg = col_bg;
            api.col_line = col_line;
            api.col_accent = col_accent;
            api.col_ring = col_ring;
            api.col_text = col_text;
            api.col_dim = col_dim;
            api.now_ms = hal_tick_ms();
#if defined(__ARM_ARCH) || defined(__thumb__)
            __asm volatile("dsb\n\tisb" ::: "memory");
            ((compass3d_entry_t)((uintptr_t)(c3d + entry_off) | 1u))(&api, a, clear_bg);
#else
            compass3d_render_c(&api, a, clear_bg);
#endif
            return;
        }
    }

    if (clear_bg && a == 255) {
        ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    }

    ui_draw_round((int16_t)margin, (int16_t)(title_y + 1),
                  (int16_t)(sc + 2), (int16_t)(title_h - 2),
                  1, (s9.mag_cal == 1) ? col_accent : col_ring, a);
    ui_draw_text((int16_t)(margin + sc + 5), (int16_t)title_y,
                 "3D", col_text, a, 1);
    if (link_compass_loading()) {
        ui_draw_text((int16_t)(UI_W - margin - ui_text_width("3D", 1)),
                     (int16_t)title_y, "3D", col_accent, a, 1);
        return;
    }

    needle_deg = wrap_mod(s9.fused_axes ? -(int)s9.heading_deg : 0, 360);
    if (link_compass_asset_ready()) {
        int16_t aw = 0, ah = 0;
        const uint8_t *abits = link_compass_asset_mask(&aw, &ah);
        if (abits && aw > 0 && ah > 0) {
            ui_mask1_draw((int16_t)(cx - aw / 2), (int16_t)(cy - ah / 2),
                          aw, ah, abits, col_line, a);
            if (UI_W >= 200) {
                compass_disc(cx, cy, r, col_line, a);
                compass_disc(cx, cy, r_in, col_bg, a);
                ui_mask1_draw((int16_t)(cx - aw / 2), (int16_t)(cy - ah / 2),
                              aw, ah, abits, col_line, a);
            } else {
                r = 30;
                r_in = 27;
            }
        }
    } else {
        compass_disc(cx, cy, r, col_line, a);
        compass_disc(cx, cy, r_in, col_bg, a);
        ui_draw_fill((int16_t)(cx - sc), (int16_t)(cy - r - sc * 2 - 1),
                     (int16_t)(sc * 2), (int16_t)(sc * 2 + 2), col_ring, a);
    }

    for (k = 0; k < 8; k++) {
        int vx, vy, r_tick0, r_tick1 = r_in - 1;
        compass_vec(needle_deg + k * 45, &vx, &vy);
        r_tick0 = r_in - ((k & 1) ? (sc + 2) : (sc * 2 + 3));
        ui_draw_line((int16_t)(cx + ((vx * r_tick0) >> 8)),
                     (int16_t)(cy + ((vy * r_tick0) >> 8)),
                     (int16_t)(cx + ((vx * r_tick1) >> 8)),
                     (int16_t)(cy + ((vy * r_tick1) >> 8)),
                     k == 0 ? col_accent : ((k & 1) ? col_dim : col_text), a);
    }

    compass_vec(needle_deg, &dx, &dy);
    compass_disc(cx + ((dx * (r - 1)) >> 8),
                 cy + ((dy * (r - 1)) >> 8), n_dot, col_accent, a);

    compass_needle(cx, cy, -dx, -dy, r_in * 55 / 100, pw, col_dim, a);
    compass_needle(cx, cy, dx, dy, r_in * 78 / 100, pw, col_accent, a);
    compass_disc(cx, cy, hub_r, col_bg, a);
    compass_disc(cx, cy, hub_r - 1, col_ring, a);

    if (s9.fused_axes) {
        snprintf(line, sizeof line, "%03u度 %s",
                 (unsigned)s9.heading_deg, compass_dir_zh(s9.heading_deg));
    } else {
        snprintf(line, sizeof line, "---度 --");
    }
    ui_text_draw_center((int16_t)cx, (int16_t)hdg_y, line, col_text, a, (uint8_t)sc);

    if (s9.mag_cal == 1) {
        snprintf(line, sizeof line, "校准中 360度 K2");
    } else {
        const char *b = s9.fused_axes == 9 ? (s9.mag_cal == 2 ? "9DOF*" : "9DOF")
                      : (s9.fused_axes == 6 ? "6DOF"
                      : (s9.fused_axes ? "3DOF" : "--"));
        snprintf(line, sizeof line, "%s K2校准", b);
    }
    ui_text_draw_center((int16_t)cx, (int16_t)sub_y, line,
                        s9.mag_cal == 1 ? col_accent : col_dim, a, 1);
#endif
}

static void draw_sport_app(uint8_t a) {
    int sx = st->sport_scroll.x;
    int os, ox, oy;
    int dx0, dx1;

    if (sx < 0) sx = 0;
    if (sx > 256) sx = 256;
    if (sx == 0) {
        draw_sport_steps(a, 1);
        draw_sport_pager(0, a);
        return;
    }
    if (sx == 256) {
        draw_sport_compass(a, 1);
        draw_sport_pager(1, a);
        return;
    }
    ui_draw_get_xform(&os, &ox, &oy);
    dx0 = -(sx * UI_W / 256);
    dx1 = UI_W + dx0;
    if (a == 255) {
        ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    }
    if (dx0 + UI_W > 0) {
        ui_draw_xform(os, ox + dx0 * os / 256, oy);
        draw_sport_steps(a, 0);
    }
    if (dx1 < UI_W) {
        ui_draw_xform(os, ox + dx1 * os / 256, oy);
        draw_sport_compass(a, 0);
    }
    ui_draw_xform(os, ox, oy);
    draw_sport_pager(sx >= 128 ? 1 : 0, a);
}

static void draw_home_art(int page, uint8_t a) {
#ifdef AT32_SLIM_ROM
    uint16_t entry_off = 0u;
    const uint8_t *blob = link_pic_sys_blob(&entry_off);

    if (pic_mod_exec(blob, entry_off, settings_get(), col_text, col_accent, col_bg,
                     0, 0, 0, (uint8_t)page, 0u, a, 0)) {
        return;
    }
#else
    int x, y, w, h;
    home_art_rect(&x, &y, &w, &h);
    if (page == UI_PAGE_MSG) {
        ui_icon_mail((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                     hal_tick_ms(), col_ring, col_text, col_blue_dim, a);
    } else if (page == UI_PAGE_SETTINGS) {
        int rad = (w < h ? w : h) / 2;
        if (rad < 8) rad = 8;
        ui_icon_gear((int16_t)(x + w / 2), (int16_t)(y + h / 2), (int16_t)rad,
                     hal_tick_ms(), col_ring, col_blue_dim, a);
    } else if (page == UI_PAGE_SPORT) {
        int scale = w / 32;
        if (h / 42 < scale) scale = h / 42;
        if (scale < 1) scale = 1;
        ui_svg_draw_p((int16_t)(x + (w - 32 * scale) / 2),
                      (int16_t)(y + (h - 42 * scale) / 2),
                      (uint8_t)scale, run_svg[run_frame()], 10, icon_pal, 5, col_accent, a,
                      (uint8_t)(scale + 3 > 8 ? 8 : scale + 3));
    } else if (page == UI_PAGE_MUSIC) {
        ui_icon_music((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                      hal_tick_ms(), col_ring, col_accent, col_text, a);
    }
#endif
}

static void draw_sport_child(uint8_t a) {
    sport_stats_t ss;
    char l0[24];
    const char *names[3];
    int n = st->sport.child_count;
    int scroll = st->sport.child_scroll.x;
    int vis = vis_index(scroll, n);

    sport_get(&ss);
    snprintf(l0, sizeof l0, "%lu步", (unsigned long)ss.steps);
    names[0] = "重置步数";
    names[1] = "传感器数值";
    names[2] = "加1000步";
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    draw_stack(&st->sport, UI_MENU_CHILD, scroll, n, vis, names, l0, a);
    draw_header_anim("运动", scroll, a);
}

#if !defined(AT32_SLIM_ROM)
static void fmt_deg(char *out, int cap, char tag, int d10) {
    int v = d10 < 0 ? -d10 : d10;
    char sg = d10 < 0 ? '-' : '+';

    if (v >= 1000) {
        snprintf(out, (size_t)cap, "%c%c%3d", tag, sg, v / 10);
    } else {
        snprintf(out, (size_t)cap, "%c%c%d.%d", tag, sg, v / 10, v % 10);
    }
}
#endif

static const char *compass_dir(uint16_t heading) {
    static const char *const dirs[8] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    return dirs[((heading + 22u) % 360u) / 45u];
}

static void draw_sensor_page(uint8_t a) {
#ifdef AT32_SLIM_ROM
    uint32_t now = hal_tick_ms();
    uint16_t entry_off = 0u;
    const uint8_t *blob = link_pic_sport_blob(&entry_off);

    if (st->i2c_scan_ms == 0u || (now - st->i2c_scan_ms) >= 3000u) {
        uint8_t addrs[4];
        int na, i, p;
        st->i2c_scan_ms = now;
        na = hal_i2c_scan(addrs, 4);
        p = snprintf(st->i2c_scan_str, sizeof st->i2c_scan_str, "I2C");
        if (na == 0) {
            snprintf(st->i2c_scan_str + p, sizeof st->i2c_scan_str - (size_t)p, " --");
        } else {
            for (i = 0; i < na && p < (int)sizeof st->i2c_scan_str - 4; i++) {
                p += snprintf(st->i2c_scan_str + p, sizeof st->i2c_scan_str - (size_t)p,
                              " %02X", addrs[i]);
            }
        }
    }
    if (pic_mod_exec_std(blob, entry_off, st->i2c_scan_str, 0u, 1u, a, 1)) {
        return;
    }
#else
    static imu9_state_t s9;
    static uint32_t scan_ms;
    static char scan[20] = "I2C --";
    uint32_t now = hal_tick_ms();
    char line[28], rbuf[8], pbuf[8];
    const char *badge;
    uint16_t badge_col;
    int x = UI_MX(45);
    int y = UI_MY(175);
    int step = ui_text_height("A", 1) + UI_MY(12);
    int max_step = (badge_y() - 2 - y) / 7;
    int gx, gy, gz, t;

    if (step > max_step && max_step > 10) {
        step = max_step;
    }

    poll_imu9_now(&s9, now);
    /* 插拔模块后不用重启，3 秒重扫一次总线以降低空地址 NACK 干扰。 */
    if (scan_ms == 0u || (now - scan_ms) >= 3000u) {
        uint8_t addrs[4];
        int na, i, p;

        scan_ms = now;
        na = hal_i2c_scan(addrs, 4);
        p = snprintf(scan, sizeof scan, "I2C");
        if (na == 0) {
            snprintf(scan + p, sizeof scan - (size_t)p, " --");
        } else {
            for (i = 0; i < na && p < (int)sizeof scan - 4; i++) {
                p += snprintf(scan + p, sizeof scan - (size_t)p, " %02X", addrs[i]);
            }
        }
    }

    if (s9.mag_cal == 1) {
        badge = "CAL";
        badge_col = col_accent;
    } else if (s9.fused_axes == 9) {
        badge = "9DOF";
        badge_col = col_ring;
    } else if (s9.fused_axes) {
        badge = (s9.fused_axes == 6) ? "6DOF" : "3DOF";
        badge_col = col_accent;
    } else {
        badge = "--";
        badge_col = col_dim;
    }

    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    ui_draw_round((int16_t)x, (int16_t)UI_MY(40), (int16_t)UI_MX(20),
                  (int16_t)UI_MY(100), 1, col_accent, a);
    ui_draw_text((int16_t)(x + UI_MX(50)), (int16_t)UI_MY(30),
                 "传感器", col_text, a, 1);
    ui_draw_text((int16_t)(x + UI_MX(50) + ui_text_width("传感器", 1) + UI_MX(45)),
                 (int16_t)UI_MY(30), badge, badge_col, a, 1);
    ui_draw_line((int16_t)x, (int16_t)UI_MY(155),
                 (int16_t)(UI_W - x), (int16_t)UI_MY(155), col_line, a);

    if (s9.mag_cal == 1) {
        snprintf(line, sizeof line, "MAG CAL ROT360");
        ui_draw_text((int16_t)x, (int16_t)y, line, col_accent, a, 1);
    } else if (s9.imu_ok) {
        t = s9.temp;
        snprintf(line, sizeof line, "%uDOF%c T%d.%dC",
                 (unsigned)s9.fused_axes,
                 s9.mag_cal == 2 ? '*' : (s9.gyro_cal ? '.' : ' '),
                 t / 10, (t % 10 < 0) ? -(t % 10) : (t % 10));
        ui_draw_text((int16_t)x, (int16_t)y, line,
                     s9.fused_axes == 9 ? col_ring : col_accent, a, 1);
    } else {
        snprintf(line, sizeof line, "%s %s", hal_mag_name(), s9.mag_ok ? "3DOF" : "--");
        ui_draw_text((int16_t)x, (int16_t)y, line, s9.mag_ok ? col_accent : col_dim, a, 1);
    }

    if (s9.imu_ok) {
        fmt_deg(rbuf, (int)sizeof rbuf, 'R', s9.roll_d10);
        fmt_deg(pbuf, (int)sizeof pbuf, 'P', s9.pitch_d10);
        snprintf(line, sizeof line, "%-6s %-6s", rbuf, pbuf);
    } else {
        snprintf(line, sizeof line, "R --   P --");
    }
    ui_draw_text((int16_t)x, (int16_t)(y + step), line, col_text, a, 1);

    if (s9.fused_axes) {
        const char *cstr = s9.mag_cal == 1 ? "CAL" :
                           (s9.mag_cal == 2 ? "MCAL" :
                           (s9.gyro_cal ? "OK" : "INIT"));
        snprintf(line, sizeof line, "H%3u %-2s %s",
                 (unsigned)s9.heading_deg, compass_dir(s9.heading_deg), cstr);
    } else {
        snprintf(line, sizeof line, "H --- --");
    }
    ui_draw_text((int16_t)x, (int16_t)(y + step * 2), line,
                 s9.mag_ok ? col_ring : col_accent, a, 1);

    if (s9.imu_ok) {
        snprintf(line, sizeof line, "A%4d%4d%4d",
                 s9.ax / 100, s9.ay / 100, s9.az / 100);
    } else {
        snprintf(line, sizeof line, "A  --  --  --");
    }
    ui_draw_text((int16_t)x, (int16_t)(y + step * 3), line, col_text, a, 1);

    if (s9.imu_ok) {
        gx = s9.gx / 10;
        gy = s9.gy / 10;
        gz = s9.gz / 10;
        if (gx > 999) gx = 999; else if (gx < -999) gx = -999;
        if (gy > 999) gy = 999; else if (gy < -999) gy = -999;
        if (gz > 999) gz = 999; else if (gz < -999) gz = -999;
        snprintf(line, sizeof line, "G%4d%4d%4d", gx, gy, gz);
    } else {
        snprintf(line, sizeof line, "G  --  --  --");
    }
    ui_draw_text((int16_t)x, (int16_t)(y + step * 4), line, col_text, a, 1);

    if (s9.mag_ok) {
        snprintf(line, sizeof line, "M%4d%4d%4d",
                 s9.mx / 10, s9.my / 10, s9.mz / 10);
    } else {
        snprintf(line, sizeof line, "M  --  --  --");
    }
    ui_draw_text((int16_t)x, (int16_t)(y + step * 5), line, col_text, a, 1);

    snprintf(line, sizeof line, "%s %s", scan, s9.mag_cal == 1 ? "K2ok" : "K2c");
    ui_draw_text((int16_t)x, (int16_t)(y + step * 6), line, col_dim, a, 1);
#endif
}

static void draw_msg_card(int scroll, int count, int i, int hot,
                          int band, int pass, uint8_t a) {
    int x, y, w, h;
    int pad = UI_MX(35);
    int right_pad = UI_MX(50);
    int arrow_w = ui_text_width(">", 1) + UI_MX(25);
    int clip_w;

    list_card_rect(scroll, count, i, &x, &y, &w, &h);
    if (y + h <= band || y >= UI_H) {
        return;
    }
    if (pass == 0) {
        blue_card_bg(x, y, w, h, hot, h / 4, a);
        return;
    }
    if (i == count - 1) {
        draw_labeled_card_fg(x, y, w, h, hot, "清空通知", "!", a);
        return;
    }
    blue_card_fg(x, y, w, h, hot, a);
    ui_text_draw_right((int16_t)(x + w - right_pad),
                       (int16_t)(y + (h - 8) / 2), ">", col_dim, a, 1);
    clip_w = w - pad - right_pad - arrow_w;
    if (clip_w > 8) {
        const uint8_t *row_bits = link_msg_row_mask(i);
        if (row_bits) {
            ui_mask1_draw_clip((int16_t)(x + pad), (int16_t)(y + (h - 22) / 2),
                               (int16_t)clip_w, 22, 128,
                               row_bits,
                               hot ? col_text : col_dim, a);
        }
    }
}

static void draw_msg_app(uint8_t a) {
    int ready = link_msg_list_ready() && st->messages.count > 0;
    int scroll = ready ? st->messages.scroll.x : 0;
    int collapse = scroll <= 0 ? 0 : (scroll >= 256 ? 256 : scroll);
    int band = header_band_h(collapse);
    int vis = vis_index(scroll, st->messages.count);
    int pass;
    int i;

    if (a == 255) {
        ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    }
    if (ready) {
        for (pass = 0; pass < 2; pass++) {
            int k;
            for (k = 0; k <= st->messages.count; k++) {
                int hot = (k == st->messages.count);
                i = hot ? vis : k;
                if (!hot && i == vis) {
                    continue;
                }
                draw_msg_card(scroll, st->messages.count, i, hot, band, pass, a);
            }
            if (pass == 0) {
                draw_list_ripple(&st->messages, UI_MENU_LIST,
                                 scroll, st->messages.count, a);
            }
        }
    } else if (!link_connected() || link_msg_list_empty() || st->messages.count == 0 ||
               (uint32_t)(hal_tick_ms() - st->reveal_ms) >= 1000u) {
        int16_t ew = ui_song_width(UI_SONG_TITLE, "无消息");
        int16_t eh = ui_song_height(UI_SONG_TITLE);
        ui_song_draw(UI_SONG_TITLE,
                     (int16_t)((UI_W - ew) / 2),
                     (int16_t)(band + (UI_H - band - eh) / 2),
                     "无消息", UI_COL_MUTED_LIGHT, a);
    }
    /* 其余情况是列表正在加载：保持空白，避免“无消息”闪一下。 */
    draw_header_anim("消息", scroll, a);
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
    case 5: v = s->led_mode == 0 ? "关闭" : (s->led_mode == 2 ? "闪烁" : "常亮"); break;
    case 6: v = screen_off_name[s->screen_off <= 4 ? s->screen_off : 0]; break;
    case 7:
        buf[0] = (s->spi_mhz == 15u) ? '1' : '3';
        buf[1] = (s->spi_mhz == 15u) ? '5' : '0';
        buf[2] = 'M';
        buf[3] = 0;
        return;
    default: break;
    }
    if (v) {
        snprintf(buf, (size_t)cap, "%s", v);
    } else {
        buf[0] = 0;
    }
}

static void settings_side_cb(int idx, char *buf, int cap, const void *ctx) {
    (void)ctx;
    settings_side(idx, buf, cap);
}

static void draw_settings_app(uint8_t a) {
    int scroll = st->settings.scroll.x;
    int vis = vis_index(scroll, SET_N);

    if (a == 255) {
        ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    }
    draw_stack_ex(&st->settings, UI_MENU_LIST, scroll, SET_N, vis,
                  set_name, settings_side_cb, 0, a);
    draw_header_anim("设置", scroll, a);
}

static void draw_message_page(uint8_t a) {
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    if (link_img_preview_ready()) {
        int16_t iw = 0, ih = 0;
        uint8_t fmt = 0;
        uint32_t ext_off = 0;
        link_img_preview_info(&iw, &ih, &fmt, &ext_off);
        ui_ext_image_draw((int16_t)((UI_W - iw) / 2), (int16_t)((UI_H - ih) / 2),
                          iw, ih, ext_off, fmt,
                          fmt == UI_EXT_FMT_1BPP ? col_text : 0u, a);
        return;
    }
    if (link_msg_detail_ready()) {
        /* 详情页不要标题，整屏都是手机渲染好的 128x160 1bpp 正文。 */
        ui_mask1_draw((int16_t)((UI_W - 128) / 2), (int16_t)((UI_H - 160) / 2),
                      128, 160, link_msg_detail_mask(), col_text, a);
    }
    /* 加载中保持空白，避免每次进来先闪“无消息”。 */
}

static void draw_about_page(uint8_t a) {
    int band = header_band_h(256);
    uint16_t tot = 0u;
    const uint8_t *blob = link_about_blob(&tot);

    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    if (blob && tot >= 40u) {
        const uint16_t line_cols[3] = { col_ring, col_text, col_dim };
        uint8_t w0 = blob[4], h0 = blob[5];
        uint16_t off0 = (uint16_t)blob[8] | ((uint16_t)blob[9] << 8);
        int img_gap = 2;
        int line_gap = 2;
        int y0 = band + 1;
        int cy, i;

        /* 图片与三行文字从上到下铺满内容区，不再为底部安全区留白。 */
        ui_mem_image_draw((int16_t)((UI_W - (int)w0) / 2), (int16_t)y0,
                          (int16_t)w0, (int16_t)h0,
                          blob + off0, 1u, a);
        cy = y0 + (int)h0 + img_gap;
        for (i = 0; i < 3; i++) {
            const uint8_t *d = blob + 16 + i * 8;
            uint16_t ol = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
            uint16_t orr = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
            uint8_t gw = d[4], gh = d[5], wl = d[6];
            uint8_t wr = (uint8_t)(gw - wl);
            uint16_t c = line_cols[d[7] < 3u ? d[7] : 1u];
            int lx = (UI_W - (int)gw) / 2;

            ui_mask_rle_draw((int16_t)lx, (int16_t)cy, (int16_t)wl, (int16_t)gh,
                             blob + ol, c, a);
            if (wr > 0u) {
                ui_mask_rle_draw((int16_t)(lx + wl), (int16_t)cy,
                                 (int16_t)wr, (int16_t)gh,
                                 blob + orr, c, a);
            }
            cy += (int)gh + line_gap;
        }
    }
    /* “关于”按 0.5x 折叠比例以华文琥珀显示，腾出纵向空间给放大彩图与简介 */
    draw_header_anim("关于", 256, a);
}

static void draw_detail(uint8_t a) {
    int kind = st->settings.level == UI_MENU_CHILD ? st->settings.child : -1;
    int scroll = st->settings.child_scroll.x;

    if (a == 0) {
        return;
    }
    if (st->messages.level == UI_MENU_CHILD) {
        draw_message_page(a);
        return;
    }
    if (kind == 7) {
        draw_about_page(a);
        return;
    }
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, a);
    draw_stack(&st->settings, UI_MENU_CHILD, scroll, DETAIL_N,
               vis_index(scroll, DETAIL_N), bright_name, 0, a);
    draw_header_anim("亮度", scroll, a);
}

static int draw_badge(void) {
    uint32_t ram_used = 0;
    uint32_t ram_total = 0;
    uint16_t cpu;
    char ram[32];
    int16_t y;
    uint32_t sig = 2166136261u;
    int i;
    int badge_tick;

    if (!st || st->overlay.opacity == 0) {
        if (st) {
            st->badge_op = 0;
        }
        return 0;
    }
    hal_ram_get(&ram_used, &ram_total);
    cpu = hal_cpu_tenths();
    if (cpu > 1000) {
        cpu = 1000;
    }
    snprintf(ram, sizeof ram, "R%lu/%luK C%u%% %u",
             (unsigned long)((ram_used + 512u) / 1024u),
             (unsigned long)((ram_total + 512u) / 1024u),
             (unsigned)cpu / 10u, (unsigned)st->fps);
    for (i = 0; ram[i]; i++) {
        sig = (sig ^ (uint8_t)ram[i]) * 16777619u;
    }
    badge_tick = (st->badge_op != 0 &&
                  st->badge_op == st->overlay.opacity &&
                  sig != st->badge_sig);
    st->badge_sig = sig;
    st->badge_op = st->overlay.opacity;
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
    return badge_tick;
}

static ui_menu_t *active_menu(void) {
    ui_menu_t *const ms[4] = { &st->settings, &st->messages, &st->sport, &st->music };
    int i;
    for (i = 0; i < 4; i++) {
        if (ui_menu_level(ms[i]) > UI_MENU_ICON || ms[i]->zoom.scale > 0) {
            return ms[i];
        }
    }
    return 0;
}

static ui_menu_t *live_menu(void) {
    ui_menu_t *m = active_menu();
    if (m) {
        return m;
    }
    switch (st->focus) {
    case UI_PAGE_SETTINGS: return &st->settings;
    case UI_PAGE_MSG:      return &st->messages;
    case UI_PAGE_SPORT:    return &st->sport;
    case UI_PAGE_MUSIC:    return &st->music;
    default:               return 0;
    }
}

static void menu_list_state(const ui_menu_t *menu, int *level, int *scroll,
                            int *count, int *focus) {
    if (!menu) {
        *level = UI_MENU_ICON;
        *scroll = *count = *focus = 0;
        return;
    }
    *level = menu->level;
    if (menu->level == UI_MENU_CHILD) {
        *scroll = menu->child_scroll.x;
        *count = menu->child_count;
        *focus = menu->child_focus;
    } else {
        *scroll = menu->scroll.x;
        *count = menu->count;
        *focus = menu->focus;
    }
}

static void on_confirm(ui_menu_t *menu) {
    settings_t *s = settings_get();
    int is_settings = menu == &st->settings;
    int result;

    if (is_settings && menu->level == UI_MENU_CHILD && menu->child == 3) {
        rip_trigger_expand(menu, UI_MENU_CHILD, menu->child_focus, 0);
        s->brightness = (uint8_t)menu->child_focus;
        if (!st->screen_asleep) {
            hal_backlight_set(backlight_level());
        }
        (void)settings_save_ext(UI_CFG_PRIO_I2C);
        link_notify_settings();
        link_notify_config();
        ui_menu_back(menu);
        return;
    }
    if (menu == &st->sport && menu->level == UI_MENU_CHILD) {
        rip_trigger_expand(menu, UI_MENU_CHILD, menu->child_focus, 0);
        if (menu->child_focus == 0) {
            sport_reset_today();
            ui_menu_back(menu);
        } else if (menu->child_focus == 1) {
            st->sensor_open = 1;
        } else if (menu->child_focus == 2) {
            sport_debug_add_steps(1000);
        }
        return;
    }
    if (is_settings && menu->level == UI_MENU_CHILD && menu->child == 8) {
        rip_trigger_expand(menu, UI_MENU_CHILD, menu->child_focus, 0);
        return;
    }
    result = ui_menu_confirm(menu);
    if (result == UI_MENU_OPENED) {
        st->reveal_ms = hal_tick_ms();
        if (menu == &st->messages) {
            /* 打开一级消息页才向手机要列表（或从外置 Flash 恢复），节省手表 RAM。 */
            link_request_messages();
        } else if (menu == &st->music) {
            /* 打开音乐页时按需分配动态缓冲并从外置 Flash 恢复曲名点阵。 */
            link_enter_music();
        } else if (menu == &st->sport) {
            st->sport_tab = 0;
            ui_anim_stop(&st->sport_scroll, 0);
            st->sport_scroll.x = 0;
        }
    }
    if (result != UI_MENU_ROW) {
        return;
    }
    if (is_settings) {
        rip_trigger_expand(menu, UI_MENU_LIST, menu->focus, 0);
        switch (menu->focus) {
        case 0:
            ui_settings_set_debug(!ui_settings_get_debug());
            break;
        case 1:
            s->raise_wrist = (uint8_t)!s->raise_wrist;
            break;
        case 2:
            s->key_beep = (uint8_t)!s->key_beep;
            if (!s->key_beep) {
                hal_beep(HAL_VIB_STOP, 0u);
            }
            break;
        case 3:
            ui_menu_enter_child(menu, DETAIL_N);
            break;
        case 4:
            s->bluetooth = (uint8_t)!s->bluetooth;
            break;
        case 5:
            /* 常亮 -> 闪烁 -> 关闭 -> 常亮，正好对应 HAL 的 1/2/0。 */
            s->led_mode = (uint8_t)((s->led_mode + 1u) % 3u);
            hal_led_mode(s->led_mode);
            break;
        case 6:
            /* 关 -> 30秒 -> 1分钟 -> 2分钟 -> 5分钟。 */
            s->screen_off = (uint8_t)((s->screen_off + 1u) % 5u);
            st->active_ms = hal_tick_ms();
            break;
        case 7:
            /* 15MHz 与 30MHz 手动切换；30MHz 为实验档。 */
            s->spi_mhz = (s->spi_mhz == 15u) ? 30u : 15u;
            hal_display_set_clock(s->spi_mhz);
            break;
        case 8:
            ui_menu_enter_child(menu, ABOUT_N);
            break;
        default:
            break;
        }
        (void)settings_save_ext(UI_CFG_PRIO_I2C);
        link_notify_settings();
        link_notify_config();
        return;
    }
    if (menu == &st->sport) {
        st->sensor_open = 0;
        ui_menu_enter_child(menu, ui_settings_get_debug() ? 3 : 2);
        return;
    }
    if (menu == &st->music) {
        music_page_key(HAL_KEY2);
        return;
    }
    if (menu == &st->messages) {
        /* 列表还在加载或确实没有消息时，不进入二级页。 */
        if (!link_msg_list_ready() || menu->count <= 0 ||
            menu->focus >= menu->count) {
            return;
        }
        if (menu->focus == menu->count - 1) {
            rip_trigger_expand(menu, UI_MENU_LIST, menu->focus, 0);
            link_clear_ext_messages();
            return;
        }
        rip_trigger_expand(menu, UI_MENU_LIST, menu->focus, 0);
        link_request_message_detail(menu->focus);
    }
    ui_menu_enter_child(menu, 1);
    ui_trace_emit("ui_nav_goto(message)");
}

static void dial_edit_sync_slide(uint8_t id) {
    st->dial_pick = (uint8_t)(id % UI_DIAL_COUNT);
    st->dial_pick_steps = st->dial_pick;
    ui_anim_stop(&st->dial_slide, 0);
    st->dial_slide.x = (int16_t)(st->dial_pick_steps * 256);
}

static void dial_edit_reset(void) {
    st->dial_edit = 0;
    ui_anim_stop(&st->dial_zoom, 0);
    st->dial_zoom.scale = 0;
    dial_edit_sync_slide(settings_get()->dial_id);
}

static void on_key(uint8_t pressed, uint8_t repeat_keys, int long_enter) {
    ui_menu_t *menu;
    uint8_t step_keys = (uint8_t)(pressed | repeat_keys);

    if (!st) {
        return;
    }
    if (step_keys || long_enter) {
        st->active_ms = hal_tick_ms();
    }
    menu = live_menu();
    if (st->sensor_open && menu == &st->sport) {
        if (long_enter || (pressed & HAL_KEY1)) {
            if (long_enter) {
                vib_play(HAL_VIB_SINGLE, 130u);
            } else {
                vib_play(HAL_VIB_SINGLE, 38u);
            }
            st->sensor_open = 0;
            return;
        }
        if (pressed & HAL_KEY2) {
            vib_play(HAL_VIB_DOUBLE, 65u);
            imu9_cal_start();
            return;
        }
        if (pressed & HAL_KEY3) {
            vib_play(HAL_VIB_SINGLE, 38u);
            imu9_reset();
            return;
        }
        if (pressed) {
            return;
        }
    }
    if (long_enter) {
        if (menu && ui_menu_level(menu) > UI_MENU_ICON) {
            vib_play(HAL_VIB_SINGLE, 130u);
            st->rip_mode = RIP_NONE;
            ui_menu_back(menu);
        } else if (!menu && st->focus == UI_PAGE_FACE) {
            vib_play(HAL_VIB_SINGLE, 130u);
            if (st->dial_edit) {
                st->dial_edit = 0;
                dial_edit_sync_slide(settings_get()->dial_id);
                ui_anim_play_range(UI_ANIM_ZOOM_OUT, &st->dial_zoom, st->dial_zoom.scale, 0);
            } else {
                st->dial_edit = 1;
                dial_edit_sync_slide(settings_get()->dial_id);
                ui_anim_play_range(UI_ANIM_ZOOM_IN, &st->dial_zoom, st->dial_zoom.scale, 255);
            }
            ui_draw_invalidate();
        }
        return;
    }
    if (!step_keys) {
        return;
    }
    if (pressed != 0) {
        vib_play(HAL_VIB_SINGLE, 38u);
    }
    if (st->dial_edit && !menu && st->focus == UI_PAGE_FACE) {
        settings_t *s = settings_get();
        int dir = 0;
        if (step_keys & HAL_KEY1) {
            dir--;
        }
        if (step_keys & HAL_KEY3) {
            dir++;
        }
        if (dir != 0) {
            st->dial_pick_steps = (int16_t)(st->dial_pick_steps + dir);
            st->dial_pick = (uint8_t)wrap_mod(st->dial_pick_steps, UI_DIAL_COUNT);
            ui_anim_play_range(UI_ANIM_STACK, &st->dial_slide,
                               st->dial_slide.x, (int16_t)(st->dial_pick_steps * 256));
        }
        if (pressed & HAL_KEY2) {
            s->dial_id = st->dial_pick;
            st->dial_edit = 0;
            (void)settings_save_ext(UI_CFG_PRIO_I2C);
            link_notify_config();
            ui_anim_play_range(UI_ANIM_ZOOM_OUT, &st->dial_zoom, st->dial_zoom.scale, 0);
        }
        ui_draw_invalidate();
        return;
    }
    if (menu == &st->music && ui_menu_level(menu) > UI_MENU_ICON) {
        if (pressed) {
            music_page_key(pressed);
        }
        return;
    }
    if (menu && ui_menu_level(menu) > UI_MENU_ICON) {
        int cur_lvl, old_scroll, n, old_focus, new_focus;
        if (menu == &st->sport && menu->level == UI_MENU_LIST) {
            if (pressed & HAL_KEY3) {
                st->sport_tab = st->sport_tab ? 0 : 1;
                ui_anim_play_range(UI_ANIM_STACK, &st->sport_scroll,
                                   st->sport_scroll.x,
                                   (int16_t)(st->sport_tab * 256));
            } else if (pressed & HAL_KEY1) {
                if (st->sport_tab != 0) {
                    st->sport_tab = 0;
                    ui_anim_play_range(UI_ANIM_STACK, &st->sport_scroll,
                                       st->sport_scroll.x, 0);
                } else {
                    st->rip_mode = RIP_NONE;
                    ui_anim_stop(&st->sport_scroll, 0);
                    st->sport_scroll.x = 0;
                    ui_menu_back(menu);
                }
            } else if (pressed & HAL_KEY2) {
                if (st->sport_tab == 1) {
                    vib_play(HAL_VIB_DOUBLE, 65u);
                    imu9_cal_start();
                } else {
                    on_confirm(menu);
                }
            }
            return;
        }
        if ((menu == &st->messages && menu->level == UI_MENU_LIST &&
             (!link_msg_list_ready() || menu->count <= 0)) ||
            (menu == &st->messages && menu->level == UI_MENU_CHILD)) {
            if (pressed & HAL_KEY1) {
                st->rip_mode = RIP_NONE;
                ui_menu_back(menu);
            }
            return;
        }
        menu_list_state(menu, &cur_lvl, &old_scroll, &n, &old_focus);
        if (step_keys & HAL_KEY1) {
            int busy_zoom = (cur_lvl == UI_MENU_LIST &&
                             (menu->zoom.scale < 255 || ui_anim_busy(&menu->zoom)));
            if ((pressed & HAL_KEY1) && old_focus == 0 && !busy_zoom) {
                /* 已在第一项时再点一次后退才返回上一级；长按滑到第一项则停在第一项。 */
                st->rip_mode = RIP_NONE;
                ui_menu_prev(menu);
            } else {
                if (pressed & HAL_KEY1) {
                    ui_menu_prev(menu);
                } else {
                    ui_menu_prev_no_back(menu);
                }
                if (busy_zoom) {
                    st->rip_mode = RIP_NONE;
                } else {
                    new_focus = cur_lvl == UI_MENU_CHILD ? menu->child_focus : menu->focus;
                    rip_trigger_move(menu, cur_lvl, old_focus, new_focus, old_scroll, n);
                }
            }
        }
        if (step_keys & HAL_KEY3) {
            int busy_zoom = (cur_lvl == UI_MENU_LIST &&
                             (menu->zoom.scale < 255 || ui_anim_busy(&menu->zoom)));
            ui_menu_next(menu);
            if (!busy_zoom) {
                new_focus = cur_lvl == UI_MENU_CHILD ? menu->child_focus : menu->focus;
                rip_trigger_move(menu, cur_lvl, old_focus, new_focus, old_scroll, n);
            }
        }
        if (pressed & HAL_KEY2) {
            on_confirm(menu);
        }
        return;
    }
    if (step_keys & HAL_KEY1) {
        ui_anim_stop(&st->dial_zoom, 1);
        slide_to(&st->scroll, &st->focus, -1, HOME_N);
    }
    if (step_keys & HAL_KEY3) {
        ui_anim_stop(&st->dial_zoom, 1);
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

    col_bg = UI_COL_BG;
    col_text = UI_COL_TEXT;
    col_dim = UI_COL_MUTED;
    col_blue_dim = UI_COL_SURFACE;
    col_ring = UI_COL_MINT;
    col_badge = UI_COL_SURFACE;
    col_dot = UI_COL_LINE;
    col_accent = UI_COL_AMBER;
    col_line = UI_COL_LINE;
    ui_menu_init(&st->settings, SET_N);
    ui_menu_init(&st->messages, MSG_N);
    ui_menu_init(&st->sport, 1);
    ui_menu_init(&st->music, 1);
    st->sensor_open = 0;
    link_init();
    sport_init();
    imu9_init();
    settings_init();
    if (settings_load_ext()) {
        settings_t *s = settings_get();
        imu9_state_t s9;
        st->debug = s->debug ? 1 : 0;
        if (st->debug) {
            st->overlay.opacity = 255;
        }
        imu9_get(&s9);
        st->prev_mag_cal = s9.mag_cal;
    }
    hal_display_set_clock(settings_get()->spi_mhz);
    link_load_ext();
    dial_edit_reset();
    st->screen_asleep = 0;
    st->active_ms = hal_tick_ms();
    hal_backlight_set(backlight_level());
    hal_led_mode(settings_get()->led_mode);
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
    dial_edit_reset();
    st->focus = (int)page;
    st->scroll.x = (int16_t)((int)page * 256);
}

/* PC 下发媒体点阵时调用：只有停在表盘或音乐页才自动打开，
   避免把正在看消息/设置的界面强行拉走。 */
void ui_app_open_music(void) {
    if (!st) {
        return;
    }
    if (st->focus != UI_PAGE_FACE && st->focus != UI_PAGE_MUSIC) {
        return;
    }
    if (ui_menu_level(&st->music) > UI_MENU_ICON) {
        return;
    }
    ui_nav_goto(UI_PAGE_MUSIC);
    st->focus = UI_PAGE_MUSIC;
    on_confirm(&st->music);
}

/* 返回消息应用所处层级：0=未在消息界面，1=列表，2=详情。 */
int ui_app_message_active(void) {
    if (!st || st->focus != UI_PAGE_MSG) {
        return 0;
    }
    return ui_menu_level(&st->messages);
}

/* 音乐应用是否处于打开状态（退出回第 0 级缩放至 70 以下时提前视为关闭）。 */
int ui_app_music_active(void) {
    if (!st) {
        return 0;
    }
    return st->focus == UI_PAGE_MUSIC &&
           ui_menu_level(&st->music) > UI_MENU_ICON &&
           !(st->music.zoom.scale < 70 && ui_anim_closing(&st->music.zoom));
}

/* 手机下发 0x25：列表实际条数；有消息时末尾附加“清除通知”操作行。 */
void ui_app_set_message_count(int count) {
    int prev_focus;
    int total_rows;

    if (!st) {
        return;
    }
    if (count < 0) {
        count = 0;
    }
    if (count > LINK_EXT_MSG_MAX) {
        count = LINK_EXT_MSG_MAX;
    }
    total_rows = count > 0 ? (count + 1) : 0;
    prev_focus = st->messages.focus;
    st->messages.count = total_rows;
    if (st->messages.focus >= total_rows) {
        st->messages.focus = total_rows > 0 ? total_rows - 1 : 0;
    }
    if (total_rows == 0 || prev_focus != st->messages.focus ||
        st->messages.scroll.x > (total_rows - 1) * 256) {
        ui_anim_stop(&st->messages.scroll, 0);
        st->messages.scroll.x = (int16_t)(st->messages.focus * 256);
    }
    if (st->messages.child_focus >= count) {
        st->messages.child_focus = 0;
    }
}

void ui_settings_set_debug(int enabled) {
    if (!st) {
        return;
    }
    st->debug = enabled ? 1 : 0;
    settings_get()->debug = (uint8_t)(enabled ? 1 : 0);
    ui_anim_play(enabled ? UI_ANIM_FADE_IN : UI_ANIM_FADE_OUT, &st->overlay);
}

int ui_settings_get_debug(void) {
    return st ? st->debug : 0;
}

/* App 远程修改手表设置：1 抬腕 2 按键音 3 亮度 4 蓝牙 5 状态灯 6 熄屏。 */
void ui_settings_apply(int key, int value) {
    settings_t *s = settings_get();
    if (!st) {
        return;
    }
    switch (key) {
    case 1:
        s->raise_wrist = (uint8_t)(value ? 1 : 0);
        break;
    case 2:
        s->key_beep = (uint8_t)(value ? 1 : 0);
        break;
    case 3:
        s->brightness = (uint8_t)(value <= 0 ? 0 : (value >= 2 ? 2 : 1));
        if (!st->screen_asleep) {
            hal_backlight_set(backlight_level());
        }
        break;
    case 4:
        s->bluetooth = (uint8_t)(value ? 1 : 0);
        break;
    case 5:
        s->led_mode = (uint8_t)(value <= 0 ? 0 : (value >= 2 ? 2 : 1));
        hal_led_mode(s->led_mode);
        break;
    case 6:
        s->screen_off = (uint8_t)(value <= 0 ? 0 : (value >= 4 ? 4 : value));
        st->active_ms = hal_tick_ms();
        break;
    default:
        break;
    }
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
    int ez, page;
    int ix, iy, iw, ih;
    int x, y, w, h, rad, sc;

    if (z <= 0) {
        return;
    }
    if (z >= 255) {
        ui_draw_xform(256, 0, 0);
        draw_app_content(menu, 255);
        return;
    }
    page = menu == &st->sport ? UI_PAGE_SPORT :
           (menu == &st->messages ? UI_PAGE_MSG :
           (menu == &st->settings ? UI_PAGE_SETTINGS : UI_PAGE_MUSIC));
    ez = (z * (z + 255)) / 510;
    home_icon_rect(&ix, &iy, &iw, &ih);
    x = ix - ix * ez / 255;
    y = iy - iy * z / 255;
    w = iw + (UI_W - iw) * ez / 255;
    h = ih + (UI_H - ih) * z / 255;
    rad = (ih / 4) * (255 - z) / 255;

    ui_draw_xform(256, 0, 0);
    ui_draw_fill(0, 0, UI_W, UI_H, col_bg, 255);
    if (z < 180) {
        home_caption_y(page_names[page], y + h + UI_MY(60),
                       (uint8_t)((180 - z) * 255 / 180));
    }
    if (rad >= 2) {
        ui_draw_round((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                      (int16_t)rad, col_line, (uint8_t)(255 - z));
        ui_draw_round((int16_t)(x + 1), (int16_t)(y + 1),
                      (int16_t)(w - 2), (int16_t)(h - 2),
                      (int16_t)(rad - 1), z < 70 ? col_blue_dim : col_bg, 255);
    }
    if (z < 70) {
        int art_sc = w * 256 / iw;
        ui_draw_xform(art_sc,
                      x + w / 2 - (ix + iw / 2) * art_sc / 256,
                      y + h / 2 - (iy + ih / 2) * art_sc / 256);
        draw_home_art(page, 255);
        ui_draw_xform(256, 0, 0);
        ui_draw_round((int16_t)(x + 1), (int16_t)(y + 1),
                      (int16_t)(w - 2), (int16_t)(h - 2),
                      (int16_t)(rad > 1 ? rad - 1 : 0), col_bg,
                      (uint8_t)(z * 255 / 70));
    } else {
        uint8_t content_a = (uint8_t)((z - 70) * 255 / 185);
        sc = w * 256 / UI_W;
        if (sc < 1) sc = 1;
        ui_draw_xform(sc, x, y);
        draw_app_content(menu, content_a);
    }
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
    if (t < 256) {
        parent_s = 256 - t * 36 / 256;
        parent_dx = -t * 28 / 256;
        margin_x = (UI_W - UI_W * parent_s / 256) / 2;
        margin_y = (UI_H - UI_H * parent_s / 256) / 2;
        ui_draw_xform(parent_s, parent_dx + margin_x, margin_y);
        draw_app_content(menu, 255);
        ui_draw_xform(parent_s, parent_dx + margin_x, margin_y);
        ui_draw_fill(0, 0, UI_W, UI_H, 0, (uint8_t)(t * 100 / 256));
    }
    ui_draw_xform(256, UI_W - t * UI_W / 256, 0);
    if (menu == &st->sport) {
        if (st->sensor_open) {
            draw_sensor_page(255);
        } else {
            draw_sport_child(255);
        }
    } else {
        draw_detail(255);
    }
}

static int menu_closing(const ui_menu_t *m) {
    return m->level == UI_MENU_LIST && m->zoom.scale < 70 &&
           ui_anim_closing(&m->zoom);
}

static void msg_link_sync(void) {
    int level = st->messages.level;
    ui_menu_t *act = active_menu();
    int closing = act && menu_closing(act);
    /* 0→1 展开的前 70/255 仍在画主页图标；此时不要切到页面数据，
       否则 SYS1 会被释放，展开首段只剩占位文字。 */
    int icon_phase = act && act->level != UI_MENU_ICON &&
                     act->zoom.scale < 70 && !closing;
    int open = (act == &st->messages && level != UI_MENU_ICON && !closing && !icon_phase);
    int music_open = (act == &st->music && st->music.level != UI_MENU_ICON &&
                      !closing && !icon_phase);
    int sport_open = (st->focus == UI_PAGE_SPORT && act == &st->sport &&
                      st->sport.zoom.scale >= 70 && !closing);
    int compass_open = (sport_open && ui_menu_level(&st->sport) == UI_MENU_LIST &&
                        (st->sport_tab == 1 || st->sport_scroll.x > 0));
    int about_open = (st->focus == UI_PAGE_SETTINGS &&
                      st->settings.child == 8 &&
                      (st->settings.level == UI_MENU_CHILD || st->settings.push.x > 0));
    int analog_open = (st->focus == UI_PAGE_FACE &&
                       !ui_anim_busy(&st->scroll) && st->scroll.x == 0 &&
                       !act &&
                       (settings_get()->dial_id == UI_DIAL_ANALOG ||
                        settings_get()->dial_id == UI_DIAL_IMU ||
                        st->dial_edit || st->dial_zoom.scale > 0));
    int sys_open = (!sport_open && !about_open && !analog_open && !open && !music_open);
    int hdr_need = !compass_open;
    int sport_pic_ready;

    /* 从详情返回列表时：若有外置 Flash 缓存将立即恢复列表缓冲，并通知手机刷新。 */
    if (st->msg_prev_level == UI_MENU_CHILD && level == UI_MENU_LIST) {
        link_request_messages();
    }
    if (st->msg_was_open && !open) {
        /* 离开消息应用：释放消息动态 RAM 缓冲并让手机恢复媒体推送。 */
        link_leave_messages();
    }
    if (st->music_was_open && !music_open) {
        /* 离开音乐应用：释放媒体点阵动态 RAM 缓冲。 */
        link_leave_music();
    }
    if (st->compass_was_open && !compass_open) {
        /* 离开指南针：先释放 C3D1 动态 RAM 再允许重新加载 SHD1 标题池。 */
        link_leave_compass();
    }
    if (st->about_was_open && !about_open) {
        /* 离开关于页：释放 ABT2 动态 RAM 缓冲。 */
        link_leave_about();
    }
    if (st->sport_open_was && !sport_open) {
        link_leave_pic_sport();
    }
    if (st->analog_open_was && !analog_open) {
        link_leave_pic_analog();
    }
    if (st->sys_open_was && !sys_open) {
        link_leave_pic_sys();
    }
    link_hdr_active(hdr_need);
    if (sys_open) {
        /* 退出一级页面时让 SYS1 按帧后台载入，不再同步阻塞收尾动画；
           未就绪的短暂窗口显示“加载中”。 */
        link_begin_pic_sys();
    }
    if (analog_open && (!st->analog_open_was || !link_pic_analog_ready())) {
        link_begin_pic_analog();
    }
    if (about_open && (!st->about_was_open || !link_about_blob(0))) {
        link_begin_about();
    }
    if (sport_open && (!st->sport_open_was ||
                       (!link_pic_sport_ready() && !compass_open && !link_compass_loading()))) {
        link_begin_pic_sport();
    }
    sport_pic_ready = link_pic_sport_ready();
    if (sport_open && sport_pic_ready && !st->sport_pic_was) {
        /* 模块增量载入完成时再起算柱状图揭幕动画，避免首帧被载入耗时吞掉。 */
        st->reveal_ms = hal_tick_ms();
    }
    if (compass_open && (!st->compass_was_open || !link_compass3d_ready())) {
        link_begin_compass();
    }
    st->about_was_open = (uint8_t)about_open;
    st->compass_was_open = (uint8_t)compass_open;
    st->sport_open_was = (uint8_t)sport_open;
    st->sport_pic_was = (uint8_t)sport_pic_ready;
    st->analog_open_was = (uint8_t)analog_open;
    st->sys_open_was = (uint8_t)sys_open;
    st->msg_was_open = (uint8_t)open;
    st->msg_prev_level = (uint8_t)level;
    st->music_was_open = (uint8_t)music_open;
}

static int dot_alpha(uint32_t now) {
    int page = vis_index(st->scroll.x, HOME_N);
    int moving = ui_anim_busy(&st->scroll);
    uint32_t idle;

    if (moving || page != st->dot_page) {
        st->dot_ms = now;
        st->dot_page = page;
    }
    if (st->dial_edit || st->dial_zoom.scale > 0) {
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

static void fps_update(uint32_t pixels, int badge_tick) {
    uint32_t now = hal_tick_ms();
    uint32_t dt;
    uint32_t badge_max = (uint32_t)UI_W * (uint32_t)(badge_h() + 2 * UI_BAND_H);
    int counted = 0;

    /* 计入真实完成的局部刷新与全屏传输；排除仅调试角标数字变化引起的重绘及其自愈帧。 */
    if (pixels > 0) {
        if (badge_tick && pixels <= badge_max) {
            st->badge_heal = 1;
        } else if (st->badge_heal && pixels == (uint32_t)UI_W * UI_H) {
            st->badge_heal = 0;
        } else {
            st->badge_heal = 0;
            if (now != st->fps_window || st->fps_frames > 0) {
                if (st->fps_frames < 1000u) {
                    st->fps_frames++;
                }
                counted = 1;
            }
        }
    } else {
        st->badge_heal = 0;
    }

    dt = now - st->fps_window;
    if (dt >= 250u) {
        uint32_t inst = (uint32_t)st->fps_frames * 1000u / dt;
        uint32_t next;
        if (inst > 99u) {
            inst = 99u;
        }
        if (st->fps_frames == 0) {
            next = ((uint32_t)st->fps * 3u) / 8u;
        } else if (st->fps == 0) {
            next = inst;
        } else if (inst > st->fps) {
            next = ((uint32_t)st->fps + inst * 3u + 2u) / 4u;
        } else {
            next = ((uint32_t)st->fps + inst) / 2u;
        }
        if (next <= 1u) {
            next = 0;
        }
        if (next > 99u) {
            next = 99u;
        }
        st->fps = (uint8_t)next;
        st->fps_frames = 0;
        st->fps_window = now;
    } else if (!counted && st->fps == 0 && st->fps_frames == 0) {
        /* 静止归零后将窗口起点对齐到最后一帧空闲时刻，避免短促滑屏首帧被前置空闲稀释。 */
        st->fps_window = now;
    }
}

void ui_frame(void) {
    uint8_t level;
    uint8_t pressed;
    uint8_t released;
    uint8_t repeat_keys = 0;
    uint8_t extra_down = 0;
    uint8_t extra_up = 0;
    uint32_t now;
    int long_enter = 0;
    int badge_tick;
    ui_menu_t *menu;
    ui_render_stats_t render;

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
    /* 熄屏时任意键只负责唤醒：这一帧不执行按键动作，松开也不触发确定。 */
    if (st->screen_asleep && level != 0) {
        st->screen_asleep = 0;
        st->active_ms = now;
        st->prev_keys = level;
        st->key2_down = now;
        st->key2_long = 1;
        st->nav_key = 0;
        st->nav_repeat = 0;
        extra_down = extra_up = 0;   /* 蓝牙按键也不在唤醒帧触发动作 */
        hal_backlight_set(backlight_level());
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
    {
        uint8_t nav_held = (uint8_t)(level & (HAL_KEY1 | HAL_KEY3));
        uint8_t nav_pressed = (uint8_t)(pressed & (HAL_KEY1 | HAL_KEY3));
        if (nav_held == HAL_KEY1 || nav_held == HAL_KEY3) {
            if ((nav_pressed & nav_held) || st->nav_key != nav_held) {
                st->nav_key = nav_held;
                st->nav_repeat = 0;
                st->nav_down_ms = now;
                st->nav_step_ms = now;
            } else if (!st->nav_repeat) {
                if ((uint32_t)(now - st->nav_down_ms) >= (uint32_t)NAV_HOLD_DELAY_MS) {
                    st->nav_repeat = 1;
                    st->nav_step_ms = now;
                    repeat_keys = nav_held;
                }
            } else if ((uint32_t)(now - st->nav_step_ms) >= (uint32_t)NAV_REPEAT_MS) {
                st->nav_step_ms += (uint32_t)NAV_REPEAT_MS;
                if ((uint32_t)(now - st->nav_step_ms) >= (uint32_t)NAV_REPEAT_MS) {
                    st->nav_step_ms = now;
                }
                repeat_keys = nav_held;
            }
        } else {
            st->nav_key = 0;
            st->nav_repeat = 0;
        }
    }
    on_key(pressed, repeat_keys, long_enter);
    if (st->rip_mode == RIP_HOLD && (level & (HAL_KEY1 | HAL_KEY3)) == 0) {
        /* 短按松手或长按连跳松手：灰色圆形在最终停驻的选项上向外扩散。 */
        st->rip_mode = RIP_EXPAND;
        st->rip_t0 = now;
        st->rip_from_dot = 1;
    }
    {
        static int raise_prev;
        int rw = sport_raise_wrist();
        if (settings_get()->raise_wrist && rw && !raise_prev) {
            st->screen_asleep = 0;
            st->active_ms = now;
            hal_backlight_set(255);
        }
        raise_prev = rw;
    }
    {
        uint8_t sel = settings_get()->screen_off;
        uint16_t sec = screen_off_sec[sel <= 4 ? sel : 0];
        if (!st->screen_asleep && sec > 0 &&
            (now - st->active_ms) >= (uint32_t)sec * 1000u) {
            st->screen_asleep = 1;
            hal_backlight_set(0);
        }
    }
    /* on_key() 内 EEPROM/外部读写可能耗时数毫秒，刷新当前毫秒后再步进动画。 */
    now = hal_tick_ms();
    ui_anim_tick(now);
    if (!ui_anim_busy(&st->scroll)) {
        st->scroll.x = (int16_t)wrap_mod(st->scroll.x, HOME_N * 256);
    }
    if (!ui_anim_busy(&st->dial_slide)) {
        st->dial_slide.x = (int16_t)wrap_mod(st->dial_slide.x, UI_DIAL_COUNT * 256);
        st->dial_pick_steps = (int16_t)(st->dial_slide.x >> 8);
    }
    ui_menu_tick(&st->settings);
    ui_menu_tick(&st->messages);
    msg_link_sync();
    ui_menu_tick(&st->sport);
    ui_menu_tick(&st->music);
    {
        imu9_state_t s9;
        imu9_get(&s9);
        if (s9.mag_cal == 2u && st->prev_mag_cal != 2u) {
            settings_t *cfg = settings_get();
            st->prev_mag_cal = 2u;
            vib_play(HAL_VIB_SINGLE, 130u);
            (void)settings_save_ext(cfg->cfg_source == UI_CFG_PRIO_APP
                                        ? UI_CFG_PRIO_APP
                                        : UI_CFG_PRIO_I2C);
            link_notify_config();
        } else if (s9.mag_cal != 2u) {
            st->prev_mag_cal = s9.mag_cal;
        }
    }

    menu = active_menu();
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
    draw_battery_badge();
    badge_tick = draw_badge();
    ui_draw_flush();
    /* ui_draw_flush 内部的 wait_display 已经保证传输结束。 */
    ui_render_get_stats(&render);
    fps_update(render.pixels, badge_tick);
#ifndef AT32_SLIM_ROM
    /* 实机改在 hal_frame_pace 的帧间空闲时隙载入，避免 I2C 读拉长动画帧；
       仿真/回归端没有空闲钩子，仍在帧尾推进。 */
    link_asset_pump(256u);
#endif
    link_ack_flush();
    hal_meter_end();
    hal_frame_pace();
}

uint8_t ui_app_get_fps(void) {
    return st ? st->fps : 0;
}

static void reset_menu_home(ui_menu_t *menu, int count) {
    if (!menu) {
        return;
    }
    ui_anim_stop(&menu->scroll, 0);
    ui_anim_stop(&menu->child_scroll, 0);
    ui_anim_stop(&menu->zoom, 0);
    ui_anim_stop(&menu->push, 0);
    ui_menu_init(menu, count);
    menu->scroll.x = 0;
    menu->child_scroll.x = 0;
    menu->zoom.scale = 0;
    menu->zoom.opacity = 0;
    menu->push.x = 0;
    menu->push.scale = 0;
}

void ui_app_go_home(void) {
    if (!st) {
        return;
    }
    if (st->messages.level != UI_MENU_ICON) {
        link_leave_messages();
    }
    if (st->music.level != UI_MENU_ICON) {
        link_leave_music();
    }
    link_leave_compass();
    link_leave_about();
    link_leave_pic_sport();
    link_leave_pic_analog();
    link_hdr_active(0);
    reset_menu_home(&st->settings, SET_N);
    reset_menu_home(&st->messages, MSG_N);
    reset_menu_home(&st->sport, 1);
    reset_menu_home(&st->music, 1);
    st->msg_was_open = 0;
    st->msg_prev_level = UI_MENU_ICON;
    st->music_was_open = 0;
    st->compass_was_open = 0;
    st->about_was_open = 0;
    st->sport_open_was = 0;
    st->sport_pic_was = 0;
    st->analog_open_was = 0;
    st->sensor_open = 0;
    dial_edit_reset();
    st->sport_tab = 0;
    ui_anim_stop(&st->sport_scroll, 0);
    st->sport_scroll.x = 0;
    st->nav_key = 0;
    st->nav_repeat = 0;
    st->rip_mode = RIP_NONE;
    ui_nav_goto(UI_PAGE_FACE);
    ui_draw_invalidate();
}
