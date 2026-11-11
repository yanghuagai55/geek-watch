#include "ui_internal.h"

#include <string.h>

/* 一整块静态内存，从头往后切。不调用 malloc。
   used 是切到哪里，对齐浪费的字节也算进去。 */

static union {
    uint64_t alignment;
    uint8_t bytes[UI_RAM_BUDGET];
} storage;
#define arena storage.bytes
static uint32_t used;

void ui_mem_init(void) {
    used = 0;
    memset(arena, 0, sizeof arena);
}

void *ui_mem_alloc(uint32_t size, uint32_t align) {
    uint32_t mask;
    uint32_t off;

    if (align == 0 || (align & (align - 1u)) != 0) {
        align = 1;
    }
    mask = align - 1u;
    off = (used + mask) & ~mask;
    if (size > UI_RAM_BUDGET || off > UI_RAM_BUDGET - size) {
        return 0;
    }
    used = off + size;
    memset(arena + off, 0, size);
    return arena + off;
}

void ui_core_init(void) {
    ui_mem_init();
    ui_anim_init();
    ui_draw_init();
}

void ui_mem_get(uint32_t *used_bytes, uint32_t *total_bytes) {
    if (used_bytes) {
        *used_bytes = used;
    }
    if (total_bytes) {
        *total_bytes = UI_RAM_BUDGET;
    }
}
