/* 音乐页冷模块（片外 AT24C256，MUS1）。
   只依赖 pic_mod_api_t：底色、歌词/均衡器、上一首/播放/下一首。
   歌词点阵（128x32）由固件贴图，mode 1 表示已贴，模块跳过文字歌词。 */

#include "pic_mod.h"

#if defined(__ARM_ARCH) || defined(__thumb__)
#define PIC_ENTRY_ATTR __attribute__((section(".text.entry"), used))
void music_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) PIC_ENTRY_ATTR;
#endif

/* 与 ui_anim_pulse 同一曲线：三角相位 + cubic ease-in-out。API 只暴露
   ease_in_out，这里自己折相位，保证和片内动画观感一致。 */
static uint8_t m_pulse(const pic_mod_api_t *api, uint32_t now_ms, uint32_t period_ms) {
    uint32_t phase;

    if (period_ms < 2u) {
        return 0u;
    }
    phase = now_ms % period_ms;
    if (phase > period_ms / 2u) {
        phase = period_ms - phase;
    }
    return api->ease_in_out(phase * 256u / (period_ms / 2u));
}

/* 两笔线画的上/下一首箭头，dir=-1 上一首 / +1 下一首。 */
static void m_skip(const pic_mod_api_t *api, int cx, int cy, int s, int dir,
                   uint16_t col, uint8_t a) {
    int x0 = cx - s;
    int x1 = cx + s;
    int tip = dir < 0 ? x0 : x1;
    int base = dir < 0 ? x1 : x0;

    api->line((int16_t)base, (int16_t)(cy - s), (int16_t)tip, (int16_t)cy, col, a);
    api->line((int16_t)tip, (int16_t)cy, (int16_t)base, (int16_t)(cy + s), col, a);
    api->line((int16_t)base, (int16_t)(cy + s), (int16_t)base, (int16_t)(cy - s), col, a);
}

/* 五根短柱共用整数脉冲呼吸；暂停时收成一条静线，状态本身即动画。 */
static void m_equalizer(const pic_mod_api_t *api, int y, int h, int playing, uint8_t a) {
    enum { BARS = 5 };
    int w = (int)api->ui_w;
    int pitch = w * 130 / 1000;
    int bw = w * 50 / 1000;
    int x0, i;

    if (pitch < bw + 2) pitch = bw + 2;
    x0 = (w - (BARS - 1) * pitch - bw) / 2;
    for (i = 0; i < BARS; i++) {
        int pulse = playing ? (int)m_pulse(api, api->now_ms + (uint32_t)i * 173u,
                                           720u + (uint32_t)i * 80u) : 0;
        int bh = 3 + pulse * h / 255;

        api->round((int16_t)(x0 + i * pitch), (int16_t)(y + h - bh),
                   (int16_t)bw, (int16_t)bh, 2,
                   i == BARS / 2 ? api->col_accent : api->col_ring,
                   (uint8_t)((int)a * 220 / 255));
    }
}

