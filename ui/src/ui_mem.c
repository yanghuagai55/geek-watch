#include "ui_internal.h"

#include <string.h>

/* 动态块分配器：基于静态池 storage（零带内头部开销），外置有序区间表管理活跃块。
   释放块后相邻空闲区自动合并，既支持 UI_RAM_BUDGET 100% 满额分配，也支持页面/图片按需申请与归还。 */

enum {
    MEM_BLK_MAX = 32
};

typedef struct {
    uint16_t off;
    uint16_t len;
} mem_blk_t;

static union {
    uint64_t alignment;
    uint8_t bytes[UI_RAM_BUDGET];
} storage;
#define arena storage.bytes

static mem_blk_t blks[MEM_BLK_MAX];
static uint8_t blk_n;

void ui_mem_init(void) {
    blk_n = 0;
    memset(blks, 0, sizeof blks);
    memset(arena, 0, sizeof arena);
}

void *ui_mem_alloc(uint32_t size, uint32_t align) {
    uint32_t mask;
    uint8_t i, j;

    if (size == 0 || size > UI_RAM_BUDGET || blk_n >= MEM_BLK_MAX) {
        return 0;
    }
    if (align == 0 || (align & (align - 1u)) != 0) {
        align = 1;
    }
    mask = align - 1u;

    for (i = 0; i <= blk_n; i++) {
        uint32_t gap_start = (i == 0) ? 0u : (uint32_t)(blks[i - 1].off + blks[i - 1].len);
        uint32_t gap_end = (i < blk_n) ? (uint32_t)blks[i].off : (uint32_t)UI_RAM_BUDGET;
        uintptr_t base_addr = (uintptr_t)arena + gap_start;
        uintptr_t aligned_addr = (base_addr + (uintptr_t)mask) & ~(uintptr_t)mask;
        uint32_t aligned_off = gap_start + (uint32_t)(aligned_addr - base_addr);

        if (aligned_off <= gap_end && gap_end - aligned_off >= size) {
            for (j = blk_n; j > i; j--) {
                blks[j] = blks[j - 1];
            }
            blks[i].off = (uint16_t)aligned_off;
            blks[i].len = (uint16_t)size;
            blk_n++;
            memset(arena + aligned_off, 0, size);
            return arena + aligned_off;
        }
    }
    return 0;
}

void ui_mem_free(void *ptr) {
    uintptr_t p;
    uintptr_t base;
    uint32_t off;
    uint8_t i, j;

    if (!ptr) {
        return;
    }
    p = (uintptr_t)ptr;
    base = (uintptr_t)arena;
    if (p < base || p >= base + UI_RAM_BUDGET) {
        return;
    }
    off = (uint32_t)(p - base);
    for (i = 0; i < blk_n; i++) {
        if (blks[i].off == (uint16_t)off) {
            for (j = i; j + 1 < blk_n; j++) {
                blks[j] = blks[j + 1];
            }
            blk_n--;
            return;
        }
    }
}

uint32_t ui_mem_avail(void) {
    uint32_t best = 0;
    uint8_t i;

    for (i = 0; i <= blk_n; i++) {
        uint32_t gap_start = (i == 0) ? 0u : (uint32_t)(blks[i - 1].off + blks[i - 1].len);
        uint32_t gap_end = (i < blk_n) ? (uint32_t)blks[i].off : (uint32_t)UI_RAM_BUDGET;
        if (gap_end > gap_start && gap_end - gap_start > best) {
            best = gap_end - gap_start;
        }
    }
    return best;
}

void ui_core_init(void) {
    ui_mem_init();
    ui_anim_init();
    ui_draw_init();
}

void ui_mem_get(uint32_t *used_bytes, uint32_t *total_bytes) {
    if (used_bytes) {
        uint32_t sum = 0;
        uint32_t prev_end = 0;
        uint8_t i;
        for (i = 0; i < blk_n; i++) {
            uint32_t gap = (uint32_t)blks[i].off - prev_end;
            if (gap < 8u) {
                sum += gap;
            }
            sum += blks[i].len;
            prev_end = (uint32_t)(blks[i].off + blks[i].len);
        }
        *used_bytes = sum;
    }
    if (total_bytes) {
        *total_bytes = UI_RAM_BUDGET;
    }
}
