#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "ui.h"

void ui_trace_emit(const char *message);

void ui_mem_init(void);
void *ui_mem_alloc(uint32_t size, uint32_t align);

void ui_anim_init(void);

void ui_draw_init(void);
void ui_draw_text(int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity, uint8_t scale);
void ui_mask_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint8_t *cover4, uint16_t color, uint8_t opacity);
int link_snap_active(void);

#endif
