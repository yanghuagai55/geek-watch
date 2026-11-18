#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "ui.h"

#ifdef UI_NO_TRACE
#define ui_trace_emit(message) ((void)0)
#else
void ui_trace_emit(const char *message);
#endif

void ui_mem_init(void);
void *ui_mem_alloc(uint32_t size, uint32_t align);
void ui_mem_free(void *ptr);
uint32_t ui_mem_avail(void);

void ui_anim_init(void);
int ui_anim_closing(const ui_obj_t *obj);

void ui_draw_init(void);
uint32_t ui_utf8_next(const char **ps);
void ui_draw_text(int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity, uint8_t scale);
void ui_mask_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint8_t *cover4, uint16_t color, uint8_t opacity);
void ui_mask_rle_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                      const uint8_t *rle4, uint16_t color, uint8_t opacity);
int link_snap_active(void);

#endif
