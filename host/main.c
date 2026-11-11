#include "hal_host.h"
#include "panel.h"
#include "ui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <direct.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int host_self_test(const char *output_dir);
int host_render_test(const char *output_dir);

static int make_dirs(const char *path) {
    char full[1024];
    char *p;
    if (!_fullpath(full, path, sizeof full)) return 0;
    for (p = full + 3; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char slash = *p;
            *p = 0;
            if (_mkdir(full) != 0 && errno != EEXIST) return 0;
            *p = slash;
        }
    }
    return _mkdir(full) == 0 || errno == EEXIST;
}

static void default_shots(char *out, size_t cap) {
    char exe[MAX_PATH];
    char *last;
    GetModuleFileNameA(NULL, exe, sizeof exe);
    last = strrchr(exe, '\\');
    if (last) *last = 0;
    last = strrchr(exe, '\\');
    if (last && strcmp(last + 1, "obj") == 0) *last = 0;
    snprintf(out, cap, "%s/obj/check_%dx%d", exe, UI_W, UI_H);
}

static void headless_init(void) {
    const hal_clock_t clock = {2026, 11, 11, 8, 30, 0};
    int i;
    hal_host_init(0);
    hal_host_use_virtual_time();
    hal_clock_set(&clock);
    ui_init();
    for (i = 0; i < 32; i++) ui_frame();
}

static int preview_reply(const char *path) {
    ui_render_stats_t stats;
    uint32_t used, total;
    if (!hal_host_save_bmp(path)) {
        fprintf(stderr, "Cannot save framebuffer: %s\n", path);
        return 0;
    }
    ui_render_get_stats(&stats);
    ui_mem_get(&used, &total);
    printf("{\"width\":%d,\"height\":%d,\"pixels\":%lu,\"bands\":%u,"
           "\"frame_us\":%lu,\"ram\":%lu,\"budget\":%lu,\"time_ms\":%lu}\n",
           UI_W, UI_H, (unsigned long)stats.pixels, stats.bands,
           (unsigned long)stats.frame_us, (unsigned long)used,
           (unsigned long)total, (unsigned long)hal_tick_ms());
    fflush(stdout);
    return 1;
}

/* A persistent C simulator, driven by the localhost preview bridge. Each
   input line is the actual key bitmask and number of 16ms UI frames. */
static int preview(const char *path) {
    char line[128];
    unsigned keys, count;
    headless_init();
    if (!preview_reply(path)) return 1;
    while (fgets(line, sizeof line, stdin)) {
        if (sscanf(line, "%u %u", &keys, &count) != 2 || keys > 7 || count > 600) {
            fprintf(stderr, "Preview expects: key_mask(0..7) frame_count(0..600)\n");
            return 2;
        }
        hal_host_set_keys((uint8_t)keys);
        while (count-- > 0) ui_frame();
        if (!preview_reply(path)) return 1;
    }
    hal_host_shutdown();
    return 0;
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc > 1 && (strcmp(argv[1], "--check") == 0 ||
                     strcmp(argv[1], "--render-check") == 0)) {
        char shots[1024];
        int behavior = 0, render;
        default_shots(shots, sizeof shots);
        if (argc > 2) snprintf(shots, sizeof shots, "%s", argv[2]);
        if (!make_dirs(shots)) {
            fprintf(stderr, "Cannot create screenshot directory: %s\n", shots);
            return 1;
        }
        if (strcmp(argv[1], "--check") == 0) behavior = host_self_test(shots);
        render = host_render_test(shots);
        printf("Shots: %s\n", shots);
        return behavior || render;
    }
    if (argc > 2 && strcmp(argv[1], "--preview") == 0) return preview(argv[2]);
    if (argc > 2 && strcmp(argv[1], "--snapshot") == 0) {
        const char *p = argc > 3 ? argv[3] : "";
        int i, ok;
        headless_init();
        for (; *p; p++) {
            uint8_t key = *p == 'l' ? HAL_KEY1 : *p == 'r' ? HAL_KEY3 : HAL_KEY2;
            hal_host_set_keys(key);
            for (i = 0; i < (*p == 'h' ? 32 : 1); i++) ui_frame();
            hal_host_set_keys(0);
            for (i = 0; i < 24; i++) ui_frame();
        }
        ok = preview_reply(argv[2]);
        hal_host_shutdown();
        return ok ? 0 : 1;
    }
    if (argc > 1 && !(argc == 3 && strcmp(argv[1], "--async") == 0)) {
        fprintf(stderr, "Usage: watch_ui [--check [shots] | --render-check [shots] | "
                "--snapshot file.bmp [lreh...] | --preview file.bmp | --async delay_us]\n");
        return 2;
    }

    printf("左右键翻页，回车进入，长按回车返回。\n");
    hal_host_set_painter(host_panel_paint);
    hal_host_init(1);
    if (argc == 3) hal_host_set_async_delay_us((uint32_t)strtoul(argv[2], NULL, 10));
    ui_set_trace(host_panel_trace);
    ui_set_service(hal_host_pump);
    ui_init();
    while (hal_host_running()) {
        hal_host_pump();
        if (!hal_host_running()) break;
        ui_frame();
    }
    ui_set_service(NULL);
    hal_host_shutdown();
    return 0;
}
