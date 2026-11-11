#include "ui.h"

void board_init(void);

int main(void) {
    board_init();
    ui_init();
    /* Bounded parsing runs during DMA/pacing waits; ACKs stay frame-owned. */
    ui_set_service(link_poll);
    for (;;) {
        ui_frame();
    }
}
