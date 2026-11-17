#ifndef WATCH_UI_H
#define WATCH_UI_H

#include "hal.h"
#include <stdint.h>

/* Portable UI core. Pure C99, no malloc, no chip headers, integer math only.
   Everything hardware-specific lives behind hal.h; ship ui/ as libui.a and
   write one hal implementation per board (see host/hal_host.c, stm32/port.c).

   Minimal integration (demo app):
       board_init();
       ui_init();
       ui_set_service(my_cooperative_hook);   // optional
       for (;;) ui_frame();

   Custom page / secondary development (no demo app):
       ui_core_init();
       for (;;) {
           ui_draw_begin();
           ui_draw_fill(0, 0, UI_W, UI_H, bg, 255);
           ui_draw_round(x, y, w, h, r, fg, 255);
           ui_svg_draw_w(x, y, scale, cmd, n, fg, 255, 3);
           ui_draw_flush();
           hal_frame_pace();
       }
   Animations: fill a ui_obj_t, ui_anim_play(id, &obj); ui_frame advances it. */

#define UI_W HAL_LCD_W
#define UI_H HAL_LCD_H
#define UI_BAND_H 4
#define UI_RAM_BUDGET (10560u + 4u * UI_W * UI_BAND_H)

/* ---- lifecycle & cooperative service ---- */
/* ui_core_init omits the demo app; follow with begin/draw/flush for custom pages. */
void ui_core_init(void);
void ui_init(void);
void ui_frame(void);
/* Short cooperative work only: do not reenter UI or take ownership of the display. */
void ui_set_service(void (*fn)(void));
void ui_service(void);

/* ---- render stats & damage control ---- */
typedef struct ui_render_stats {
    uint32_t prepare_us;
    uint32_t raster_us;
    uint32_t wait_us;
    uint32_t frame_us;
    uint32_t pixels;
    uint16_t bands;
    uint16_t items;
    uint16_t dropped;
} ui_render_stats_t;

void ui_render_get_stats(ui_render_stats_t *out);
void ui_draw_invalidate(void);

/* 贴近肉眼观感的屏幕刷新帧率（fps，约每 250ms 平滑更新；含局部脏区刷新与全屏传输，静止时回落至 0）。 */
uint8_t ui_app_get_fps(void);
/* 强制回到表盘主页（关闭子页/菜单） */
void ui_app_go_home(void);
/* 打开音乐页（PC 下发媒体点阵时使用） */
void ui_app_open_music(void);

/* ---- layout: permille of the panel, never hard-coded pixels ----
   屏幕长边竖着放。0 贴着左或上，1000 贴着右或下。 */
#define UI_MX(m) ((m) * UI_W / 1000)
#define UI_MY(m) ((m) * UI_H / 1000)
#define UI_LAYOUT_MARGIN_M 70

/* One quiet palette for every page: warm white, mint and amber on graphite.
   Compile-time RGB565 keeps the theme out of RAM and avoids per-page drift. */
#define UI_RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | \
                                      (((g) & 0xFC) << 3) | ((b) >> 3)))
#define UI_COL_BG       UI_RGB565(0, 0, 0)
#define UI_COL_SURFACE  UI_RGB565(24, 33, 32)
#define UI_COL_LINE     UI_RGB565(49, 65, 60)
#define UI_COL_TEXT     UI_RGB565(240, 241, 228)
#define UI_COL_MUTED    UI_RGB565(135, 151, 143)
#define UI_COL_MUTED_LIGHT UI_RGB565(176, 186, 180)
#define UI_COL_RIPPLE   UI_RGB565(172, 178, 176)
#define UI_COL_MINT     UI_RGB565(115, 212, 174)
#define UI_COL_AMBER    UI_RGB565(255, 177, 94)

/* ---- pages & navigation ---- */
typedef enum {
    UI_PAGE_FACE = 0,
    UI_PAGE_SPORT = 1,
    UI_PAGE_MSG = 2,
    UI_PAGE_SETTINGS = 3,
    UI_PAGE_MUSIC = 4
} ui_page_id_t;

void ui_nav_goto(ui_page_id_t page);

/* ---- animation ----
   动画只改这些属性。真正把属性画成像素是后面的事，调用处不用改。 */
typedef struct ui_obj {
    int16_t x;
    int16_t y;
    uint8_t opacity;
    uint8_t scale; /* 0 收起，255 铺满 */
} ui_obj_t;

typedef enum {
    UI_ANIM_SLIDE_LEFT = 0,
    UI_ANIM_SLIDE_RIGHT = 1,
    UI_ANIM_FADE_IN = 2,
    UI_ANIM_FADE_OUT = 3,
    UI_ANIM_ZOOM_IN = 4,
    UI_ANIM_ZOOM_OUT = 5,
    UI_ANIM_STACK = 6
} ui_anim_id_t;

