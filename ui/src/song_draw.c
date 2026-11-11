#include "ui_internal.h"
#include "song_data.h"

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
    *ps += 1;
    return 0;
}

static const song_glyph_t *find_song(int face, uint32_t cp) {
    int i;

    for (i = 0; i < song_glyph_n; i++) {
        if (song_glyph[i].face == (uint8_t)face && song_glyph[i].cp == cp) {
            return &song_glyph[i];
        }
    }
    return 0;
}

int16_t ui_song_width(int face, const char *utf8) {
    int width = 0;
    const char *s = utf8;

    if (!s || face < 0 || face > 1) {
        return 0;
    }
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const song_glyph_t *g = find_song(face, cp);
        if (g) {
            width += g->advance;
        }
    }
    return (int16_t)width;
}

int16_t ui_song_height(int face) {
    if (face < 0 || face > 1) {
        return 0;
    }
    return (int16_t)song_line_h[face];
}

void ui_song_draw(int face, int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity) {
    int pen = x;
    const char *s = utf8;

    if (!s || face < 0 || face > 1 || opacity == 0) {
        return;
    }
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const song_glyph_t *g = find_song(face, cp);
        if (!g || g->w == 0 || g->h == 0) {
            continue;
        }
        ui_mask_draw((int16_t)(pen + g->x), (int16_t)(y + g->y),
                     (int16_t)g->w, (int16_t)g->h,
                     song_cover + g->off, color, opacity);
        pen += g->advance;
    }
}
