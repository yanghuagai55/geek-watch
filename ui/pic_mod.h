#ifndef PIC_MOD_H
#define PIC_MOD_H

#include "hal.h"
#include "imu9.h"
#include "sport.h"
#include <stdint.h>

enum {
    PIC_MAGIC_SPORT  = 0x31525053u, /* 'SPR1' */
    PIC_MAGIC_ANALOG = 0x314C4E41u, /* 'ANL1' */
    PIC_MAGIC_SYS    = 0x31535953u, /* 'SYS1' */
    PIC_MAGIC_MUSIC  = 0x3153554Du, /* 'MUS1' */
    PIC_MOD_ABI_VER  = 1u
};

/* Generic AT24C256 PIC applet header (12B, binary compatible with compass3d_hdr_t). */
typedef struct pic_mod_hdr {
    uint32_t magic;
    uint16_t ver_flags;
    uint16_t entry_off;
    uint16_t total_size;
    uint16_t crc16;
} pic_mod_hdr_t;

/* Host ABI exposed cleanly to cold SRAM PIC applets (Sport+Sensor / Analog Dial). */
typedef struct pic_mod_api {
    uint16_t ui_w;
    uint16_t ui_h;
    uint16_t col_bg;
    uint16_t col_line;
    uint16_t col_accent;
    uint16_t col_ring;
    uint16_t col_text;
    uint16_t col_dim;
    uint16_t col_surface;
    uint16_t dial_fg;
    uint16_t dial_ac;
    uint16_t dial_bg;
    uint32_t now_ms;
    uint32_t reveal_ms;
    uint8_t dial_top;
    uint8_t dial_bot;
    uint8_t dial_flags;
    uint8_t use_24h;
    uint8_t bat_pct;
    uint16_t bat_mv;
    const char *lyric;
    uint8_t media_playing;
    const char *comp_top;
    const char *comp_bot;
    const char *i2c_scan;
    void (*fill)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col, uint8_t op);
    void (*round)(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t col, uint8_t op);
    void (*line)(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t col, uint8_t op);
    void (*text)(int16_t x, int16_t y, const char *s, uint16_t col, uint8_t op, uint8_t sc);
    void (*text_center)(int16_t cx, int16_t y, const char *s, uint16_t col, uint8_t op, uint8_t sc);
    void (*text_right)(int16_t right, int16_t y, const char *s, uint16_t col, uint8_t op, uint8_t sc);
    void (*text_fit)(char *out, int cap, const char *s, int16_t max_w, uint8_t sc);
    int16_t (*text_w)(const char *s, uint8_t sc);
    int16_t (*text_h)(const char *s, uint8_t sc);
    void (*song_draw)(int face, int16_t x, int16_t y, const char *utf8, uint16_t col, uint8_t op);
    int16_t (*song_w)(int face, const char *utf8);
    int16_t (*song_h)(int face);
    uint8_t (*ease_in_out)(uint32_t t);
    void (*clock_get)(hal_clock_t *out);
    void (*sport_get)(sport_stats_t *out);
    void (*sport_bars)(uint32_t *out, int n);
    void (*imu_get)(imu9_state_t *out);
    int (*baro_read)(hal_baro_t *out);
    const char *(*mag_name)(void);
    uint8_t (*get_fps)(void);
    uint16_t (*get_cpu_tenths)(void);
    uint8_t page_id;
    uint16_t col_blue_dim;
    void (*line_w)(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t w, uint16_t col, uint8_t op);
} pic_mod_api_t;

typedef void (*pic_mod_entry_t)(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg);

void sport_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg);
void analog_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg);
void sys_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg);
void music_mod_render_c(const pic_mod_api_t *api, uint8_t mode, uint8_t alpha, int clear_bg);

#endif
