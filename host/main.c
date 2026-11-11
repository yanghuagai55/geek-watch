#include "hal_host.h"
#include "panel.h"
#include "ui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

int host_self_test(void);

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc > 1 && strcmp(argv[1], "--check") == 0) {
        return host_self_test();
    }

    printf("左右键翻页。消息和设置是图标，回车放大进入。\n");
    printf("进去之后再选亮度或关于，回车，新页面从右边盖上来。\n");

    hal_host_set_painter(host_panel_paint);
    hal_host_init(1);
    ui_set_trace(host_panel_trace);
    ui_init();

    while (hal_host_running()) {
        hal_host_pump();
        if (!hal_host_running()) {
            break;
        }
        ui_frame();
    }
    hal_host_shutdown();
    return 0;
}
