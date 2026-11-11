#ifndef WATCH_UI_H
#define WATCH_UI_H

#include "hal.h"
#include <stdint.h>

/* 手表界面系统。主循环只做这两句：
       ui_init();
       for (;;) ui_frame();
   换页、动画、调试开关用下面的函数，不要在页面里直接画点。

   调试模式打开后，左下角一直显示 RAM 和 CPU。
   CPU 大约每 0.5 秒才换一次数字。
   字模放在 Flash 里，不计入这 16KB。

   最外层左右翻页，内容直接画在背景上。
   进入下一级时，新页面从右往左盖住，上一页缩小后退。

   GIF / DMA 要抢屏幕时调用 ui_output_preempt()，
   当前帧还没送出的色带会停掉。主循环里再用 ui_gif_post()
   把一块 RGB565 贴上去。 */

#define UI_W HAL_LCD_W
#define UI_H HAL_LCD_H
#define UI_BAND_H 8
#define UI_RAM_BUDGET (16 * 1024)

/* 屏幕长边竖着放。页面按千分比排，不写死某一块屏幕的像素。
   0 贴着左或上，1000 贴着右或下。 */
#define UI_MX(m) ((m) * UI_W / 1000)
#define UI_MY(m) ((m) * UI_H / 1000)

typedef enum {
    UI_PAGE_FACE = 0,
    UI_PAGE_SPORT = 1,
    UI_PAGE_MSG = 2,
    UI_PAGE_SETTINGS = 3
} ui_page_id_t;

typedef enum {
    UI_ANIM_SLIDE_LEFT = 0,
    UI_ANIM_SLIDE_RIGHT = 1,
    UI_ANIM_FADE_IN = 2,
    UI_ANIM_FADE_OUT = 3,
    UI_ANIM_ZOOM_IN = 4,
    UI_ANIM_ZOOM_OUT = 5,
    UI_ANIM_STACK = 6
} ui_anim_id_t;

/* 动画只改这些属性。真正把属性画成像素是后面的事，调用处不用改。 */
typedef struct ui_obj {
    int16_t x;
    int16_t y;
    uint8_t opacity;
    uint8_t scale; /* 0 收起，255 铺满 */
} ui_obj_t;

typedef void (*ui_trace_fn)(const char *message);

void ui_set_trace(ui_trace_fn fn);

void ui_init(void);
void ui_frame(void);

void ui_nav_goto(ui_page_id_t page);

/* 按预设把 obj 从起点变到终点。ui_frame 里会推进时间。
   ui_anim_play_range 仍用预设的时长和属性，起止值由调用者给出。 */
void ui_anim_play(ui_anim_id_t id, ui_obj_t *obj);
void ui_anim_play_range(ui_anim_id_t id, ui_obj_t *obj, int16_t from, int16_t to);
int ui_anim_busy(const ui_obj_t *obj);
void ui_anim_tick(uint32_t now_ms);
uint16_t ui_anim_duration_ms(ui_anim_id_t id);

void ui_settings_set_debug(int enabled);
int ui_settings_get_debug(void);

/* 蓝牙串口。手机发来校时、消息、歌词；手表发回播放或暂停。
   帧：A5 命令 长度 数据 异或。异或含帧头。每帧在 ui_frame 里收。 */
void link_init(void);
void link_poll(void);
const char *link_message(int index);
int link_brightness(void);
const char *link_media_lyric(void);
int link_media_playing(void);
/* 调试模式打开后，07 命令里按住的键会并进按键电平。 */
uint8_t link_key_level(void);
void link_key_edges(uint8_t *down, uint8_t *up);
/* 本帧收完并画完之后调用，把 ok / bad 发出去，避免插进画面回传。 */
void link_ack_flush(void);
void link_media_play(void);
void link_media_pause(void);
void link_media_toggle(void);
/* 手机发 06 后，下一帧把画面按 RGB565 从串口送回。 */
void link_snap_request(void);
void link_snap_begin(int w, int h);
void link_snap_pixels(const uint16_t *px, int n);
void link_snap_end(void);

void ui_mem_get(uint32_t *used_bytes, uint32_t *total_bytes);

/* 矢量。r 为 0 时是从 (x0,y0) 到 (x1,y1) 的线段，r > 0 时是圆心 (x0,y0)、半径 r 的圆。
   坐标是图自己的，调用时再平移、再按整数放大。 */
typedef struct ui_svg_cmd {
    int8_t x0;
    int8_t y0;
    int8_t x1;
    int8_t y1;
    int8_t r;
} ui_svg_cmd_t;

/* 画进当前这一页。页面在放大、左右滑或被上一页缩小盖住时，图会跟着走。
   命令和像素要保持到这一帧刷完，放在静态存储里。 */
void ui_svg_draw(int16_t x, int16_t y, uint8_t scale,
                 const ui_svg_cmd_t *cmd, int count,
                 uint16_t color, uint8_t opacity);
void ui_image_draw(int16_t x, int16_t y, int16_t w, int16_t h,
                   uint8_t scale, const uint16_t *rgb565, uint8_t opacity);
void ui_text_draw(int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity, uint8_t scale);
int16_t ui_text_width(const char *utf8, uint8_t scale);

/* 华文琥珀。0 是中文标题，1 是表盘数字。
   小字号的中文也从华文琥珀取。字模在 Flash，按 1:1 画。 */
enum { UI_SONG_TITLE = 0, UI_SONG_CLOCK = 1 };
void ui_song_draw(int face, int16_t x, int16_t y, const char *utf8,
                  uint16_t color, uint8_t opacity);
int16_t ui_song_width(int face, const char *utf8);
int16_t ui_song_height(int face);

/* DMA 完成或 GIF 要出图时，在中断里调用。只置一个标志。 */
void ui_output_preempt(void);

/* 主循环里调用。这是盖在整帧上面的一块，不跟页面走。
   像素要一直有效到这一帧刷完。 */
void ui_gif_post(int16_t x, int16_t y, int16_t w, int16_t h,
                 const uint16_t *rgb565);

#endif /* WATCH_UI_H */
