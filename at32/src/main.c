#include "ui.h"

/* AT32F421 极客手表：入口。硬件都在 at32/port.c 里。 */

void board_init(void);
void port_service(void);

int main(void) {
    board_init();
    ui_init();
    /* DMA / 等帧的空隙里跑串口解析 + 传感器采样 + 蜂鸣器，ACK 仍归帧处理。 */
    ui_set_service(port_service);
    for (;;) {
        ui_frame();
    }
}
