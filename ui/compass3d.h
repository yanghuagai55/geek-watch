#ifndef COMPASS3D_H
#define COMPASS3D_H

#include "imu9.h"
#include <stdint.h>

enum {
    COMPASS3D_MAGIC = 0x31443343u, /* 'C3D1' little-endian */
    COMPASS3D_ABI_VER = 1u
};

/* 片外 3D 指南针 ARM Cortex-M4 Thumb-2 位置无关二进制头 (12 字节)。 */
typedef struct compass3d_hdr {
    uint32_t magic;      /* COMPASS3D_MAGIC */
    uint16_t ver_flags;  /* (ABI_VER << 8) | 1 (bit0: Thumb 入口) */
    uint16_t entry_off;  /* 相对本头部起始的入口字节偏移 */
    uint16_t total_size; /* 头部 + 机器码/只读常量总字节数 */
    uint16_t crc16;      /* CRC16-CCITT (poly 0x1021, init 0xFFFF) 覆盖 [entry_off..total_size) */
} compass3d_hdr_t;

/* 宿主机固件暴露给 SRAM 动态加载 3D 指南针模块的最小渲染与传感器接口。 */
typedef struct compass3d_api {
    uint16_t ui_w;
    uint16_t ui_h;
    uint16_t col_bg;
    uint16_t col_line;
    uint16_t col_accent;
    uint16_t col_ring;
    uint16_t col_text;
    uint16_t col_dim;
    uint32_t now_ms;
    void (*fill)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col, uint8_t op);
    void (*round)(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t col, uint8_t op);
    void (*line)(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t col, uint8_t op);
    void (*text)(int16_t x, int16_t y, const char *s, uint16_t col, uint8_t op, uint8_t sc);
    void (*text_center)(int16_t cx, int16_t y, const char *s, uint16_t col, uint8_t op, uint8_t sc);
    int16_t (*text_h)(const char *s, uint8_t sc);
    void (*imu_get)(imu9_state_t *out);
    void (*imu_rot)(int16_t out9_q14[9]);
} compass3d_api_t;

typedef void (*compass3d_entry_t)(const compass3d_api_t *api, uint8_t alpha, int clear_bg);

/* Host/跨平台模拟测试同源入口（AT32 实际直接跳转 SRAM 中解包后的 ARM Thumb-2 机器码）。 */
void compass3d_render_c(const compass3d_api_t *api, uint8_t alpha, int clear_bg);

#endif
