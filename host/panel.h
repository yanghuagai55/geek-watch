#ifndef HOST_PANEL_H
#define HOST_PANEL_H

/* 窗口右侧的说明。用系统字体画，不属于固件。 */

void host_panel_trace(const char *message);
void host_panel_paint(void *hdc, int x, int panel_w, int height);

#endif
