#include "hal.h"
#include "hal_host.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

static const char *shot_dir;
static int shot_errors;

static void save_shot(const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", shot_dir, name);
    if (!hal_host_save_bmp(path)) {
        fprintf(stderr, "FAIL: cannot save %s\n", path);
        shot_errors++;
    }
}

static void send_link(uint8_t cmd, const uint8_t *pay, int n) {
    uint8_t buf[40];
    int i;
    uint8_t sum = 0;

    buf[0] = 0xA5;
    buf[1] = cmd;
    buf[2] = (uint8_t)n;
    for (i = 0; i < n; i++) {
        buf[3 + i] = pay[i];
    }
    for (i = 0; i < 3 + n; i++) {
        sum ^= buf[i];
    }
    buf[3 + n] = sum;
    hal_host_link_rx(buf, 4 + n);
}

static int region_diff(void) {
    int diff = 0;
    int x, y;
    int bh = UI_MY(220);
    int top;
    int bot;

    if (bh < 28) {
        bh = 28;
    }
    top = UI_H - bh - UI_MY(16);
    bot = top + bh - 6;
    for (y = top + 8; y <= bot; y++) {
        for (x = UI_MX(16) + 4; x <= UI_MX(16) + 22; x++) {
            if (hal_host_pixel(x, y) != hal_host_pixel(x + UI_W * 3 / 4, y)) {
                diff++;
            }
        }
    }
    return diff;
}

static void dot_at(int page, int *x, int *y) {
    int gap = UI_MX(110);
    int h = UI_MY(40);
    int lift = UI_MY(40);
    int w = UI_MX(62);
    int x0;

    if (gap < 8) {
        gap = 8;
    }
    if (h < 6) {
        h = 6;
    }
    if (lift < 6) {
        lift = 6;
    }
    if (w < 3) {
        w = 3;
    }
    /* 与 ui_app.c draw_dots 一致：HOME_N=5，dots_y=UI_H-4-lift-h。 */
    x0 = (UI_W - 4 * gap) / 2;
    *x = x0 + page * gap - UI_MX(16) + w / 2;
    *y = UI_H - 4 - lift - h + h / 2;
}

static void card_at(int *x, int *y) {
    int header = UI_MY(150);
    int ch = UI_MY(210);
    int cw = UI_MX(840);
    int y0 = header + (UI_H - header) / 2;
    int card_y = y0 - ch / 2;
    int card_x = (UI_W - cw) / 2;

    *x = card_x + (cw - 16) / 2;
    *y = card_y + (ch - 16) / 2;
}

static void frames(int n) {
    int i;
    for (i = 0; i < n; i++) {
        ui_frame();
    }
}

static void tap(uint8_t key) {
    hal_host_set_keys(key);
    ui_frame();
    hal_host_set_keys(0);
    ui_frame();
}

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3));
}

static uint32_t region_sig(int x, int y, int w, int h) {
    uint32_t s = 2166136261u;
    int ix, iy;

    for (iy = 0; iy < h; iy++) {
        for (ix = 0; ix < w; ix++) {
            s ^= hal_host_pixel(x + ix, y + iy);
            s *= 16777619u;
        }
    }
    return s;
}

