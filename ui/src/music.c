#include "music.h"
#include "ui.h"

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3));
}

/* 左/右双三角（上一首/下一首）图标，dir=-1 左 / +1 右。 */
static void skip_icon(int cx, int cy, int s, int dir, uint16_t col, uint8_t a) {
    int x0 = cx - s, x1 = cx + s;
    int tip = dir < 0 ? x0 : x1;
    int base = dir < 0 ? x1 : x0;
    ui_draw_line((int16_t)base, (int16_t)(cy - s), (int16_t)tip, (int16_t)cy, col, a);
    ui_draw_line((int16_t)tip, (int16_t)cy, (int16_t)base, (int16_t)(cy + s), col, a);
    ui_draw_line((int16_t)base, (int16_t)(cy + s), (int16_t)base, (int16_t)(cy - s), col, a);
}

void music_page_draw(uint8_t a) {
    uint16_t bg = rgb565(6, 12, 28);
    uint16_t text = rgb565(236, 244, 255);
    uint16_t dim = rgb565(150, 190, 230);
    uint16_t blue = rgb565(0, 114, 230);
    uint16_t ring = rgb565(150, 210, 255);
    const char *lyric = link_media_lyric();
    int playing = link_media_playing();
    int16_t tw;
    int cx = UI_W / 2;
    int by = UI_H - UI_MY(200);
    int r = UI_MX(120);
    const char *state = playing ? "播放中" : "已暂停";

    ui_draw_fill(0, 0, UI_W, UI_H, bg, a);

    tw = ui_song_width(UI_SONG_TITLE, "音乐");
    ui_song_draw(UI_SONG_TITLE, (int16_t)((UI_W - tw) / 2), (int16_t)UI_MY(40), "音乐", text, a);

    if (!lyric[0]) lyric = "暂无歌词";
    tw = ui_text_width(lyric, 1);
    ui_text_draw((int16_t)((UI_W - tw) / 2), (int16_t)UI_MY(400), lyric, ring, a, 1);

    tw = ui_text_width(state, 1);
    ui_text_draw((int16_t)((UI_W - tw) / 2), (int16_t)(by - r - UI_MY(110)), state, dim, a, 1);

    /* 上一首 / 播放暂停 / 下一首 */
    skip_icon(cx - UI_MX(300), by, UI_MX(60), -1, ring, a);
    skip_icon(cx + UI_MX(300), by, UI_MX(60), +1, ring, a);
    ui_draw_round((int16_t)(cx - r), (int16_t)(by - r), (int16_t)(r * 2), (int16_t)(r * 2), r, blue, a);
    if (playing) {
        int bw = r / 3, bh = r, gap = r / 2;
        ui_draw_round((int16_t)(cx - gap / 2 - bw), (int16_t)(by - bh / 2), (int16_t)bw, (int16_t)bh, 2, text, a);
        ui_draw_round((int16_t)(cx + gap / 2), (int16_t)(by - bh / 2), (int16_t)bw, (int16_t)bh, 2, text, a);
    } else {
        int x0 = cx - r / 3, x1 = cx + r / 2, y0 = by - r / 2, y1 = by + r / 2;
        ui_draw_line((int16_t)x0, (int16_t)y0, (int16_t)x0, (int16_t)y1, text, a);
        ui_draw_line((int16_t)x0, (int16_t)y1, (int16_t)x1, (int16_t)by, text, a);
        ui_draw_line((int16_t)x1, (int16_t)by, (int16_t)x0, (int16_t)y0, text, a);
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