/* 按预设把 obj 从起点变到终点。ui_frame 里会推进时间。
   ui_anim_play_range 仍用预设的时长和属性，起止值由调用者给出。 */
void ui_anim_play(ui_anim_id_t id, ui_obj_t *obj);
void ui_anim_play_range(ui_anim_id_t id, ui_obj_t *obj, int16_t from, int16_t to);
int ui_anim_busy(const ui_obj_t *obj);
void ui_anim_stop(ui_obj_t *obj, int finish);
void ui_anim_tick(uint32_t now_ms);
uint16_t ui_anim_duration_ms(ui_anim_id_t id);
/* Reusable easing, t 0..256 -> travelled 0..255. Cubic ease-in-out (LVGL, MIT). */
uint8_t ui_ease_in_out(uint32_t t);
/* Allocation-free 0..255..0 pulse for small ambient motions. period_ms must > 0. */
uint8_t ui_anim_pulse(uint32_t now_ms, uint32_t period_ms);

/* ---- drawing primitives (record into the current frame) ---- */
void ui_draw_begin(void);
void ui_draw_xform(int scale256, int ox, int oy);
void ui_draw_get_xform(int *scale256, int *ox, int *oy);
void ui_draw_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, uint8_t opacity);
void ui_draw_round(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint16_t color, uint8_t opacity);
void ui_draw_ripple(int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t radius, int16_t cy, int16_t rip_r,
                    uint16_t color, uint8_t opacity);
void ui_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, uint8_t opacity);
void ui_draw_line_w(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t width, uint16_t color, uint8_t opacity);
void ui_draw_flush(void);

/* ---- vector / bitmap / text ----
   矢量。r 为 0 时是从 (x0,y0) 到 (x1,y1) 的线段，r > 0 时是圆心 (x0,y0)、半径 r 的圆。
   坐标是图自己的，调用时再平移、再按整数放大。 */
typedef struct ui_svg_cmd {
    int8_t x0;
    int8_t y0;
    int8_t x1;
    int8_t y1;
    int8_t r;
    uint8_t ci; /* palette index: 0 = base color, 1..n = pal[ci-1] */
} ui_svg_cmd_t;

/* 画进当前这一页。页面在放大、左右滑或被上一页缩小盖住时，图会跟着走。
   命令和像素要保持到这一帧刷完，放在静态存储里。 */
void ui_svg_draw(int16_t x, int16_t y, uint8_t scale,
                 const ui_svg_cmd_t *cmd, int count,
                 uint16_t color, uint8_t opacity);
/* Same, with a stroke thickness in pixels (0/2 = hairline). Discs keep radius r. */
void ui_svg_draw_w(int16_t x, int16_t y, uint8_t scale,
                   const ui_svg_cmd_t *cmd, int count,
                   uint16_t color, uint8_t opacity, uint8_t width);
/* Multi-color: cmd[i].ci selects pal[ci-1]; ci 0 uses `color`. pal must stay
   valid until the frame flushes (static storage). */
void ui_svg_draw_p(int16_t x, int16_t y, uint8_t scale,
                   const ui_svg_cmd_t *cmd, int count,
                   const uint16_t *pal, uint8_t npal,
                   uint16_t color, uint8_t opacity, uint8_t width);
void ui_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint8_t scale, const uint16_t *rgb565, uint8_t opacity);
/* 4bpp 覆盖位图：每字节两个像素，高半字节在前，alpha 0..15。 */
void ui_mask_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint8_t *cover4, uint16_t color, uint8_t opacity);
/* 1bpp 位图：每行 (w+7)/8 字节，MSB 在左，用于手机下发的消息列表/详情。 */
void ui_mask1_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                   const uint8_t *bits, uint16_t color, uint8_t opacity);
void ui_mask1_draw_clip(int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t src_w, const uint8_t *bits,
                        uint16_t color, uint8_t opacity);
/* 外部 32KB Flash 流式贴图：按 4 行分带直接从 hal_ext_flash_read 逐行流式读取渲染，无需全屏 RAM 缓冲。 */
enum {
    UI_EXT_FMT_1BPP = 1,
    UI_EXT_FMT_4BPP = 2,
    UI_EXT_FMT_RGB565 = 3,
    UI_EXT_FMT_PAL4_RLE = 4,   /* 16色自适应调色板(32B) + 行结束偏移(2*H B) + Nibble RLE */
    UI_EXT_FMT_RGB565_RLE = 5  /* 真彩 RGB565 + 行结束偏移(2*H B) + PackBits RLE */
};
void ui_ext_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint32_t ext_offset, uint8_t fmt,
                       uint16_t color, uint8_t opacity);
