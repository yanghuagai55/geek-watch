#include "music.h"
#include "ui.h"

#include <stdio.h>

/* Two line-drawn skip marks: dir=-1 previous / +1 next. */
static void skip_icon(int cx, int cy, int s, int dir, uint16_t col, uint8_t a) {
    int x0 = cx - s;
    int x1 = cx + s;
    int tip = dir < 0 ? x0 : x1;
    int base = dir < 0 ? x1 : x0;

    ui_draw_line((int16_t)base, (int16_t)(cy - s), (int16_t)tip, (int16_t)cy, col, a);
    ui_draw_line((int16_t)tip, (int16_t)cy, (int16_t)base, (int16_t)(cy + s), col, a);
    ui_draw_line((int16_t)base, (int16_t)(cy + s), (int16_t)base, (int16_t)(cy - s), col, a);
}

/* Five small bars breathe on a shared integer pulse. Paused state settles
   into a quiet line, so the animation carries state instead of decoration. */
static void equalizer(int y, int h, int playing, uint8_t a) {
    enum { BARS = 5 };
    int pitch = UI_MX(130);
    int bw = UI_MX(50);
    int x0 = (UI_W - (BARS - 1) * pitch - bw) / 2;
    int i;

    if (pitch < bw + 2) pitch = bw + 2;
    x0 = (UI_W - (BARS - 1) * pitch - bw) / 2;
    for (i = 0; i < BARS; i++) {
        int pulse = playing ? ui_anim_pulse(hal_tick_ms() + (uint32_t)i * 173u,
                                            720u + (uint32_t)i * 80u) : 0;
        int bh = 3 + pulse * h / 255;
        int x = x0 + i * pitch;
        ui_draw_round((int16_t)x, (int16_t)(y + h - bh), (int16_t)bw, (int16_t)bh,
                      2, i == BARS / 2 ? UI_COL_AMBER : UI_COL_MINT,
                      (uint8_t)((int)a * 220 / 255));
    }
}

void music_page_draw(uint8_t a) {
    const char *lyric = link_media_lyric();
    char fitted[64];
    int margin = UI_MX(UI_LAYOUT_MARGIN_M);
    int header = ui_song_height(UI_SONG_TITLE) + UI_MY(20);
    int divider = header + UI_MY(20);
    int cx = UI_W / 2;
    int by = UI_H - UI_MY(190);
    int r = UI_MX(110);
    int playing = link_media_playing();
    int16_t tw;

    if (!lyric[0]) lyric = "暂无歌词";
    if (r < 12) r = 12;

    ui_draw_fill(0, 0, UI_W, UI_H, UI_COL_BG, a);
    tw = ui_song_width(UI_SONG_TITLE, "音乐");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2),
                 (int16_t)UI_MY(25), "音乐", UI_COL_TEXT, a);
    ui_draw_line((int16_t)margin, (int16_t)divider,
                 (int16_t)(UI_W - margin), (int16_t)divider, UI_COL_LINE, a);

    ui_text_fit(fitted, sizeof fitted, lyric,
                (int16_t)(UI_W - margin * 2), 1);
    ui_text_draw_center((int16_t)cx, (int16_t)(divider + UI_MY(90)), fitted,
                        UI_COL_TEXT, a, 1);
    equalizer(UI_MY(560), UI_MY(120), playing, a);

    skip_icon(cx - UI_MX(310), by, UI_MX(55), -1, UI_COL_MINT, a);
    skip_icon(cx + UI_MX(310), by, UI_MX(55), +1, UI_COL_MINT, a);
    ui_draw_round((int16_t)(cx - r), (int16_t)(by - r),
                  (int16_t)(r * 2), (int16_t)(r * 2), (int16_t)r, UI_COL_MINT, a);
    if (playing) {
        int bw = r / 4;
        int bh = r;
        int gap = r / 2;
        ui_draw_round((int16_t)(cx - gap / 2 - bw), (int16_t)(by - bh / 2),
                      (int16_t)bw, (int16_t)bh, 2, UI_COL_BG, a);
        ui_draw_round((int16_t)(cx + gap / 2), (int16_t)(by - bh / 2),
                      (int16_t)bw, (int16_t)bh, 2, UI_COL_BG, a);
    } else {
        int x0 = cx - r / 3;
        int x1 = cx + r / 2;
        int y0 = by - r / 2;
        int y1 = by + r / 2;
        ui_draw_line((int16_t)x0, (int16_t)y0, (int16_t)x0, (int16_t)y1, UI_COL_BG, a);
        ui_draw_line((int16_t)x0, (int16_t)y1, (int16_t)x1, (int16_t)by, UI_COL_BG, a);
        ui_draw_line((int16_t)x1, (int16_t)by, (int16_t)x0, (int16_t)y0, UI_COL_BG, a);
    }
}

int music_page_key(uint8_t pressed) {
    if (pressed & HAL_KEY1) {
        link_media_prev();
        return 1;
    }
    if (pressed & HAL_KEY2) {
        link_media_toggle();
        return 1;
    }
    if (pressed & HAL_KEY3) {
        link_media_next();
        return 1;
    }
    return 0;
}
