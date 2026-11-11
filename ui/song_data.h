#ifndef SONG_DATA_H
#define SONG_DATA_H

#include <stdint.h>

/* face 0 是大标题，face 1 是表盘数字。都从华文琥珀光栅。 */

typedef struct song_glyph {
    uint32_t cp;
    uint8_t face;
    uint8_t w;
    uint8_t h;
    uint16_t advance;
    int16_t x;
    int16_t y;
    uint16_t off;
} song_glyph_t;

extern const uint8_t song_cover[];
extern const song_glyph_t song_glyph[];
extern const int song_glyph_n;
extern const int song_line_h[2];

#endif
