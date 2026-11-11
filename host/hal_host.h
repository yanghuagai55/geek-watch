#ifndef HAL_HOST_H
#define HAL_HOST_H

#include <stdint.h>

/* 电脑端口的测试和窗口钩子。界面代码不包含这个文件。 */

void hal_host_init(int create_window);
void hal_host_shutdown(void);
void hal_host_use_virtual_time(void);
void hal_host_set_keys(uint8_t level);
void hal_host_link_rx(const uint8_t *data, int len);
void hal_host_link_tx_take(uint8_t *dst, int cap, int *out_n);
uint16_t hal_host_pixel(int x, int y);
int hal_host_save_bmp(const char *path);
int hal_host_running(void);
void hal_host_pump(void);

/* hdc 是 Windows 的 HDC，用 void* 是为了不让这个头文件依赖 windows.h。 */
void hal_host_set_painter(void (*fn)(void *hdc, int x, int panel_w, int height));

#endif
