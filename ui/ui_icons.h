#ifndef UI_ICONS_H
#define UI_ICONS_H

#include "ui.h"

/* Animated monochrome icons: single RGB565 color on bg, integer-only. */
void ui_icon_mail(int16_t x, int16_t y, int16_t w, int16_t h,
                  uint32_t now_ms, uint16_t color, uint16_t accent,
                  uint16_t bg, uint8_t opacity);
void ui_icon_gear(int16_t cx, int16_t cy, int16_t r,
                  uint32_t now_ms, uint16_t color, uint16_t bg, uint8_t opacity);

#endif /* UI_ICONS_H */