void ui_ext_image_draw_fast(int16_t x, int16_t y, int16_t w, int16_t h,
                            const uint8_t *mem_ptr, uint32_t ext_offset,
                            uint8_t fmt, uint16_t color, uint8_t opacity,
                            uint16_t gen_tag);
void ui_mem_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint8_t *pal4_rle, uint8_t transparent0, uint8_t opacity);
void ui_text_draw(int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity, uint8_t scale);
int16_t ui_text_width(const char *utf8, uint8_t scale);
int16_t ui_text_height(const char *utf8, uint8_t scale);
void ui_text_draw_center(int16_t cx, int16_t y, const char *utf8,
                         uint16_t color, uint8_t opacity, uint8_t scale);
void ui_text_draw_right(int16_t right, int16_t y, const char *utf8,
                        uint16_t color, uint8_t opacity, uint8_t scale);
/* UTF-8 safe single-line fitting. Output is always NUL terminated. */
void ui_text_fit(char *out, int capacity, const char *utf8,
                 int16_t max_width, uint8_t scale);

/* ---- checked-in song font (Flash, 1:1) ----
   华文琥珀。0 是中文标题，1 是表盘数字；小字号中文仍走点阵字模。 */
enum { UI_SONG_TITLE = 0, UI_SONG_CLOCK = 1 };
void ui_song_draw(int face, int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity);
int16_t ui_song_width(int face, const char *utf8);
int16_t ui_song_height(int face);

/* ---- debug overlay ---- */
void ui_settings_set_debug(int enabled);
int ui_settings_get_debug(void);

/* ---- dynamic memory pool (arena) ---- */
void *ui_mem_alloc(uint32_t size, uint32_t align);
void ui_mem_free(void *ptr);
uint32_t ui_mem_avail(void);
void ui_mem_get(uint32_t *used_bytes, uint32_t *total_bytes);

/* ---- trace hook ---- */
typedef void (*ui_trace_fn)(const char *message);
void ui_set_trace(ui_trace_fn fn);

/* ---- BLE link (phone <-> watch), framed A5 cmd len data xor ----
   蓝牙串口。手机发来校时、消息、歌词；手表发回播放或暂停。每帧在 ui_frame 里收。 */
void link_init(void);
void link_poll(void);
const char *link_message(int index);
int link_brightness(void);
const char *link_media_lyric(void);
int link_media_playing(void);
/* 0x0B 下发的 128x32 点阵位图（4bpp 覆盖）；ready 为 0 时音乐页回退文字歌词。 */
int link_media_mask_ready(void);
const uint8_t *link_media_mask(void);
/* 消息页：列表按可见滑窗 6 行动态缓存、详情 128x160 1bpp，持久化于外置 32KB Flash 动态池。 */
int link_msg_list_ready(void);
int link_msg_list_empty(void);
int link_msg_count(void);
const uint8_t *link_msg_list_mask(void);
const uint8_t *link_msg_row_mask(int index);
int link_msg_detail_ready(void);
const uint8_t *link_msg_detail_mask(void);
void link_request_messages(void);
void link_request_message_detail(int index);
void link_leave_messages(void);
void link_clear_ext_messages(void);
void link_enter_music(void);
void link_leave_music(void);
enum {
    LINK_ASSET_COMPASS   = 1u, /* 指南针 64x64 1bpp 底图降级包 */
    LINK_ASSET_MSG_EMPTY = 2u, /* 保留兼容性编号 */
    LINK_ASSET_COMPASS3D = 3u, /* Plan B: ARM Cortex-M4 3D指南针 PIC 二进制包 ('C3D1') */
    LINK_ASSET_ABOUT     = 4u, /* 关于页放大彩图 + 华文琥珀简介缓存包 ('ABT2') */
    LINK_ASSET_HEADERS   = 5u, /* 片外 13 枚华文琥珀中文标题包 ('SHD1') */
    LINK_ASSET_SPORT     = 6u, /* 片外 运动步数 + 传感器诊断 PIC 包 ('SPR1') */
    LINK_ASSET_ANALOG    = 7u, /* 片外 墨竹指针表盘 + IMU 水平仪 PIC 包 ('ANL1') */
    LINK_ASSET_SYS       = 8u, /* 片外 主菜单动态图标 PIC 包 ('SYS1') */
    LINK_ASSET_FONT      = 9u, /* 片外 中文点阵字模尾包（非设置常用字） */
    LINK_ASSET_MUSIC     = 10u /* 片外 音乐页绘制 PIC 包 ('MUS1') */
};
void link_enter_compass(void);
void link_begin_compass(void);
int link_compass_loading(void);
void link_leave_compass(void);
int link_compass_asset_ready(void);
const uint8_t *link_compass_asset_mask(int16_t *w, int16_t *h);
int link_compass3d_ready(void);
const uint8_t *link_compass3d_blob(uint16_t *entry_off, uint16_t *total_size);
int link_about_asset_ready(void);
void link_enter_about(void);
void link_begin_about(void);
int link_about_loading(void);
void link_leave_about(void);
const uint8_t *link_about_blob(uint16_t *total_size);
int link_hdr_lookup(uint32_t cp, int16_t *w, int16_t *h, int16_t *adv,
                    int8_t *ox, int8_t *oy, const uint8_t **rle_out);