static void music_mod_impl(const pic_mod_api_t *api, uint8_t mode, uint8_t a, int clear_bg) {
    char fitted[64];
    int w, h, cx, by, r, margin, playing;

    if (!api || a == 0u) {
        return;
    }
    w = (int)api->ui_w;
    h = (int)api->ui_h;
    cx = w / 2;
    margin = w * 70 / 1000;
    by = h - h * 190 / 1000;
    r = w * 110 / 1000;
    if (r < 12) r = 12;
    playing = api->media_playing ? 1 : 0;

    if (clear_bg && a == 255u) {
        api->fill(0, 0, (int16_t)w, (int16_t)h, api->col_bg, a);
    }
    /* 静态底图：用矢量圆盘画黑胶纹样（和运动页同一套 PIC 绘制方式），
       只占下半屏，不挡歌词点阵和均衡器。 */
    api->round((int16_t)(cx - w * 460 / 1000), (int16_t)(by - w * 460 / 1000),
               (int16_t)(w * 920 / 1000), (int16_t)(w * 920 / 1000),
               (int16_t)(w * 460 / 1000), api->col_surface, a);
    api->round((int16_t)(cx - w * 390 / 1000), (int16_t)(by - w * 390 / 1000),
               (int16_t)(w * 780 / 1000), (int16_t)(w * 780 / 1000),
               (int16_t)(w * 390 / 1000), api->col_line, a);
    api->round((int16_t)(cx - w * 382 / 1000), (int16_t)(by - w * 382 / 1000),
               (int16_t)(w * 764 / 1000), (int16_t)(w * 764 / 1000),
               (int16_t)(w * 382 / 1000), api->col_surface, a);
    api->round((int16_t)(cx - w * 310 / 1000), (int16_t)(by - w * 310 / 1000),
               (int16_t)(w * 620 / 1000), (int16_t)(w * 620 / 1000),
               (int16_t)(w * 310 / 1000), api->col_line, a);
    api->round((int16_t)(cx - w * 302 / 1000), (int16_t)(by - w * 302 / 1000),
               (int16_t)(w * 604 / 1000), (int16_t)(w * 604 / 1000),
               (int16_t)(w * 302 / 1000), api->col_surface, a);

    if (mode & 2u) {
        static const char off[] = "\xe6\x97\xa0\xe8\xbf\x9e\xe6\x8e\xa5"; /* 无连接 */
        int16_t tw = api->song_w(0, off);
        int16_t th = api->song_h(0);

        api->song_draw(0, (int16_t)((w - tw) / 2),
                       (int16_t)((h - th) / 2 - h * 25 / 1000),
                       off, api->col_dim, a);
        return;
    }

    if ((mode & 1u) == 0u) {
        const char *lyric = api->lyric;
        if (!lyric || !lyric[0]) {
            lyric = "\xe6\x9a\x82\xe6\x97\xa0\xe6\xad\x8c\xe8\xaf\x8d"; /* 暂无歌词 */
        }
        api->text_fit(fitted, (int)sizeof fitted, lyric,
                      (int16_t)(w - margin * 2), 1u);
        api->text_center((int16_t)cx, (int16_t)(h * 170 / 1000), fitted,
                         api->col_text, a, 1u);
    }

    m_equalizer(api, h * 560 / 1000, h * 120 / 1000, playing, a);
    m_skip(api, cx - w * 310 / 1000, by, w * 55 / 1000, -1, api->col_ring, a);
    m_skip(api, cx + w * 310 / 1000, by, w * 55 / 1000, +1, api->col_ring, a);
    api->round((int16_t)(cx - r), (int16_t)(by - r),
               (int16_t)(r * 2), (int16_t)(r * 2), (int16_t)r, api->col_ring, a);
    if (playing) {
        int bw = r / 4;
        int bh = r;
        int gap = r / 2;

        api->round((int16_t)(cx - gap / 2 - bw), (int16_t)(by - bh / 2),
                   (int16_t)bw, (int16_t)bh, 2, api->col_bg, a);
        api->round((int16_t)(cx + gap / 2), (int16_t)(by - bh / 2),
                   (int16_t)bw, (int16_t)bh, 2, api->col_bg, a);
    } else {
        int x0 = cx - r / 3;
        int x1 = cx + r / 2;
        int y0 = by - r / 2;
        int y1 = by + r / 2;

        api->line((int16_t)x0, (int16_t)y0, (int16_t)x0, (int16_t)y1, api->col_bg, a);
        api->line((int16_t)x0, (int16_t)y1, (int16_t)x1, (int16_t)by, api->col_bg, a);
        api->line((int16_t)x1, (int16_t)by, (int16_t)x0, (int16_t)y0, api->col_bg, a);
    }
}

#if defined(__ARM_ARCH) || defined(__thumb__)
void music_mod_entry(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    music_mod_impl(api, mode, alpha, clear_bg);
}
#else
void music_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg) {
    music_mod_impl(api, mode, alpha, clear_bg);
}
#endif