static int fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int host_self_test(const char *output_dir) {
    uint32_t used = 0;
    uint32_t total = 0;
    uint32_t used_later = 0;
    ui_obj_t obj;
    uint32_t t;
    uint16_t dur;
    uint32_t sub_card;
    int diff_on;
    int diff_off;
    uint16_t cpu;

    shot_dir = output_dir;
    shot_errors = 0;
    hal_host_init(0);
    hal_host_use_virtual_time();
    {
        const hal_clock_t clock = {2026, 11, 11, 8, 30, 0};
        hal_clock_set(&clock);
    }
    ui_init();

    ui_mem_get(&used, &total);
    printf("RAM %lu / %lu\n", (unsigned long)used, (unsigned long)total);
    if (total != UI_RAM_BUDGET) {
        return fail("budget");
    }
    if (used < (uint32_t)UI_W * UI_BAND_H * 2u) {
        return fail("band not in arena");
    }
    if (used >= total) {
        return fail("arena full");
    }

    obj.x = 99;
    obj.y = 0;
    obj.opacity = 9;
    t = hal_tick_ms();
    ui_anim_play(UI_ANIM_SLIDE_LEFT, &obj);
    if (obj.x != 0) {
        return fail("slide snap");
    }
    ui_anim_tick(t);
    if (obj.x != 0) {
        return fail("slide start");
    }
    dur = ui_anim_duration_ms(UI_ANIM_SLIDE_LEFT);
    ui_anim_tick(t + dur / 2u);
    if (obj.x >= -(int)HAL_LCD_W / 2) {
        fprintf(stderr, "FAIL: ease midpoint x=%d\n", obj.x);
        return 1;
    }
    ui_anim_tick(t + dur);
    if (obj.x != -(int)HAL_LCD_W) {
        fprintf(stderr, "FAIL: slide end x=%d\n", obj.x);
        return 1;
    }
    if (ui_anim_busy(&obj)) {
        return fail("slide still busy");
    }

    obj.opacity = 80;
    t = hal_tick_ms();
    ui_anim_play(UI_ANIM_FADE_IN, &obj);
    if (obj.opacity != 0) {
        return fail("fade snap");
    }
    ui_anim_tick(t + ui_anim_duration_ms(UI_ANIM_FADE_IN));
    if (obj.opacity != 255) {
        return fail("fade end");
    }

    if (ui_settings_get_debug()) {
        return fail("debug default");
    }
    frames(3);
    if (hal_host_pixel(30, 28) == 0) {
        return fail("flush did not update");
    }
    ui_settings_set_debug(1);
    frames(20);
    save_shot("shot_launcher.bmp");
    diff_on = region_diff();
    printf("overlay diff %d\n", diff_on);
    if (diff_on < 30) {
        return fail("overlay missing");
    }
    /* 连按两次右键，动画从当前页改到消息，而不是先走完运动。 */
    tap(HAL_KEY3);
    tap(HAL_KEY3);
    frames(24);
    save_shot("shot_focus.bmp");
    /* 调试角标是底部覆盖层，会盖住导航点；查点前先关掉。 */
    ui_settings_set_debug(0);
    frames(24);
    {
        int dx, dy;
        dot_at(2, &dx, &dy);
        if (hal_host_pixel(dx, dy) != rgb565(150, 210, 255)) {
            fprintf(stderr, "FAIL: page dot=%04x at %d,%d\n", hal_host_pixel(dx, dy), dx, dy);
            return 1;
        }
    }

    tap(HAL_KEY3);
    frames(20);
    tap(HAL_KEY2);
    frames(24);
    /* 设置扩为6项，亮度在索引3：移三格再进入子页 */
    tap(HAL_KEY3);
    frames(12);
    tap(HAL_KEY3);
    frames(12);
    tap(HAL_KEY3);
    frames(12);
    tap(HAL_KEY2);
    frames(8);
    save_shot("shot_push.bmp");
    frames(16);
    save_shot("shot_settings.bmp");
    save_shot("shot_settings_stack.bmp");
    {
        int cx, cy;
        int header = UI_MY(150);
        int y0 = header + (UI_H - header) / 2;
        int fill_x = (UI_W - UI_MX(840)) / 2 + UI_MX(60);
        card_at(&cx, &cy);
        if (hal_host_pixel(fill_x, y0) != rgb565(0, 114, 230)) {
            fprintf(stderr, "FAIL: bright stack=%04x at %d,%d\n",
                    hal_host_pixel(fill_x, y0), fill_x, y0);
            return 1;
        }
        sub_card = region_sig(cx, cy, 16, 16);
        tap(HAL_KEY3);
        frames(20);
        if (region_sig(cx, cy, 16, 16) == sub_card) {
            return fail("detail stack did not move");
        }
        tap(HAL_KEY1);
        frames(20);
        if (region_sig(cx, cy, 16, 16) != sub_card) {
            return fail("detail stack did not return");
        }
    }

    tap(HAL_KEY1);
    frames(20);
    tap(HAL_KEY1);
    frames(20);
    tap(HAL_KEY1);
    frames(20);
    tap(HAL_KEY1);
    frames(16);
    tap(HAL_KEY1);
    frames(16);
    tap(HAL_KEY1);
    frames(16);
    {
        int dx, dy;
        dot_at(0, &dx, &dy);
        if (hal_host_pixel(dx, dy) == rgb565(150, 210, 255)) {
            return fail("clock shows dots");
        }
    }

    tap(HAL_KEY3);
    frames(20);
    {
        int seen[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        int got = 0;
        int guard = 0;
        while (got < 8 && guard < 120) {
            int pose = (int)((hal_tick_ms() / 70u) % 8u);
            frames(1);
            pose = (int)((hal_tick_ms() / 70u) % 8u);
            if (!seen[pose]) {
                char path[64];
                snprintf(path, sizeof path, "shot_run_%d.bmp", pose);
                save_shot(path);
                seen[pose] = 1;
                got++;
            }
            guard++;
        }
        if (got != 8) {
            return fail("run frames");
        }
    }
    tap(HAL_KEY2);   /* 零级动画采集完，进入一级柱状图 */
    frames(24);

    {
        static const uint16_t gif_px[4] = {0xF800, 0xF800, 0xF800, 0xF800};
        ui_gif_post(2, 2, 2, 2, gif_px);
        ui_frame();
        if (hal_host_pixel(2, 2) != 0xF800) {
            fprintf(stderr, "FAIL: gif preempt=%04x\n", hal_host_pixel(2, 2));
            return 1;
        }
    }

    tap(HAL_KEY1);   /* 退出运动菜单 */
    frames(24);
    tap(HAL_KEY1);   /* 确保回到首页 */
    frames(24);
    ui_nav_goto(UI_PAGE_MSG);
    frames(2);
    {
        int ix = UI_MX(125), iy = UI_MY(275), iw = UI_MX(375), ih = UI_MY(100);
        uint32_t icon = region_sig(ix, iy, iw, ih);
        tap(HAL_KEY2);
        frames(24);
        if (region_sig(ix, iy, iw, ih) == icon) {
            return fail("msg list did not open");
        }
        {
            int header = ui_song_height(UI_SONG_TITLE) + UI_MY(20);
            int y0;
            if (header < UI_MY(150)) {
                header = UI_MY(150);
            }
            y0 = header + (UI_H - header) / 2;
            int fill_x = (UI_W - UI_MX(840)) / 2 + UI_MX(60);
            if (hal_host_pixel(fill_x, y0) != rgb565(0, 114, 230)) {
                return fail("msg list is not the stack");
            }
        }
        save_shot("shot_msg.bmp");
        tap(HAL_KEY2);
        frames(20);
        {
            int px = (UI_W - 16 * 4) / 2 + 11 * 4;
            int py = UI_MY(280) + 2 * 4;
            if (hal_host_pixel(px, py) != 0xFE84) {
                fprintf(stderr, "FAIL: msg detail image=%04x at %d,%d\n",
                        hal_host_pixel(px, py), px, py);
                return 1;
            }
        }
        save_shot("shot_msg_detail.bmp");
        hal_host_set_keys(HAL_KEY2);
        frames(32);
        hal_host_set_keys(0);
        frames(40);
        hal_host_set_keys(HAL_KEY2);
        frames(32);
        hal_host_set_keys(0);
        frames(40);
        /* 消息图标是动画，区域签名随时间变；改用“列表蓝卡消失”判断已关闭。 */
        {
            int header = ui_song_height(UI_SONG_TITLE) + UI_MY(20);
            int y0, fill_x;
            if (header < UI_MY(150)) {
                header = UI_MY(150);
            }
            y0 = header + (UI_H - header) / 2;
            fill_x = (UI_W - UI_MX(840)) / 2 + UI_MX(60);
            if (hal_host_pixel(fill_x, y0) == rgb565(0, 114, 230)) {
                return fail("long enter did not close msg");
            }
        }
        {
            int px = (UI_W - 16 * 4) / 2 + 11 * 4;
            int py = UI_MY(280) + 2 * 4;
            if (hal_host_pixel(px, py) == 0xFE84) {
                return fail("msg image stayed on icon");
            }
        }
    }

    ui_settings_set_debug(0);
    frames(20);
    diff_off = region_diff();
    printf("overlay diff after off %d\n", diff_off);
    if (diff_off != 0) {
        return fail("overlay still visible");
    }
    if (ui_settings_get_debug()) {
        return fail("debug still on");
    }

    {
        uint8_t time_pay[7];
        uint8_t msg_pay[8];
        uint8_t lyric_pay[6];
        uint8_t tx[16];
        int tx_n = 0;
        hal_clock_t clock;
        const char *pushed = "\xE7\x89\x88\xE6\x9C\xAC";
        time_pay[0] = 0xEA;
        time_pay[1] = 0x07;
        time_pay[2] = 11;
        time_pay[3] = 11;
        time_pay[4] = 8;
        time_pay[5] = 30;
        time_pay[6] = 0;
        send_link(0x01, time_pay, 7);
        msg_pay[0] = 1;
        memcpy(msg_pay + 1, pushed, 6);
        send_link(0x02, msg_pay, 7);
        memcpy(lyric_pay, "\xE8\xBF\x90\xE5\x8A\xA8", 6);
        send_link(0x03, lyric_pay, 6);
        send_link(0x04, (const uint8_t *)"\x01", 1);
        frames(1);
        hal_clock_get(&clock);
        if (clock.year != 2026 || clock.month != 11 || clock.day != 11 ||
            clock.hour != 8 || clock.minute != 30) {
            return fail("time sync");
        }
        if (strcmp(link_message(1), pushed) != 0) {
            return fail("message push");
        }
        if (!link_media_playing() || strcmp(link_media_lyric(), "\xE8\xBF\x90\xE5\x8A\xA8") != 0) {
            return fail("lyric");
        }
        hal_host_link_tx_take(tx, 16, &tx_n);
        link_media_toggle();
        hal_host_link_tx_take(tx, 16, &tx_n);
        if (tx_n != 5 || tx[0] != 0xA5 || tx[1] != 0x10 || tx[3] != 2 ||
            link_media_playing()) {
            return fail("media pause");
        }
        send_link(0x06, 0, 0);
        frames(1);
        hal_host_link_tx_take(tx, 16, &tx_n);
        if (tx_n < 11 || tx[0] != 0xA5 || tx[1] != 0x11 || tx[2] != 0xFF ||
            tx[6] != (UI_W & 255) || tx[7] != (UI_W >> 8) ||
            tx[8] != (UI_H & 255) || tx[9] != (UI_H >> 8)) {
            return fail("snap");
        }
    }

    ui_mem_get(&used_later, &total);
    if (used_later != used) {
        fprintf(stderr, "FAIL: ram changed %lu -> %lu\n",
                (unsigned long)used, (unsigned long)used_later);
        return 1;
    }
    cpu = hal_cpu_tenths();
    printf("CPU %u.%u%%\n", cpu / 10u, cpu % 10u);
    if (cpu > 1000) {
        return fail("cpu range");
    }
    if (shot_errors) return fail("screenshot output");
    printf("Behavior PASS %dx%d\n", UI_W, UI_H);
    return 0;
}
