#include "ui.h"

void board_init(void);

int main(void) {
    board_init();
    ui_init();
    for (;;) {
        ui_frame();
    }
}
