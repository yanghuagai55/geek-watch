#ifndef CLOCK_FONT_H
#define CLOCK_FONT_H

#include <stdint.h>

/* 表盘用的 Inter Medium 数字。灰度在 Flash 里，按 1:1 画，不再放大点阵。 */

typedef struct clock_glyph {
    uint8_t w;
    uint8_t h;
    int16_t x;
    int16_t y;
    uint16_t off;
} clock_glyph_t;

extern const int clock_cell_w;
extern const int clock_row_h;
extern const uint8_t clock_cover[];
extern const clock_glyph_t clock_digit[10];

#endif
