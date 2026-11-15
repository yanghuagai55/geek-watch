#include "hal.h"
#include "hal_host.h"
#include "imu9.h"
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
    uint8_t buf[160];
    int i;
    uint8_t sum = 0;

    if (n < 0 || 4 + n > (int)sizeof buf) {
        return;
    }
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
    int bx = UI_MX(16);
    int top = UI_H - 18;
    int bot = UI_H - 4;

    /* 采样角标左侧专属边条区 [bx+1 .. bx+3] 对比最右侧空白边距 [UI_W-1]，不受 FPS 位数变宽影响。 */
    for (y = top; y <= bot; y++) {
        for (x = bx + 1; x <= bx + 3; x++) {
            if (hal_host_pixel(x, y) != hal_host_pixel(UI_W - 1, y)) {
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

static int header_h(int collapse) {
    int title_h = ui_song_height(UI_SONG_TITLE);
    int max_band = title_h + UI_MY(50);
    int min_scale = 136;
    int min_band = title_h * min_scale / 256 + UI_MY(45);

    if (collapse <= 0) {
        return max_band;
    }
    if (collapse >= 256) {
        return min_band;
    }
    return max_band - (max_band - min_band) * collapse / 256;
}

static int list_pitch(void) {
    int pitch = UI_MY(195);
    return pitch < 24 ? 24 : pitch;
}

static int list_cam(int scroll, int n) {
    int cam;
    int max_cam;

    if (n <= 4 || scroll <= 256) {
        return 0;
    }
    cam = scroll - 256;
    max_cam = (n - 4) * 256;
    if (cam > max_cam) {
        cam = max_cam;
    }
    return cam;
}

static void list_card_at(int scroll, int n, int index,
                         int *bx, int *by, int *cx, int *cy, int *ch_out) {
    int collapse = scroll <= 0 ? 0 : (scroll >= 256 ? 256 : scroll);
    int band = header_h(collapse);
    int top0 = band + UI_MY(25);
    int cw = UI_MX(860);
    int ch = UI_MY(175);
    int card_x = (UI_W - cw) / 2;
    int card_y;

    if (ch < 20) {
        ch = 20;
    }
    card_y = top0 + (index * 256 - list_cam(scroll, n)) * list_pitch() / 256;
    if (bx) *bx = card_x;
    if (by) *by = card_y;
    /* Sample the label, not the empty middle of the row. */
    if (cx) *cx = card_x + UI_MX(55);
    if (cy) *cy = card_y + (ch - 8) / 2;
    if (ch_out) *ch_out = ch;
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
    {
        int rx, ry, lit = 0;
        for (ry = UI_MY(220); ry < UI_MY(760) && !lit; ry += 2) {
            for (rx = UI_W / 4; rx < UI_W * 3 / 4; rx += 2) {
                if (hal_host_pixel(rx, ry) != 0) {
                    lit = 1;
                    break;
                }
            }
        }
        if (!lit) {
            return fail("flush did not update");
        }
        if (hal_host_pixel(0, 0) != UI_RGB565(0, 0, 0) ||
            hal_host_pixel(0, UI_H - 1) != UI_RGB565(0, 0, 0)) {
            return fail("global background not pure black");
        }
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
        if (hal_host_pixel(dx, dy) != UI_COL_MINT) {
            fprintf(stderr, "FAIL: page dot=%04x at %d,%d\n", hal_host_pixel(dx, dy), dx, dy);
            return 1;
        }
    }

    tap(HAL_KEY3);
    frames(20);
    tap(HAL_KEY2);
    frames(24);
    /* 设置一级页：初始展开头栏，下滚第1格缩起头栏并露出4张完整选项卡，继续下滚贴在黑底阴影头栏下。 */
    {
        int bx, by, ch, bx3, by3, ch3;
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) != UI_COL_LINE) {
            return fail("settings expanded header line missing");
        }
        list_card_at(0, 8, 0, &bx, &by, 0, 0, &ch);
        if (hal_host_pixel(bx, by + ch / 2) != UI_COL_MINT) {
            return fail("settings row 0 not focused");
        }
        save_shot("shot_settings.bmp");

        tap(HAL_KEY3);
        frames(8);
        save_shot("shot_settings_tuck.bmp");
        frames(16);
        if (hal_host_pixel(UI_W / 2, header_h(256) - 1) != UI_COL_LINE) {
            return fail("settings collapsed header line missing");
        }
        list_card_at(256, 8, 3, &bx3, &by3, 0, 0, &ch3);
        if (by3 + ch3 - 1 >= UI_H ||
            hal_host_pixel(bx3, by3 + ch3 / 2) != UI_COL_LINE) {
            return fail("settings 4th option not cleanly visible");
        }

        tap(HAL_KEY3);
        frames(24);
        save_shot("shot_settings_scrolled.bmp");
        if (hal_host_pixel(UI_W / 2, header_h(256) - 3) != UI_RGB565(0, 0, 0)) {
            return fail("collapsed header backing not opaque black");
        }
        if (hal_host_pixel(0, header_h(256) + 2) != UI_RGB565(0, 0, 0)) {
            return fail("unexpected transition band under header");
        }
    }
    /* 从索引 2 长按 KEY3 连续前进，验证灰圆在选项上移动 -> 松开在该选项扩散 -> 长按 KEY1 停在第 0 项。 */
    {
        int bx, by, ch, sy, found_dot = 0, found_exp = 0;
        hal_host_set_keys(HAL_KEY3);
        frames(29); /* 2->3(0ms), 3->4(320ms), 4->5(435ms) */
        save_shot("shot_hold_dot.bmp");
        for (sy = header_h(256) + 2; sy < UI_H - 2; sy++) {
            uint16_t p = hal_host_pixel(UI_W / 2, sy);
            if (p != UI_COL_BG && p != UI_COL_SURFACE &&
                p != UI_COL_LINE && p != UI_COL_MINT &&
                p != UI_COL_TEXT && p != UI_COL_MUTED) {
                found_dot = 1;
                break;
            }
        }
        if (!found_dot) {
            return fail("hold moving gray circle missing");
        }
        hal_host_set_keys(0);
        frames(3);
        save_shot("shot_ripple_expand.bmp");
        list_card_at(1280, 8, 5, &bx, &by, 0, 0, &ch);
        for (sy = by + 2; sy < by + ch - 2; sy++) {
            uint16_t p = hal_host_pixel(bx + UI_MX(430), sy);
            if (p != UI_COL_SURFACE && p != UI_COL_BG) {
                found_exp = 1;
                break;
            }
        }
        if (!found_exp) {
            return fail("release ripple did not expand over card");
        }
        frames(20); /* 扩散完毕恢复卡片底色，焦点停在索引 5 不再继续前进 */
        if (hal_host_pixel(bx, by + ch / 2) != UI_COL_MINT ||
            hal_host_pixel(bx + UI_MX(430), by + ch / 2) != UI_COL_SURFACE) {
            return fail("ripple did not settle on landed focus 5");
        }

        /* 长按 KEY1 连续后退：必须停在第 0 项，不得自动退回第 0 级桌面。 */
        hal_host_set_keys(HAL_KEY1);
        frames(70);
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) != UI_COL_LINE) {
            return fail("long KEY1 exited menu past item 0");
        }
        list_card_at(0, 8, 0, &bx, &by, 0, 0, &ch);
        if (hal_host_pixel(bx, by + ch / 2) != UI_COL_MINT) {
            return fail("long KEY1 did not stop on item 0");
        }
        hal_host_set_keys(0);
        frames(3);
        save_shot("shot_ripple_stop0.bmp");
        frames(20);
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) != UI_COL_LINE ||
            hal_host_pixel(bx, by + ch / 2) != UI_COL_MINT) {
            return fail("released KEY1 at item 0 left menu");
        }
        /* 松手后再点一次 KEY1 -> 返回第 0 级，再按 KEY2 重新进入设置并移到索引 2 以衔接下文测试。 */
        tap(HAL_KEY1);
        frames(24);
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) == UI_COL_LINE) {
            return fail("second KEY1 click at item 0 did not return");
        }
        tap(HAL_KEY2);
        frames(24);
        tap(HAL_KEY3);
        frames(2);
        tap(HAL_KEY3);
        frames(24);
    }
    /* 设置共8项，亮度在索引3：当前在索引2，再下移一格进入子页。 */
    tap(HAL_KEY3);
    frames(12);
    tap(HAL_KEY2);
    frames(8);
    save_shot("shot_push.bmp");
    frames(16);
    save_shot("shot_settings_stack.bmp");
    save_shot("shot_detail.bmp");
    {
        int bx, by, cx, cy, ch;
        list_card_at(0, 3, 0, &bx, &by, &cx, &cy, &ch);
        if (hal_host_pixel(bx, by + ch / 2) != UI_COL_MINT) {
            fprintf(stderr, "FAIL: focused card=%04x at %d,%d\n",
                    hal_host_pixel(bx, by + ch / 2), bx, by + ch / 2);
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
    tap(HAL_KEY1);
    frames(16);
    tap(HAL_KEY1);
    frames(16);
    {
        int dx, dy;
        dot_at(0, &dx, &dy);
        if (hal_host_pixel(dx, dy) == UI_COL_MINT) {
            return fail("clock shows dots");
        }
    }

    tap(HAL_KEY3);
    frames(20);
    {
        int dx, dy;
        dot_at(1, &dx, &dy);
        if (hal_host_pixel(dx, dy) != UI_COL_MINT) {
            return fail("did not reach sport page");
        }
    }
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
    tap(HAL_KEY2);   /* 零级动画采集完，进入一级柱状图（无顶部大标题） */
    frames(24);
    save_shot("shot_sport_l1.bmp");
    if (hal_host_pixel(UI_W / 2, header_h(0) - 1) == UI_COL_LINE) {
        return fail("sport level 1 still draws header line");
    }
    tap(HAL_KEY2);   /* 进入二级“运动的设置”，保留“运动”大标题 */
    frames(24);
    save_shot("shot_sport_l2.bmp");
    if (hal_host_pixel(UI_W / 2, header_h(0) - 1) != UI_COL_LINE) {
        return fail("sport level 2 header missing");
    }
    {
        int bx, by, ch;
        list_card_at(0, 2, 0, &bx, &by, 0, 0, &ch);
        if (hal_host_pixel(bx, by + ch / 2) != UI_COL_MINT) {
            return fail("sport level 2 card 0 not focused");
        }
    }
    tap(HAL_KEY1);   /* 退出运动二级菜单回到一级柱状图 */
    frames(24);
    tap(HAL_KEY3);   /* 一级柱状图按 KEY3 切到指南针，KEY2 触发校准 */
    frames(24);
    {
        imu9_state_t s9;
        tap(HAL_KEY2);
        frames(8);
        imu9_get(&s9);
        if (s9.mag_cal != 1) {
            return fail("sport compass cal start");
        }
        save_shot("shot_sport_compass_cal.bmp");
        tap(HAL_KEY2);
        frames(8);
        imu9_get(&s9);
        if (s9.mag_cal == 1) {
            return fail("sport compass cal stop");
        }
        save_shot("shot_sport_compass.bmp");
    }
    tap(HAL_KEY1);   /* 指南针按 KEY1 退回一级柱状图 */
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
        uint8_t count_pay = 5;
        uint8_t chunk[129];
        int c, px, py;
        int found_muted = 0;

        tap(HAL_KEY2);
        frames(24);
        if (region_sig(ix, iy, iw, ih) == icon) {
            return fail("msg list did not open");
        }
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) != UI_COL_LINE) {
            return fail("msg empty header missing");
        }
        /* 空态：居中浅灰“无消息”，且 KEY3 不得滚动空白列表触发顶栏收缩。 */
        save_shot("shot_msg_empty.bmp");
        for (py = header_h(0); py < UI_H - 8 && !found_muted; py++) {
            for (px = 8; px < UI_W - 8; px++) {
                if (hal_host_pixel(px, py) == UI_COL_MUTED_LIGHT) {
                    found_muted = 1;
                    break;
                }
            }
        }
        if (!found_muted) {
            return fail("msg empty muted-light text missing");
        }
        tap(HAL_KEY3);
        frames(20);
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) != UI_COL_LINE) {
            return fail("empty msg list scrolled on KEY3");
        }

        /* 模拟手机下发 0x25 (count=5) + 14 块 0x22 消息列表点阵，并测右边界裁剪。 */
        send_link(0x25, &count_pay, 1);
        frames(3);
        for (c = 0; c < 14; c++) {
            int row_b;
            chunk[0] = (uint8_t)c;
            for (row_b = 0; row_b < 8; row_b++) {
                int y_row = (c * 8 + row_b) % 22;
                int msg_i = (c * 8 + row_b) / 22;
                int b;
                for (b = 0; b < 16; b++) {
                    uint8_t v = 0;
                    if (y_row >= 5 && y_row <= 16) {
                        if (b == 1) {
                            v = 0x7E;
                        } else if (b >= 3 && b <= 9 + (msg_i % 3)) {
                            v = (y_row == 8 || y_row == 13) ? 0xFF : 0x55;
                        } else if (b == 15) {
                            v = 0xFF; /* 刻意放在 x=120..127 用于验证卡片右边界裁剪 */
                        }
                    }
                    chunk[1 + row_b * 16 + b] = v;
                }
            }
            send_link(0x22, chunk, 129);
            frames(3);
        }
        frames(8);
        {
            uint32_t list_ram = 0;
            ui_mem_get(&list_ram, NULL);
            if (list_ram <= used) {
                return fail("msg list did not allocate dynamic RAM");
            }
        }
        {
            int bx, by, ch;
            int cw = UI_MX(860);
            list_card_at(0, 5, 0, &bx, &by, 0, 0, &ch);
            if (hal_host_pixel(bx, by + ch / 2) != UI_COL_MINT) {
                return fail("msg card 0 not focused");
            }
            if (hal_host_pixel(bx + cw + 1, by + ch / 2) != UI_COL_BG) {
                return fail("msg mask spilled outside right border of card");
            }
        }
        /* 下移一格：顶栏缩起、4 张消息选项卡同屏可见。 */
        tap(HAL_KEY3);
        frames(24);
        if (hal_host_pixel(UI_W / 2, header_h(256) - 1) != UI_COL_LINE) {
            return fail("msg collapsed header missing");
        }
        {
            int bx3, by3, ch3;
            list_card_at(256, 5, 3, &bx3, &by3, 0, 0, &ch3);
            if (by3 + ch3 - 1 >= UI_H ||
                hal_host_pixel(bx3, by3 + ch3 / 2) != UI_COL_LINE) {
                return fail("msg 4th option card not visible");
            }
        }
        save_shot("shot_msg.bmp");

        /* 进入二级消息详情并下发 20 块 0x23 详情图。 */
        tap(HAL_KEY2);
        frames(8);
        for (c = 0; c < 20; c++) {
            int row_b;
            chunk[0] = (uint8_t)c;
            for (row_b = 0; row_b < 8; row_b++) {
                int y_row = c * 8 + row_b;
                int b;
                for (b = 0; b < 16; b++) {
                    uint8_t v = 0;
                    if (y_row >= 24 && y_row < 136 && b >= 2 && b <= 13) {
                        v = (y_row % 16 < 4) ? 0xFF : 0x81;
                    }
                    chunk[1 + row_b * 16 + b] = v;
                }
            }
            send_link(0x23, chunk, 129);
            frames(3);
        }
        frames(16);
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) == UI_COL_LINE ||
            hal_host_pixel(UI_W / 2, header_h(256) - 1) == UI_COL_LINE) {
            return fail("msg detail did not open");
        }
        save_shot("shot_msg_detail.bmp");
        {
            uint32_t detail_ram = 0;
            ui_mem_get(&detail_ram, NULL);
            if (detail_ram < used + 2560u) {
                return fail("msg detail did not allocate 2560B dynamic RAM");
            }
        }
        hal_host_set_keys(HAL_KEY2);
        frames(32);
        hal_host_set_keys(0);
        frames(40);
        /* 从详情退回列表：外置 32KB Flash 应立即恢复列表点阵缓存。 */
        if (!link_msg_list_ready()) {
            return fail("ext flash did not restore msg list on back");
        }
        hal_host_set_keys(HAL_KEY2);
        frames(32);
        hal_host_set_keys(0);
        frames(40);
        /* 回到消息图标首页：顶栏分割线消失，中心磁贴恢复。 */
        if (hal_host_pixel(UI_W / 2, header_h(0) - 1) == UI_COL_LINE ||
            hal_host_pixel(UI_W / 2, header_h(256) - 1) == UI_COL_LINE) {
            return fail("long enter did not close msg");
        }
        if (hal_host_pixel(UI_W / 2, UI_MY(100) + 4) == UI_COL_BG) {
            return fail("msg card stayed on icon");
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
