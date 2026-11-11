#ifndef HAL_HOST_H
#define HAL_HOST_H

#include <stdint.h>

/* 电脑端口的测试和窗口钩子。界面代码不包含这个文件。 */

void hal_host_init(int create_window);
void hal_host_shutdown(void);
void hal_host_use_virtual_time(void);
void hal_host_set_time_us(uint64_t us);
void hal_host_advance_us(uint32_t us);
void hal_host_set_frame_step_us(uint32_t us);
void hal_host_set_keys(uint8_t level);

/* Delayed DMA borrows the original pointer, checking it against a snapshot
   until completion. All counters are host-only, outside the MCU arena. */
typedef struct hal_host_display_stats {
    uint32_t calls, pixels, completed, busy_polls;
    uint32_t overwrites, submit_while_busy, invalid_regions;
    uint32_t pointer_changes;
    int min_x, min_y, max_x, max_y, max_w, max_h;
} hal_host_display_stats_t;
void hal_host_set_async_delay_us(uint32_t us);
void hal_host_display_reset_stats(void);
void hal_host_display_get_stats(hal_host_display_stats_t *out);
void hal_host_copy_frame(uint16_t *out);
void hal_host_link_clear(void);
void hal_host_link_rx(const uint8_t *data, int len);
void hal_host_link_tx_take(uint8_t *dst, int cap, int *out_n);
uint16_t hal_host_pixel(int x, int y);
int hal_host_save_bmp(const char *path);
int hal_host_running(void);
void hal_host_pump(void);

/* hdc 是 Windows 的 HDC，用 void* 是为了不让这个头文件依赖 windows.h。 */
void hal_host_set_painter(void (*fn)(void *hdc, int x, int panel_w, int height));

#endif
