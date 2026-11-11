#ifndef UI_FONT_H
#define UI_FONT_H

#include <stdint.h>

/* 点阵字模。数据由 host/gen_font.c 生成，放在 Flash 侧，不占 16KB 预算。 */

typedef struct ui_glyph {
    uint32_t cp;
    uint8_t w;
    uint8_t h;
    uint8_t advance;
    uint16_t offset;
} ui_glyph_t;

const ui_glyph_t *ui_font_find(uint32_t cp);
const uint8_t *ui_font_bits(void);

#endif