void link_hdr_active(int active);
void link_begin_pic_sport(void);
void link_leave_pic_sport(void);
int link_pic_sport_ready(void);
const uint8_t *link_pic_sport_blob(uint16_t *entry_off);
void link_begin_pic_analog(void);
void link_leave_pic_analog(void);
int link_pic_analog_ready(void);
const uint8_t *link_pic_analog_blob(uint16_t *entry_off);
void link_begin_pic_sys(void);
void link_leave_pic_sys(void);
int link_pic_sys_ready(void);
const uint8_t *link_pic_sys_blob(uint16_t *entry_off);
void link_begin_pic_music(void);
void link_leave_pic_music(void);
int link_pic_music_ready(void);
const uint8_t *link_pic_music_blob(uint16_t *entry_off);
const uint8_t *link_font_ext_bits(uint16_t off);
uint16_t link_img_gen(void);
void link_asset_pump(uint16_t max_bytes);
void link_asset_finish(void);
/* 外部 32KB Flash 分区与动态池（0x0000 设置双槽 + 0x0100 动态目录双槽 + 0x0300 统一动态池）。 */
enum {
    LINK_EXT_OFF_CFG        = 0x0000u, /*   256B: 双槽 64B 设置/表盘/校准持久化 (0x0000/0x0080) */
    LINK_EXT_OFF_DIR        = 0x0100u, /*   512B: 双槽 256B 动态片外 ROM 目录表 (0x0100/0x0200) */
    LINK_EXT_OFF_POOL       = 0x0300u, /* 32000B: 双端动态池 (低端放表盘图+ROM资源 / 高端放媒体+20条消息) */
    LINK_EXT_POOL_SIZE      = 32000u,
    LINK_EXT_MSG_MAX        = 20u,
    LINK_EXT_OFF_IMG        = 0x3C00u, /* 兼容底图测试与直接流式测试偏移 */
    LINK_EXT_IMG_MAX        = 0x5400u
};
void link_load_ext(void);
uint32_t link_ext_img_offset(void);
void link_ext_pool_stats(uint16_t *total, uint16_t *used, uint16_t *free_max,
                         uint16_t *msg_bytes, uint16_t *img_bytes, uint8_t *msg_n);
int link_img_preview_ready(void);
void link_img_preview_info(int16_t *w, int16_t *h, uint8_t *fmt, uint32_t *ext_offset);
void link_img_preview_clear(void);
void link_notify_settings(void);
void link_notify_config(void);
void ui_settings_apply(int key, int value);
void ui_app_set_message_count(int count);
/* 消息/音乐应用打开状态查询（消息返回 ui_menu_level：0 关闭 / 1 列表 / 2 详情）。 */
int ui_app_message_active(void);
int ui_app_music_active(void);
/* 最近 4 秒内收到过合法帧即认为 PC/手机在线。 */
int link_connected(void);
/* 调试模式打开后，07 命令里按住的键会并进按键电平。 */
uint8_t link_key_level(void);
void link_key_edges(uint8_t *down, uint8_t *up);
/* 本帧收完并画完之后调用，把 ok / bad 发出去，避免插进画面回传。 */
void link_ack_flush(void);
void link_media_play(void);
void link_media_pause(void);
void link_media_toggle(void);
void link_media_next(void);
void link_media_prev(void);
/* 手机发 06 后，下一帧把画面按 RGB565 从串口送回。 */
void link_snap_request(void);
void link_snap_begin(int w, int h);
void link_snap_pixels(const uint16_t *px, int n);
void link_snap_end(void);

/* ---- display hand-off ---- */
/* DMA 完成或 GIF 要出图时，在中断里调用。只置一个标志。 */
void ui_output_preempt(void);
/* 主循环里调用。这是盖在整帧上面的一块，不跟页面走。像素要一直有效到这一帧刷完。 */
void ui_gif_post(int16_t x, int16_t y, int16_t w, int16_t h,
                 const uint16_t *rgb565);

#endif /* WATCH_UI_H */