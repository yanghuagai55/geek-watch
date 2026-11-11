/* One-off preview: render the Gemini-generated icon tables through the real
   UI renderer so they can be judged on-host before integration. */
#include "hal_host.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* ui.h already defines ui_svg_cmd_t identically; skip the asset's own typedef. */
#define UI_SVG_CMD_T_DEFINED
#include "gemini_icons.h"

static const int8_t sin16[16] = {
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49
};

static int tab(int pos) {
    int i = (pos >> 8) & 15;
    int f = pos & 255;
    int a = sin16[i], b = sin16[(i + 1) & 15];
    return (a * (256 - f) + b * f) >> 8;
}

static void rot_cmd(ui_svg_cmd_t *d, const ui_svg_cmd_t *s, int cx, int cy, int ph) {
    int sn = tab(ph), cs = tab(ph + 1024);
    int x = s->x0 - cx, y = s->y0 - cy;
    d->x0 = (int8_t)(cx + (x * cs - y * sn) / 127);
    d->y0 = (int8_t)(cy + (x * sn + y * cs) / 127);
    x = s->x1 - cx; y = s->y1 - cy;
    d->x1 = (int8_t)(cx + (x * cs - y * sn) / 127);
    d->y1 = (int8_t)(cy + (x * sn + y * cs) / 127);
    d->r = s->r;
    d->ci = s->ci;
}

static void shot(const char *name) {
    char path[128];
    ui_draw_flush();
    snprintf(path, sizeof path, "host/obj/preview/g_%s.bmp", name);
    hal_host_save_bmp(path);
}

static void bg(void) {
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0x0063, 255);
}

int main(void) {
    static ui_svg_cmd_t rot[7];
    char name[16];
    int i, k;

    hal_host_init(0);
    hal_host_use_virtual_time();
    ui_core_init();

    for (k = 0; k < 5; k++) {
        bg();
        ui_svg_draw_p(16, 26, 3, run_svg[k], 10, icon_pal, 5, 0xFCE1, 255, 5);
        snprintf(name, sizeof name, "run%d", k);
        shot(name);
    }
    for (k = 0; k < 4; k++) {
        bg();
        ui_svg_draw_p(16, 52, 2, mail_svg[k], 11, icon_pal, 5, 0x969F, 255, 3);
        snprintf(name, sizeof name, "mail%d", k);
        shot(name);
    }
    for (i = 0; i < 4; i++) {
        int ph = i * 1024; /* 0, 90, 180, 270 deg in 8.8 table units */
        bg();
        for (k = 0; k < 7; k++) {
            rot_cmd(&rot[k], &gear_svg[0][k], 16, 16, ph);
        }
        ui_svg_draw_p(16, 32, 3, rot, 7, icon_pal, 5, 0xFCE1, 255, 4);
        snprintf(name, sizeof name, "gear%d", i);
        shot(name);
    }
    hal_host_shutdown();
    printf("preview shots written\n");
    return 0;
}