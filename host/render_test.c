#include "ui_internal.h"
#include "hal_host.h"
#include "imu9.h"
#include "settings.h"
#include "sport.h"
#include "compass3d_blob_test.h"
#include "about_asset_test.h"
#include "hdr_asset_test.h"
#include "pic_mods_test.h"

#include <stdio.h>
#include <string.h>

static uint16_t reference[UI_W * UI_H];
static uint8_t wire[UI_W * UI_H * 2 + 4096];
static const char *output_dir;
static int service_calls, preempt_sent;

#define REQUIRE(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %dx%d: %s (%s:%d)\n", UI_W, UI_H, \
                message, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static void step(int count) {
    while (count-- > 0) ui_frame();
}

static void key_tap(uint8_t key) {
    hal_host_set_keys(key);
    ui_frame();
    hal_host_set_keys(0);
    ui_frame();
}

static int save(const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", output_dir, name);
    REQUIRE(hal_host_save_bmp(path), "save render evidence");
    return 0;
}

static int same_frame(const char *label) {
    int x, y;
    for (y = 0; y < UI_H; y++) {
        for (x = 0; x < UI_W; x++) {
            uint16_t want = reference[y * UI_W + x];
            uint16_t got = hal_host_pixel(x, y);
            if (want != got) {
                fprintf(stderr, "FAIL %s at (%d,%d): optimized %04x full %04x\n",
                        label, x, y, want, got);
                save("render_mismatch_full.bmp");
                return 1;
            }
        }
    }
    return 0;
}

/* Compare the retained, optimized framebuffer against a forced redraw of
   the identical recorded commands, not a second/JS raster implementation. */
static int compare_full(const char *label) {
    ui_render_stats_t render;
    hal_host_display_stats_t display;
    hal_host_copy_frame(reference);
    ui_render_get_stats(&render);
    REQUIRE(!render.dropped, "commands unexpectedly dropped");
    ui_draw_invalidate();
    hal_host_display_reset_stats();
    ui_draw_flush();
    hal_host_display_get_stats(&display);
    REQUIRE(display.pixels == (uint32_t)UI_W * UI_H, "force-full pixel count");
    REQUIRE(!display.overwrites && !display.submit_while_busy && !display.invalid_regions,
            "force-full transfer contract");
    REQUIRE(!hal_display_busy(), "synchronous flush returned with borrowed buffer");
    return same_frame(label);
}

static void scene(int variant) {
    static const ui_svg_cmd_t svg[] = {
        {-6, -4, 20, 13, 0}, {20, -3, -5, 19, 0}, {7, 8, 0, 0, 5}
    };
    static const uint8_t mask[] = {0x01, 0x5F, 0xFF, 0xA0, 0x38, 0xCF, 0xF0, 0x70};
    int move = variant == 2 ? 23 : 0;
    int transform = variant == 6 ? 151 : (variant == 7 ? 359 : 256);
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
    ui_draw_fill(-8, -5, 24, 16, 0xF800, 255);
    if (variant == 5) ui_draw_fill(18, 19, 29, 24, 0xFFE0, 112);
    if (variant != 3) {
        ui_draw_round((int16_t)(11 + move), 13, 39, 31, 7,
                      variant == 1 ? 0x07E0 : 0x001F, variant == 4 ? 45 : 255);
    }
    if (variant != 5) ui_draw_fill(18, 19, 29, 24, 0xFFE0, 112);
    ui_draw_xform(transform, variant == 6 ? -9 : 4, variant == 7 ? -17 : 3);
    ui_draw_line(-11, 38, 56, variant == 8 ? 12 : 68, 0xFFFF, 180);
    ui_draw_line(45, 64, -7, 42, 0x07FF, 255);
    ui_draw_line(12, 48, 12, 48, 0xF81F, 127);
    ui_svg_draw(48, 43, 2, svg, 3, variant == 8 ? 0xF800 : 0xAFE5, 176);
    ui_draw_round(-4, 55, 31, 18, 5, 0xF81F, 110);
    ui_draw_ripple(12, 16, 44, 24, 6, (int16_t)(28 + (variant % 3)),
                   (int16_t)(8 + variant * 2), 0xCE79, 150);
    ui_draw_text(-3, 76, variant == 10 ? "changed" : "12:34", 0xFFFF, 210, 1);
    ui_mask_draw(15, 63, 4, 4, mask, 0xFFE0, 220);
    ui_draw_xform(256, 0, 0);
    ui_draw_fill(UI_W - 9, UI_H - 3, 15, 9, variant == 9 ? 0x07E0 : 0xF800, 255);
    ui_draw_fill(-20, UI_H + 1, 10, 3, 0xFFFF, 255);
}

/* These API checks deliberately avoid demo setup and renderer internals. */
static int test_core_drawing(void) {
    ui_render_stats_t render;
    hal_host_display_stats_t display;
    uint32_t used, budget, first = 0, after;
    int pass;
    for (pass = 0; pass < 2; pass++) {
        ui_core_init();
        ui_mem_get(&used, &budget);
        REQUIRE(used > 0 && used < budget && budget == UI_RAM_BUDGET,
                "core-only arena allocation");
        if (pass) REQUIRE(used == first, "core reinitialization leaked RAM");
        first = used;
        hal_host_display_reset_stats();
        ui_draw_begin();
        ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
        ui_draw_fill(2, 3, 12, 9, 0xF800, 255);
        ui_draw_round(20, 21, 24, 20, 6, 0x07E0, 255);
        ui_draw_line(4, 50, 30, 50, 0xFFFF, 255);
        ui_draw_ripple(60, 20, 40, 24, 6, 32, 30, 0xCE79, 255);
        hal_host_display_get_stats(&display);
        REQUIRE(display.calls == 0, "public primitives submitted before flush");
        ui_draw_flush();
        ui_render_get_stats(&render);
        hal_host_display_get_stats(&display);
        REQUIRE(render.items == 5 && render.dropped == 0,
                "core-only drawing lost commands or added demo commands");
        REQUIRE(render.pixels == (uint32_t)UI_W * UI_H &&
                render.bands == (UI_H + UI_BAND_H - 1) / UI_BAND_H,
                "core initialization did not invalidate the full display");
        REQUIRE(display.pixels == render.pixels && display.calls == render.bands &&
                display.completed == display.calls && !display.overwrites &&
                !display.submit_while_busy && !display.invalid_regions && !hal_display_busy(),
                "core-only flush transfer contract");
        REQUIRE(hal_host_pixel(0, 0) == 0x0863 &&
                hal_host_pixel(UI_W - 1, UI_H - 1) == 0x0863,
                "core-only full background missing");
        REQUIRE(hal_host_pixel(2, 3) == 0xF800 && hal_host_pixel(13, 11) == 0xF800 &&
                hal_host_pixel(14, 11) == 0x0863, "core-only fill bounds");
        REQUIRE(hal_host_pixel(32, 31) == 0x07E0 && hal_host_pixel(20, 21) == 0x0863,
                "core-only rounded rectangle missing or square");
        REQUIRE(hal_host_pixel(4, 50) == 0xFFFF && hal_host_pixel(17, 50) == 0xFFFF &&
                hal_host_pixel(30, 50) == 0xFFFF, "core-only line endpoints");
        REQUIRE(hal_host_pixel(80, 32) == 0xCE79 && hal_host_pixel(60, 20) == 0x0863 &&
                hal_host_pixel(60, 19) == 0x0863, "core-only clipped ripple bounds");
        hal_host_display_reset_stats();
        ui_draw_flush();
        hal_host_display_get_stats(&display);
        REQUIRE(display.pixels == (uint32_t)UI_W * UI_H && display.calls == render.bands,
                "settled content did not receive one full heal repaint");
        hal_host_display_reset_stats();
        ui_draw_flush();
        hal_host_display_get_stats(&display);
        REQUIRE(!display.calls && !display.pixels, "core-only unchanged flush transferred pixels");
        ui_mem_get(&after, NULL);
        REQUIRE(after == used, "public drawing allocated demo or per-frame RAM");
    }
    printf("Core-only public drawing / reinitialization PASS\n");
    return 0;
}

static int test_scaled_edges(void) {
    static const uint16_t image = 0xF800;
    static const uint8_t mask = 0xF0;
    int pass;
    ui_core_init();
    for (pass = 0; pass < 3; pass++) {
        ui_draw_begin();
        ui_draw_fill(0, 0, UI_W, UI_H, 0, 255);
        ui_draw_xform(64, 0, 0);
        if (pass < 2) ui_draw_line(40, 40, 40, 40, pass ? 0x07E0 : 0xFFFF, 255);
        ui_draw_flush();
        REQUIRE(hal_host_pixel(10, 10) == (pass == 2 ? 0 : pass ? 0x07E0 : 0xFFFF) &&
                hal_host_pixel(11, 10) == hal_host_pixel(10, 10),
                "downscaled two-pixel stroke left stale edge");
    }
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0, 255);
    ui_draw_xform(128, 0, 0);
    ui_image_draw(1, 1, 1, 1, 1, &image, 255);
    ui_mask_draw(5, 1, 1, 1, &mask, 0x07E0, 255);
    ui_draw_flush();
    REQUIRE(hal_host_pixel(0, 0) == 0xF800 && hal_host_pixel(2, 0) == 0x07E0,
            "subpixel bitmap origin discarded first source pixel");
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0, 255);
    ui_draw_line(-32768, -32768, 32767, 32767, 0xFFFF, 255);
    ui_draw_flush();
    REQUIRE(hal_host_pixel(20, 20) == 0xFFFF && hal_host_pixel(21, 20) == 0xFFFF,
            "extreme line coordinates overflowed clipped interpolation");
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0, 255);
    ui_draw_line(4, 20, 8, 20, 0xFFFF, 128);
    ui_draw_flush();
    REQUIRE(hal_host_pixel(5, 20) != hal_host_pixel(4, 20) &&
            hal_host_pixel(9, 20) == hal_host_pixel(4, 20),
            "horizontal stroke changed legacy overlapping alpha");
    printf("Scaled stroke damage / bitmap origin / extreme line / alpha PASS\n");
    return 0;
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int check_perf_reply(void) {
    ui_render_stats_t stats;
    uint32_t values[8];
    uint32_t ram_used = 0;
    uint32_t ram_total = 0;
    uint16_t cpu;
    uint8_t checksum = 0;
    int n, i;
    ui_render_get_stats(&stats);
    values[0] = stats.prepare_us;
    values[1] = stats.raster_us;
    values[2] = stats.wait_us;
    values[3] = stats.frame_us;
    values[4] = stats.pixels;
    values[5] = (uint32_t)stats.bands | ((uint32_t)stats.dropped << 16);
    hal_ram_get(&ram_used, &ram_total);
    cpu = hal_cpu_tenths();
    values[6] = (ram_used & 0xFFFFu) | ((uint32_t)cpu << 16);
    values[7] = (ram_total & 0xFFFFu) | ((uint32_t)ui_app_get_fps() << 16);
    hal_host_link_tx_take(wire, sizeof wire, &n);
    /* The binary reply is exactly 36 bytes; the existing text ACK follows it. */
    REQUIRE(n == 36 + 4, "performance reply size plus legacy ACK");
    REQUIRE(wire[0] == 0xA5 && wire[1] == 0x12 && wire[2] == 0x20,
            "performance reply header or payload length");
    for (i = 0; i < 36; i++) checksum ^= wire[i];
    REQUIRE(checksum == 0, "performance reply XOR (including header)");
    for (i = 0; i < 8; i++) {
        if (read_le32(wire + 3 + i * 4) != values[i]) {
            fprintf(stderr, "FAIL %dx%d: performance LE u32[%d]: got %lu expected %lu\n",
                    UI_W, UI_H, i, (unsigned long)read_le32(wire + 3 + i * 4),
                    (unsigned long)values[i]);
            return 1;
        }
    }
    REQUIRE(memcmp(wire + 36, "ok\r\n", 4) == 0, "performance ACK ordering");
    return 0;
}

static void perf_service(void) {
    /* Separate frame time from DMA wait time using only the host clock API. */
    hal_host_advance_us(37);
}

static int test_perf_request(void) {
    static const uint8_t request[] = {0xA5, 0x09, 0x00, 0xAC};
    static const uint8_t invalid[] = {
        0xA5, 0x09, 0x01, 0x00, 0xAD, /* Valid XOR, but forbidden payload. */
        0xA5, 0x09, 0x00, 0xAD        /* No payload, but invalid XOR. */
    };
    ui_render_stats_t stats;
    int pass, i, n;
    ui_core_init();
    link_init();
    hal_host_link_clear();
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
    ui_draw_flush();
    link_poll();
    link_ack_flush();
    hal_host_link_tx_take(wire, sizeof wire, &n);
    REQUIRE(n == 0, "unsolicited performance report");

    hal_host_set_async_delay_us(375);
    ui_set_service(perf_service);
    for (pass = 0; pass < 3; pass++) {
        hal_host_link_rx(request, sizeof request);
        link_poll();
        hal_host_link_tx_take(wire, sizeof wire, &n);
        REQUIRE(n == 0, "performance request replied before drawing/ACK flush");
        if (pass == 0) {
            ui_draw_begin();
            ui_draw_fill(0, 0, UI_W, UI_H, 0x001F, 255);
        } else if (pass == 1) {
            /* Intentional overflow exercises BOTH halves of bands|dropped<<16,
               including a dropped count greater than one byte. */
            ui_draw_begin();
            for (i = 0; i < 400; i++) ui_draw_fill(5, 6, 2, 2, (uint16_t)i, 255);
        } /* Pass 2 retains identical commands: settle once, then the latest frame must report zero pixels. */
        if (pass == 2) ui_draw_flush();
        ui_draw_flush();
        ui_render_get_stats(&stats);
        if (pass < 2) {
            REQUIRE(stats.pixels > 0 && stats.bands > 0 && stats.wait_us > 0 &&
                    stats.frame_us > stats.wait_us, "performance timing fixture not exercised");
        } else {
            REQUIRE(stats.pixels == 0 && stats.bands == 0 && stats.wait_us == 0,
                    "unchanged performance fixture transferred pixels");
        }
        if (pass == 0) REQUIRE(stats.dropped == 0, "normal performance fixture dropped commands");
        else REQUIRE(stats.items > 0 && stats.items < 400 && stats.dropped > 255 &&
                     stats.dropped == 400 - stats.items, "performance overflow fixture accounting");
        hal_host_link_tx_take(wire, sizeof wire, &n);
        REQUIRE(n == 0, "draw flush sent performance report before link_ack_flush");
        link_ack_flush();
        REQUIRE(!check_perf_reply(), "performance report differs from latest public statistics");
        link_poll();
        link_ack_flush();
        hal_host_link_tx_take(wire, sizeof wire, &n);
        REQUIRE(n == 0, "performance request was not one-shot");
    }
    ui_set_service(NULL);
    hal_host_set_async_delay_us(0);

    hal_host_link_rx(invalid, sizeof invalid);
    link_poll();
    link_ack_flush();
    hal_host_link_tx_take(wire, sizeof wire, &n);
    REQUIRE(n == 9 && memcmp(wire, "ok\r\nbad\r\n", 9) == 0,
            "nonempty or bad-XOR 09 request armed a performance report");
    for (pass = 0; pass < 2; pass++) {
        hal_host_link_rx(request, sizeof request);
        link_poll();
        hal_host_link_tx_take(wire, sizeof wire, &n);
        REQUIRE(n == 0, "pending performance request replied during poll");
        if (pass == 0) link_init();
        else ui_init();
        /* Do not clear the host RX/TX here: initialization must cancel the request. */
        ui_draw_begin();
        ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
        ui_draw_flush();
        link_ack_flush();
        hal_host_link_tx_take(wire, sizeof wire, &n);
        REQUIRE(n == 0, "reinitialization retained pending performance request or ACK");
        hal_host_link_rx(request, sizeof request);
        link_poll();
        ui_draw_flush();
        link_ack_flush();
        REQUIRE(!check_perf_reply(), "new performance request failed after reinitialization");
    }
    printf("09/12 performance protocol / latest stats / one-shot / reset PASS\n");
    return 0;
}

static int same_obj(const ui_obj_t *a, const ui_obj_t *b) {
    return a->x == b->x && a->y == b->y && a->opacity == b->opacity && a->scale == b->scale;
}

static int test_anim_stop(void) {
    static const struct {
        ui_anim_id_t id;
        ui_obj_t from, to;
    } cases[] = {
        {UI_ANIM_SLIDE_LEFT, {0, 29, 123, 77}, {-UI_W, 29, 123, 77}},
        {UI_ANIM_FADE_OUT, {37, 29, 255, 77}, {37, 29, 0, 77}},
        {UI_ANIM_ZOOM_IN, {37, 29, 123, 0}, {37, 29, 123, 255}}
    };
    ui_obj_t obj, midway, want;
    unsigned i;
    int finish;
    ui_core_init();
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t duration = ui_anim_duration_ms(cases[i].id);
        REQUIRE(duration > 1, "animation preset duration");
        for (finish = 0; finish <= 1; finish++) {
            obj = cases[i].from;
            ui_anim_play(cases[i].id, &obj);
            REQUIRE(ui_anim_busy(&obj) && same_obj(&obj, &cases[i].from),
                    "animation did not start at preset origin");
            hal_host_advance_us(duration / 2 * 1000u);
            ui_anim_tick(hal_tick_ms());
            REQUIRE(ui_anim_busy(&obj) && !same_obj(&obj, &cases[i].from) &&
                    !same_obj(&obj, &cases[i].to), "animation stop fixture not midway");
            midway = obj;
            want = finish ? cases[i].to : midway;
            ui_anim_stop(&obj, finish);
            REQUIRE(!ui_anim_busy(&obj) && same_obj(&obj, &want),
                    "animation stop must preserve current value or apply endpoint");
            hal_host_advance_us(duration * 1000u);
            ui_anim_tick(hal_tick_ms());
            ui_anim_stop(&obj, !finish);
            REQUIRE(!ui_anim_busy(&obj) && same_obj(&obj, &want),
                    "stopped animation changed on later tick or repeated stop");
        }
    }
    printf("Animation stop finish=0/1 (x / opacity / scale) PASS\n");
    return 0;
}

static int test_anim_slots(void) {
    ui_obj_t objs[5], midway[4], endpoints[4], rejected, redirected;
    uint32_t duration = ui_anim_duration_ms(UI_ANIM_SLIDE_RIGHT);
    int i;
    ui_core_init();
    REQUIRE(duration > 1 && duration == ui_anim_duration_ms(UI_ANIM_SLIDE_LEFT),
            "slot redirection fixture duration");
    for (i = 0; i < 5; i++) {
        objs[i].x = (int16_t)(-100 + i * 17);
        objs[i].y = (int16_t)(20 + i);
        objs[i].opacity = (uint8_t)(110 + i);
        objs[i].scale = (uint8_t)(210 + i);
        if (i < 4) {
            endpoints[i] = objs[i];
            endpoints[i].x = (int16_t)(100 + i * 23);
            ui_anim_play_range(UI_ANIM_SLIDE_RIGHT, &objs[i], objs[i].x, endpoints[i].x);
            REQUIRE(ui_anim_busy(&objs[i]), "four animation slots not available");
        }
    }
    hal_host_advance_us(duration / 2 * 1000u);
    ui_anim_tick(hal_tick_ms());
    for (i = 0; i < 4; i++) {
        REQUIRE(ui_anim_busy(&objs[i]) && objs[i].x > -100 + i * 17 &&
                objs[i].x < endpoints[i].x, "full animation slots did not advance");
        midway[i] = objs[i];
    }
    rejected = objs[4];
    ui_anim_play_range(UI_ANIM_FADE_OUT, &objs[4], 255, 0);
    REQUIRE(!ui_anim_busy(&objs[4]) && same_obj(&objs[4], &rejected),
            "full slots accepted or mutated a fifth object");
    for (i = 0; i < 4; i++)
        REQUIRE(ui_anim_busy(&objs[i]) && same_obj(&objs[i], &midway[i]),
                "fifth animation displaced a running slot");

    /* Redirect an existing object while all four slots are still occupied. */
    redirected = midway[2];
    redirected.x = -123;
    ui_anim_play_range(UI_ANIM_SLIDE_LEFT, &objs[2], objs[2].x, redirected.x);
    for (i = 0; i < 4; i++)
        REQUIRE(ui_anim_busy(&objs[i]) && same_obj(&objs[i], &midway[i]),
                "same-object redirection jumped or displaced another slot");
    hal_host_advance_us((duration - duration / 2) * 1000u);
    ui_anim_tick(hal_tick_ms());
    for (i = 0; i < 4; i++) {
        if (i == 2) {
            REQUIRE(ui_anim_busy(&objs[i]) && objs[i].x < midway[i].x &&
                    objs[i].x > redirected.x, "redirection did not restart from current value");
        } else {
            REQUIRE(!ui_anim_busy(&objs[i]) && same_obj(&objs[i], &endpoints[i]),
                    "full-slot rejection/redirection changed an original animation");
        }
    }
    REQUIRE(!ui_anim_busy(&objs[4]) && same_obj(&objs[4], &rejected),
            "rejected fifth animation was queued for later");
    hal_host_advance_us(duration / 2 * 1000u);
    ui_anim_tick(hal_tick_ms());
    REQUIRE(!ui_anim_busy(&objs[2]) && same_obj(&objs[2], &redirected),
            "redirected animation finished at old endpoint");
    ui_anim_play_range(UI_ANIM_SLIDE_RIGHT, &objs[4], objs[4].x, 101);
    REQUIRE(ui_anim_busy(&objs[4]), "completed slots were not reusable");
    ui_anim_stop(&objs[4], 1);
    rejected.x = 101;
    REQUIRE(!ui_anim_busy(&objs[4]) && same_obj(&objs[4], &rejected),
            "range animation stop did not apply custom endpoint");
    /* 防止关键操作（如 I2C EEPROM 阻塞几毫秒）后以旧 now 调用 ui_anim_tick 导致无符号下溢瞬跳结束 */
    objs[4].scale = 255;
    ui_anim_play_range(UI_ANIM_ZOOM_OUT, &objs[4], 255, 0);
    ui_anim_tick(hal_tick_ms() - 12u);
    REQUIRE(ui_anim_busy(&objs[4]) && objs[4].scale == 255,
            "ui_anim_tick underflowed on stale timestamp before t0");
    ui_anim_stop(&objs[4], 0);
    printf("Animation four-slot rejection / same-object redirection / reuse PASS\n");
    return 0;
}

static int test_arena(void) {
    uint32_t used, total, first, later;
    unsigned pass, align;
    for (pass = 0; pass < 32; pass++) {
        ui_mem_init();
        for (align = 1; align <= 8; align *= 2) {
            unsigned char *p = ui_mem_alloc(13, align);
            int i;
            REQUIRE(p && (uintptr_t)p % align == 0, "arena pointer alignment");
            for (i = 0; i < 13; i++) REQUIRE(p[i] == 0, "arena zero initialization");
            memset(p, 0xA5, 13);
        }
        ui_mem_get(&used, &total);
        REQUIRE(!ui_mem_alloc(UINT32_MAX, 8), "arena oversized allocation accepted");
        ui_mem_get(&later, NULL);
        REQUIRE(later == used, "failed allocation consumed arena");
        ui_mem_init();
        REQUIRE(ui_mem_alloc(UI_RAM_BUDGET, 1) != NULL, "arena exact capacity");
        REQUIRE(!ui_mem_alloc(1, 1), "arena overflow accepted");
        /* Dynamic block free, out-of-order coalescing, and gap reuse. */
        ui_mem_init();
        {
            void *b0 = ui_mem_alloc(512, 4);
            void *b1 = ui_mem_alloc(1024, 4);
            void *b2 = ui_mem_alloc(1024, 4);
            void *b3 = ui_mem_alloc(512, 4);
            void *merged;
            REQUIRE(b0 && b1 && b2 && b3, "dynamic block allocs");
            ui_mem_free(b2);
            ui_mem_free(b1);
            merged = ui_mem_alloc(2048, 4);
            REQUIRE(merged == b1, "coalesced middle gap reuse");
            ui_mem_free(b0);
            ui_mem_free(merged);
            ui_mem_free(b3);
            ui_mem_get(&later, NULL);
            REQUIRE(later == 0 && ui_mem_avail() == UI_RAM_BUDGET,
                    "all freed blocks returned to arena");
        }
    }
    ui_init();
    ui_mem_get(&first, &total);
    REQUIRE(total == 10560u + 4u * UI_W * UI_BAND_H, "declared RAM budget");
    REQUIRE(UI_BAND_H == 4, "four-row DMA buffers");
    REQUIRE(first >= 4u * UI_W * UI_BAND_H && first < total, "two buffers fit arena");
    for (pass = 0; pass < 32; pass++) {
        ui_settings_set_debug(1);
        ui_init();
        ui_mem_get(&later, NULL);
        REQUIRE(first == later, "reinitialization leaked arena");
        REQUIRE(!ui_settings_get_debug(), "reinitialization retained app state");
    }
    printf("Arena PASS: %lu / %lu bytes\n", (unsigned long)first, (unsigned long)total);
    return 0;
}

static int test_clock_and_detector(void) {
    uint16_t px[4] = {1, 2, 3, 4};
    hal_host_display_stats_t stats;
    hal_host_set_time_us(1234567);
    REQUIRE(hal_tick_us() == 1234567 && hal_tick_ms() == 1234, "virtual clock units");
    hal_host_advance_us(433);
    REQUIRE(hal_tick_us() == 1235000 && hal_tick_ms() == 1235, "virtual clock advance");
    hal_host_set_frame_step_us(0);
    hal_frame_pace();
    REQUIRE(hal_tick_us() == 1235000, "frozen virtual pacing");
    hal_host_set_async_delay_us(400);
    hal_host_display_reset_stats();
    hal_display_flush(0, 0, 2, 2, px);
    REQUIRE(hal_display_busy(), "simulated DMA did not stay busy");
    REQUIRE(hal_host_pixel(0, 0) == 0, "DMA copied at submission instead of completion");
    px[0] = 0xCAFE; /* Deliberately prove the detector observes borrowed memory. */
    while (hal_display_busy()) hal_idle();
    hal_host_display_get_stats(&stats);
    REQUIRE(stats.overwrites == 1, "DMA overwrite detector did not fire");
    REQUIRE(hal_host_pixel(0, 0) == 0xCAFE, "DMA secretly copied safety snapshot");
    /* Deadline arithmetic also works when the microsecond counter wraps. */
    hal_host_set_time_us(0xFFFFFF80ull);
    hal_host_display_reset_stats();
    hal_display_flush(0, 0, 2, 2, px);
    REQUIRE(hal_display_busy(), "DMA wraparound completed too early");
    while (hal_display_busy()) hal_idle();
    hal_host_display_get_stats(&stats);
    REQUIRE(stats.completed == 1 && !stats.overwrites, "DMA wraparound completion");
    hal_host_set_async_delay_us(0);
    hal_host_set_time_us(0);
    hal_host_set_frame_step_us(16000);
    hal_host_display_reset_stats();
    return 0;
}

static int test_dirty(void) {
    int variant;
    hal_host_display_stats_t stats;
    ui_render_stats_t render;
    ui_init();
    for (variant = 0; variant <= 10; variant++) {
        char label[48];
        snprintf(label, sizeof label, "scene variant %d", variant);
        scene(variant);
        hal_host_display_reset_stats();
        ui_draw_flush();
        REQUIRE(!compare_full(label), "optimized scene differs from force-full");
    }
    REQUIRE(!save("render_primitives.bmp"), "primitive screenshot");
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
    ui_draw_fill(11, UI_BAND_H + 1, 7, 2, 0xF800, 255);
    ui_draw_flush();
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
    ui_draw_fill(11, UI_BAND_H + 1, 7, 2, 0x07E0, 255);
    hal_host_display_reset_stats();
    ui_draw_flush();
    hal_host_display_get_stats(&stats);
    ui_render_get_stats(&render);
    REQUIRE(stats.pixels == (uint32_t)UI_W * UI_BAND_H && stats.calls == 1,
            "small change did not use one full-width dirty band");
    REQUIRE(stats.min_x == 0 && stats.max_x == UI_W && stats.max_y < UI_H,
            "dirty band must span full width without covering the screen");
    REQUIRE(stats.pixels == render.pixels && stats.calls == render.bands,
            "renderer statistics disagree with HAL");
    REQUIRE(!compare_full("full-width changed fill"), "full-width fill differs");
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
    ui_draw_flush();
    REQUIRE(!compare_full("removed fill"), "removed fill trails");
    hal_host_display_reset_stats();
    ui_draw_flush();
    hal_host_display_get_stats(&stats);
    REQUIRE(stats.pixels == (uint32_t)UI_W * UI_H,
            "settle heal did not repaint the full screen once");
    hal_host_display_reset_stats();
    ui_draw_flush();
    hal_host_display_get_stats(&stats);
    REQUIRE(stats.calls == 0 && stats.pixels == 0, "unchanged commands transmit pixels");
    /* In-place pixels must remain dynamic even when pointer/commands match. */
    {
        static uint16_t image[15];
        int i;
        for (i = 0; i < 15; i++) image[i] = 0xF800;
        ui_draw_begin();
        ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
        ui_image_draw(21, UI_H / 2, 5, 3, 3, image, 255);
        ui_draw_flush();
        image[0] = 0x07E0;
        image[14] = 0x001F;
        hal_host_display_reset_stats();
        ui_draw_flush();
        hal_host_display_get_stats(&stats);
        REQUIRE(stats.pixels > 0, "mutable image reference was cached as static");
        REQUIRE(hal_host_pixel(21, UI_H / 2) == 0x07E0, "mutable image did not update");
        REQUIRE(!compare_full("mutable image"), "mutable image differs");
        ui_draw_begin();
        ui_draw_fill(0, 0, UI_W, UI_H, 0x0863, 255);
        ui_draw_xform(173, -7, -5);
        ui_image_draw(-2, -1, 5, 3, 3, image, 124);
        ui_draw_flush();
        REQUIRE(!compare_full("scaled clipped transparent image"), "image clipping differs");
    }
    printf("Dirty/full equivalence PASS (including last %d rows)\n", UI_H % UI_BAND_H);
    return 0;
}

/* FPS 贴近肉眼观感：统计局部脏区刷新与全屏传输，静止后自然衰减至 0 且不受调试角标自激。 */
static int test_full_frame_fps(void) {
    ui_render_stats_t render;
    int i, saw_partial;

    /* 1) Continuous full-screen redraw at 16ms (~62 fps). */
    hal_host_set_async_delay_us(0);
    hal_host_set_frame_step_us(16000);
    ui_init();
    for (i = 0; i < 20; i++) {
        ui_draw_invalidate();
        ui_frame();
    }
    REQUIRE(ui_app_get_fps() >= 58 && ui_app_get_fps() <= 64,
            "fast lock-on full-frame FPS outside 58..64");
    for (i = 0; i < 50; i++) {
        ui_draw_invalidate();
        ui_frame();
    }
    REQUIRE(ui_app_get_fps() >= 58 && ui_app_get_fps() <= 64,
            "steady full-frame FPS outside 58..64");

    /* 2) Slow full-screen DMA (32ms frame step -> ~31 fps). */
    hal_host_set_frame_step_us(32000);
    for (i = 0; i < 60; i++) {
        ui_draw_invalidate();
        ui_frame();
    }
    REQUIRE(ui_app_get_fps() >= 28 && ui_app_get_fps() <= 35,
            "slow 32ms full-frame FPS did not converge near 31 fps");

    /* 3) Active partial dirty-band animation (Settings -> Brightness detail @ 16ms). */
    hal_host_set_frame_step_us(16000);
    ui_init();
    ui_nav_goto(UI_PAGE_SETTINGS);
    ui_frame();
    key_tap(HAL_KEY2);
    step(24); /* Finish zoom-in so Settings list is settled at top level. */
    key_tap(HAL_KEY3);
    step(2);
    key_tap(HAL_KEY3);
    step(2);
    key_tap(HAL_KEY3);
    step(24);
    key_tap(HAL_KEY2);
    step(24); /* Settle on Settings -> Brightness detail (3 items, bottom margin clean). */
    step(95); /* Let FPS decay completely to 0 before starting animation burst. */
    REQUIRE(ui_app_get_fps() == 0,
            "settled static Settings menu did not decay to 0 fps");

    saw_partial = 0;
    key_tap(HAL_KEY3);
    for (i = 0; i < 24; i++) {
        if (i == 8) {
            key_tap(HAL_KEY1);
        } else if (i == 17) {
            key_tap(HAL_KEY3);
        } else {
            ui_frame();
        }
        ui_render_get_stats(&render);
        if (render.pixels > 0 && render.pixels < (uint32_t)UI_W * UI_H) {
            saw_partial++;
        }
    }
    REQUIRE(saw_partial >= 16,
            "Settings scroll did not exercise partial dirty-band updates");
    REQUIRE(ui_app_get_fps() >= 50 && ui_app_get_fps() <= 64,
            "active partial dirty-band scroll FPS outside 50..64");

    /* 4) Truly static settled UI (1.5s after scroll finishes -> 0 fps). */
    step(95);
    ui_render_get_stats(&render);
    REQUIRE(render.pixels == 0, "settled Settings still transmitted pixels");
    REQUIRE(ui_app_get_fps() <= 5 && ui_app_get_fps() == 0,
            "idle CPU loop inflated static Settings FPS");

    /* 5) Debug badge enabled while static -> decays cleanly to 0 without self-feedback. */
    ui_settings_set_debug(1);
    step(16); /* Finish badge fade-in animation. */
    key_tap(HAL_KEY1);
    for (i = 0; i < 20; i++) {
        if (i == 8) {
            key_tap(HAL_KEY3);
        } else if (i == 16) {
            key_tap(HAL_KEY1);
        } else {
            ui_frame();
        }
    }
    REQUIRE(ui_app_get_fps() >= 50 && ui_app_get_fps() <= 64,
            "partial scroll with debug badge enabled outside 50..64");
    step(100); /* 1.6s static with debug badge visible */
    ui_render_get_stats(&render);
    REQUIRE(render.pixels == 0,
            "static UI with debug badge kept redrawing");
    REQUIRE(ui_app_get_fps() == 0,
            "debug badge self-feedback loop prevented decay to 0 fps");

    printf("Perceptual partial + full-frame FPS PASS\n");
    return 0;
}

static void count_service(void) {
    service_calls++;
}

static void preempt_service(void) {
    hal_host_display_stats_t stats;
    service_calls++;
    hal_host_display_get_stats(&stats);
    if (stats.calls && !preempt_sent) {
        preempt_sent = 1;
        ui_output_preempt();
    }
}

static int test_async_and_preempt(void) {
    hal_host_display_stats_t stats;
    ui_render_stats_t render;
    uint32_t start;
    int i;
    static const uint16_t overlay[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xFFFF, 0xF81F};
    hal_host_set_async_delay_us(375);
    service_calls = 0;
    ui_set_service(count_service);
    for (i = 0; i < 8; i++) {
        scene(i);
        ui_draw_invalidate();
        hal_host_display_reset_stats();
        start = hal_tick_us();
        ui_draw_flush();
        ui_render_get_stats(&render);
        hal_host_display_get_stats(&stats);
        REQUIRE(!hal_display_busy(), "flush returned before final DMA completion");
        REQUIRE(!stats.overwrites && !stats.submit_while_busy && !stats.invalid_regions,
                "delayed DMA corrupted borrowed band");
        REQUIRE(stats.completed == stats.calls && stats.calls > 2, "DMA transfers incomplete");
        REQUIRE(stats.pointer_changes > 0, "renderer did not alternate DMA buffers");
        REQUIRE(stats.max_h <= UI_BAND_H, "DMA exceeded four-row band");
        REQUIRE(hal_tick_us() - start >= stats.calls * 375u, "DMA virtual duration");
        REQUIRE(render.wait_us > 0 && render.frame_us >= render.wait_us,
                "DMA wait timing not recorded");
        REQUIRE(!compare_full("delayed DMA"), "delayed DMA framebuffer differs");
    }
    REQUIRE(service_calls > 0, "renderer starved service callback");
    preempt_sent = service_calls = 0;
    ui_set_service(preempt_service);
    scene(9);
    ui_draw_invalidate();
    hal_host_display_reset_stats();
    ui_draw_flush();
    hal_host_display_get_stats(&stats);
    REQUIRE(preempt_sent, "preemption was not exercised during DMA");
    REQUIRE(stats.pixels > 0 && stats.pixels < (uint32_t)UI_W * UI_H,
            "preemption did not stop remaining bands");
    REQUIRE(!hal_display_busy() && !stats.overwrites && !stats.submit_while_busy,
            "preemption left borrowed memory active");
    ui_set_service(NULL);
    scene(9);
    ui_draw_flush();
    REQUIRE(!compare_full("preempt recovery"), "preempted regions not restored");
    ui_gif_post(3, 5, 3, 2, overlay);
    ui_draw_flush();
    REQUIRE(!hal_display_busy(), "overlay flush returned while busy");
    for (i = 0; i < 6; i++)
        REQUIRE(hal_host_pixel(3 + i % 3, 5 + i / 3) == overlay[i], "external overlay missing");
    REQUIRE(!save("render_external_overlay.bmp"), "overlay screenshot");
    scene(9);
    ui_draw_flush();
    REQUIRE(!compare_full("external overlay restore"), "external overlay left stale pixels");
    hal_host_display_get_stats(&stats);
    REQUIRE(!stats.overwrites && !stats.submit_while_busy, "overlay DMA ownership");
    hal_host_set_async_delay_us(0);
    printf("Async DMA / service / preemption / overlay PASS\n");
    return 0;
}

static int test_face_snapshot_and_keys(void) {
    hal_host_display_stats_t stats;
    ui_render_stats_t render;
    const hal_clock_t clock = {2026, 11, 11, 8, 30, 0};
    int n, i;
    uint32_t payload;
    uint8_t checksum;
    ui_init();
    hal_clock_set(&clock);
    hal_host_set_keys(0);
    step(32);
    hal_host_set_frame_step_us(0);
    ui_frame();
    ui_frame(); /* consume the one settle heal repaint before measuring idle */
    hal_host_display_reset_stats();
    step(5);
    hal_host_display_get_stats(&stats);
    ui_render_get_stats(&render);
    REQUIRE(stats.pixels == 0 && stats.calls == 0 && render.pixels == 0,
            "settled unchanged face transmits pixels");
    REQUIRE(!save("shot_face_settled.bmp"), "face screenshot");
    hal_host_copy_frame(reference);
    hal_host_link_clear();
    link_snap_request();
    ui_frame();
    hal_host_link_tx_take(wire, sizeof wire, &n);
    REQUIRE(n == UI_W * UI_H * 2 + 11, "snapshot missing full unchanged frame payload");
    REQUIRE(wire[0] == 0xA5 && wire[1] == 0x11 && wire[2] == 0xFF, "snapshot header");
    payload = wire[3] | ((uint32_t)wire[4] << 8) | ((uint32_t)wire[5] << 16);
    REQUIRE(payload == 4u + UI_W * UI_H * 2u, "snapshot declared payload length");
    REQUIRE((wire[6] | (wire[7] << 8)) == UI_W &&
            (wire[8] | (wire[9] << 8)) == UI_H, "snapshot dimensions");
    checksum = 0;
    for (i = 0; i < n; i++) checksum ^= wire[i];
    REQUIRE(checksum == 0, "snapshot checksum");
    for (i = 0; i < UI_W * UI_H; i++)
        REQUIRE(((wire[10 + i * 2] << 8) | wire[11 + i * 2]) == reference[i],
                "snapshot pixel ordering or unchanged framebuffer mismatch");
    REQUIRE(!same_frame("snapshot retained framebuffer"), "snapshot altered screen");
    hal_host_set_frame_step_us(16000);
    key_tap(HAL_KEY3);
    key_tap(HAL_KEY3);
    key_tap(HAL_KEY1);
    key_tap(HAL_KEY3);
    key_tap(HAL_KEY1);
    key_tap(HAL_KEY3);
    step(24);
    hal_host_copy_frame(reference);
    hal_host_set_frame_step_us(0);
    ui_nav_goto(UI_PAGE_MSG);
    ui_frame();
    REQUIRE(!same_frame("rapid left/right reversals"), "rapid keys lost destination");
    REQUIRE(!save("shot_rapid_reversal.bmp"), "key reversal screenshot");
    ui_init();
    hal_host_set_frame_step_us(16000);
    hal_host_set_keys(HAL_KEY2);
    step(32);
    hal_host_set_keys(0);
    step(2);
    REQUIRE(!link_media_playing(), "long enter entered dial edit instead of play");
    key_tap(HAL_KEY2); /* confirm/exit dial edit mode without toggling play */
    step(2);
    REQUIRE(!link_media_playing(), "confirming dial edit triggered media toggle");
    key_tap(HAL_KEY2);
    REQUIRE(link_media_playing(), "short enter after long press was lost");
    printf("Unchanged face / full snapshot / rapid reversal / long press PASS\n");
    return 0;
}

static int test_ext_flash_and_stream(void) {
    uint8_t rgb_row[16];
    uint8_t read_back[16];
    int i;

    REQUIRE(hal_ext_flash_present(), "simulated 32KB ext flash online");
    REQUIRE(!hal_ext_flash_write(HAL_EXT_FLASH_SIZE - 4u, rgb_row, 8u),
            "ext flash out-of-bounds write rejected");
    for (i = 0; i < 8; i++) {
        uint16_t c = (i & 1) ? UI_COL_MINT : UI_COL_AMBER;
        rgb_row[i * 2] = (uint8_t)(c & 0xFFu);
        rgb_row[i * 2 + 1] = (uint8_t)(c >> 8);
    }
    for (i = 0; i < 8; i++) {
        REQUIRE(hal_ext_flash_write(LINK_EXT_OFF_IMG + (uint32_t)i * 16u, rgb_row, 16u),
                "ext flash row write");
    }
    REQUIRE(hal_ext_flash_read(LINK_EXT_OFF_IMG, read_back, 16u) &&
            memcmp(read_back, rgb_row, 16u) == 0, "ext flash read verify");

    ui_core_init();
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, UI_COL_BG, 255);
    ui_ext_image_draw(10, 12, 8, 8, LINK_EXT_OFF_IMG, UI_EXT_FMT_RGB565, 0, 255);
    ui_draw_flush();
    REQUIRE(hal_host_pixel(10, 14) == UI_COL_AMBER &&
            hal_host_pixel(11, 14) == UI_COL_MINT &&
            hal_host_pixel(17, 19) == UI_COL_MINT,
            "streaming RGB565 from 32KB ext flash across bands");
    ui_init();
    printf("External 32KB Flash & streaming image PASS\n");
    return 0;
}

static int angle_diff_deg(int a, int b) {
    int d = (a - b) % 360;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d < 0 ? -d : d;
}

static void push_link_frame(uint8_t cmd, const uint8_t *pay, int len) {
    uint8_t frame[180];
    uint8_t xo = 0;
    int i;
    frame[0] = 0xA5;
    frame[1] = cmd;
    frame[2] = (uint8_t)len;
    for (i = 0; i < len; i++) {
        frame[3 + i] = pay[i];
    }
    for (i = 0; i < 3 + len; i++) {
        xo ^= frame[i];
    }
    frame[3 + len] = xo;
    hal_host_link_rx(frame, 4 + len);
}

static int test_persist_and_compressed_dials(void) {
    uint8_t blob[512];
    uint8_t pay[140];
    const uint8_t *rx_cfg;
    uint16_t rx_n = 0;
    int tx_n = 0;
    int y, pos, total, chunks, c;
    const settings_t *cs;
    sport_stats_t sp;
    imu9_state_t s9;
    hal_clock_t clk;

    /* 1. 测试 PAL4_RLE (fmt=4) 8x8 编码写外置 Flash 并按条带无损光栅化 */
    memset(blob, 0, sizeof blob);
    blob[0] = (uint8_t)(UI_COL_BG & 0xFFu); blob[1] = (uint8_t)(UI_COL_BG >> 8);
    blob[2] = (uint8_t)(UI_COL_AMBER & 0xFFu); blob[3] = (uint8_t)(UI_COL_AMBER >> 8);
    blob[4] = (uint8_t)(UI_COL_MINT & 0xFFu); blob[5] = (uint8_t)(UI_COL_MINT >> 8);
    pos = 32 + 8 * 2; /* header(32) + row_end[8](16) = 48 */
    for (y = 0; y < 8; y++) {
        if ((y & 1) == 0) {
            /* Repeat 8 px of color index 1 (AMBER) */
            blob[pos++] = (uint8_t)(0x80u | (8 - 2));
            blob[pos++] = 1u;
        } else {
            /* Literal 8 px alternating color index 2 / 1 (MINT/AMBER) */
            blob[pos++] = 7u; /* lit_len - 1 = 7 -> 8 px -> 4 bytes */
            blob[pos++] = 0x21u; blob[pos++] = 0x21u;
            blob[pos++] = 0x21u; blob[pos++] = 0x21u;
        }
        rx_n = (uint16_t)(pos - (32 + 16));
        blob[32 + y * 2] = (uint8_t)(rx_n & 0xFFu);
        blob[32 + y * 2 + 1] = (uint8_t)(rx_n >> 8);
    }
    REQUIRE(hal_ext_flash_write(LINK_EXT_OFF_IMG, blob, (uint32_t)pos),
            "write PAL4_RLE test blob");
    ui_core_init();
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, UI_COL_BG, 255);
    ui_ext_image_draw(20, 22, 8, 8, LINK_EXT_OFF_IMG, UI_EXT_FMT_PAL4_RLE, 0, 255);
    ui_draw_flush();
    REQUIRE(hal_host_pixel(20, 22) == UI_COL_AMBER &&
            hal_host_pixel(27, 22) == UI_COL_AMBER &&
            hal_host_pixel(20, 23) == UI_COL_MINT &&
            hal_host_pixel(21, 23) == UI_COL_AMBER,
            "PAL4_RLE compressed decoding pixel exact");

    /* 2. 测试 RGB565_RLE (fmt=5) 8x4 真彩压缩流 */
    memset(blob, 0, sizeof blob);
    pos = 4 * 2; /* row_end[4] = 8 bytes */
    for (y = 0; y < 4; y++) {
        uint16_t col = (y == 2) ? 0xF800u : 0x07E0u;
        blob[pos++] = (uint8_t)(0x80u | (8 - 2));
        blob[pos++] = (uint8_t)(col & 0xFFu);
        blob[pos++] = (uint8_t)(col >> 8);
        rx_n = (uint16_t)(pos - 8);
        blob[y * 2] = (uint8_t)(rx_n & 0xFFu);
        blob[y * 2 + 1] = (uint8_t)(rx_n >> 8);
    }
    REQUIRE(hal_ext_flash_write(LINK_EXT_OFF_IMG, blob, (uint32_t)pos),
            "write RGB565_RLE test blob");
    ui_draw_begin();
    ui_draw_fill(0, 0, UI_W, UI_H, UI_COL_BG, 255);
    ui_ext_image_draw(30, 30, 8, 4, LINK_EXT_OFF_IMG, UI_EXT_FMT_RGB565_RLE, 0, 255);
    ui_draw_flush();
    REQUIRE(hal_host_pixel(33, 30) == 0x07E0u &&
            hal_host_pixel(35, 32) == 0xF800u,
            "RGB565_RLE compressed decoding pixel exact");

    /* 3. 测试 0x2A App 最高优先级设置下发 -> AT24C256 持久化 -> 掉电冷启动恢复 */
    ui_init();
    hal_host_link_clear();
    hal_host_set_frame_step_us(16000);
    /* 设置日期时间 + 写入一组磁强计校准偏置 */
    clk.year = 2026; clk.month = 11; clk.day = 16;
    clk.hour = 10; clk.minute = 30; clk.second = 0;
    hal_clock_set(&clk);
    settings_record_clock(&clk);
    imu9_load_mag_cal(-45, 75, -120);

    pay[0] = 0x03u; /* save_to_i2c=1, app_override=1, use_24h=0, debug=0 */
    pay[1] = 0u;    /* raise_wrist=0 */
    pay[2] = 0u;    /* key_beep=0 */
    pay[3] = 2u;    /* brightness=2 */
    pay[4] = 2u;    /* led_mode=2 */
    pay[5] = 3u;    /* screen_off=3 */
    pay[6] = (uint8_t)(12345u & 0xFFu);
    pay[7] = (uint8_t)(12345u >> 8);
    pay[8] = 0u; pay[9] = 0u;
    pay[10] = UI_DIAL_ANALOG;
    pay[11] = UI_DIAL_COMP_HDG;
    pay[12] = UI_DIAL_COMP_BAT;
    pay[13] = UI_DIAL_FLAG_SECONDS | UI_DIAL_FLAG_SCRIM;
    pay[14] = (uint8_t)(UI_COL_MINT & 0xFFu); pay[15] = (uint8_t)(UI_COL_MINT >> 8);
    pay[16] = (uint8_t)(UI_COL_AMBER & 0xFFu); pay[17] = (uint8_t)(UI_COL_AMBER >> 8);
    pay[18] = 0x08u; pay[19] = 0x00u;
    push_link_frame(0x2Au, pay, 20);
    step(2);
    hal_host_link_tx_take(wire, sizeof wire, &tx_n);
    REQUIRE(tx_n >= 40, "0x2A returned 0x1A + 0x2C");

    /* 模拟彻底掉电冷启动 (仅保留 sim_ext_flash AT24C256 芯片数据！) */
    ui_init();
    cs = settings_get();
    sport_get(&sp);
    imu9_get(&s9);
    hal_clock_get(&clk);
    REQUIRE(cs->cfg_loaded == 1u && cs->cfg_source == UI_CFG_PRIO_APP, "cfg_loaded/prio");
    REQUIRE(cs->brightness == 2u && cs->led_mode == 2u && cs->screen_off == 3u && cs->use_24h == 0u, "settings fields");
    REQUIRE(cs->dial_id == UI_DIAL_ANALOG && cs->dial_top == UI_DIAL_COMP_HDG && cs->dial_bot == UI_DIAL_COMP_BAT, "dial config");
    REQUIRE(sp.goal == 12345u, "step goal");
    REQUIRE(s9.mag_cal == 2u && s9.mox == -45 && s9.moy == 75 && s9.moz == -120, "mag cal");
    REQUIRE(clk.day == 16, "clock day restore");

    /* 保存指针表盘截图 */
    step(4);
    REQUIRE(!save("shot_dial_analog.bmp"), "analog dial shot");

    /* 验证长按 KEY2 缩小表盘进入编辑选择模式（左右显示 < > 提示），KEY1/KEY3 滑移切换表盘，短按 KEY2 放大选中并落盘 I2C */
    hal_host_set_keys(HAL_KEY2);
    step(32);
    hal_host_set_keys(0);
    step(22);
    REQUIRE(!save("shot_dial_edit.bmp"), "dial edit shot");
    key_tap(HAL_KEY3);
    step(5);
    REQUIRE(!save("shot_dial_slide.bmp"), "dial slide mid-anim shot");
    step(16);
    key_tap(HAL_KEY2);
    step(6);
    REQUIRE(!save("shot_dial_zoom_cover.bmp"), "dial confirm zoom occlusion shot");
    step(16);
    REQUIRE(settings_get()->dial_id == UI_DIAL_IMU, "dial edit KEY3+KEY2 selected IMU dial");
    REQUIRE(!save("shot_dial_imu.bmp"), "imu dial shot");

    /* 4. 上传完整 128x160 PAL4_RLE 压缩相册底图并通过 0x28(len=8)+0x29 设为持久化相册表盘 */
    {
        static uint8_t img128[32 + UI_H * 2 + UI_H * 4]; /* 16色纯色/渐变 128x160! */
        uint8_t p8[8];
        int p = 32 + UI_H * 2;
        memset(img128, 0, sizeof img128);
        img128[0] = (uint8_t)(UI_RGB565(12, 28, 36) & 0xFFu);
        img128[1] = (uint8_t)(UI_RGB565(12, 28, 36) >> 8);
        img128[2] = (uint8_t)(UI_COL_MINT & 0xFFu);
        img128[3] = (uint8_t)(UI_COL_MINT >> 8);
        for (y = 0; y < UI_H; y++) {
            uint8_t ci = (y > UI_H / 2) ? 1u : 0u;
            int w0 = UI_W <= 129 ? UI_W : 128;
            int rem = UI_W - w0;
            img128[p++] = (uint8_t)(0x80u | (w0 - 2));
            img128[p++] = ci;
            if (rem >= 2) {
                img128[p++] = (uint8_t)(0x80u | (rem - 2));
                img128[p++] = ci;
            }
            rx_n = (uint16_t)(p - (32 + UI_H * 2));
            img128[32 + y * 2] = (uint8_t)(rx_n & 0xFFu);
            img128[32 + y * 2 + 1] = (uint8_t)(rx_n >> 8);
        }
        total = p;
        p8[0] = (uint8_t)(UI_W & 0xFFu); p8[1] = (uint8_t)(UI_W >> 8);
        p8[2] = (uint8_t)(UI_H & 0xFFu); p8[3] = (uint8_t)(UI_H >> 8);
        p8[4] = UI_EXT_FMT_PAL4_RLE;
        p8[5] = (uint8_t)(total & 0xFFu); p8[6] = (uint8_t)(total >> 8);
        p8[7] = 1u; /* apply_dial = 1 */
        push_link_frame(0x28u, p8, 8);
        step(1);

        chunks = (total + 127) / 128;
        for (c = 0; c < chunks; c++) {
            int left = total - c * 128;
            int n = left > 128 ? 128 : left;
            pay[0] = (uint8_t)c;
            memcpy(pay + 1, img128 + c * 128, (size_t)n);
            push_link_frame(0x29u, pay, 1 + n);
            step(1);
        }
    }
    REQUIRE(settings_get()->dial_id == UI_DIAL_PHOTO && settings_get()->img_ready == 1u &&
            settings_get()->img_on_chip == 1u,
            "compressed photo dial applied on-chip via 0x28/0x29");

    /* 重启冷启动校验相册表盘从片内 Hot-Swap 自动恢复并渲染 */
    ui_init();
    REQUIRE(settings_get()->dial_id == UI_DIAL_PHOTO && settings_get()->img_ready == 1u &&
            settings_get()->img_on_chip == 1u,
            "on-chip photo dial metadata survived cold boot");
    step(4);
    REQUIRE(hal_host_pixel(4, UI_H - 12) == UI_COL_MINT,
            "restored on-chip PAL4_RLE photo dial bottom pixels drawn crisp");
    REQUIRE(!save("shot_dial_photo.bmp"), "photo dial shot");

    /* 查询 0x2B -> 校验 0x2C 状态包（含 bit4: img_on_chip） */
    hal_host_link_clear();
    push_link_frame(0x2Bu, pay, 0);
    step(2);
    hal_host_link_tx_take(wire, sizeof wire, &tx_n);
    rx_cfg = NULL;
    for (c = 0; c + 30 <= tx_n; c++) {
        if (wire[c] == 0xA5u && wire[c + 1] == 0x2Cu && wire[c + 2] == 26u) {
            rx_cfg = wire + c;
            break;
        }
    }
    REQUIRE(rx_cfg != NULL && (rx_cfg[23] & 0x1Fu) == 0x1Fu &&
            rx_cfg[24] == UI_CFG_PRIO_APP && rx_cfg[28] == 0x50u,
            "0x2C config state reflects AT24C256 present + loaded + photo + on_chip + mag cal @0x50");

    /* 5. 验证 AT24C256 动态内存池：在已存压缩相册表盘基础上写入 12 条消息，断电重启仍保留且可滚动/查看详情 */
    {
        uint32_t msg_total = 12u * 352u;
        int msg_chunks = (int)((msg_total + 127u) / 128u);
        uint16_t p_tot = 0, p_used = 0, p_free = 0, p_mb = 0, p_ib = 0;
        uint8_t p_mn = 0;
        uint32_t ram0 = 0, ram1 = 0;

        pay[0] = 12u;
        push_link_frame(0x25u, pay, 1);
        step(1);
        for (c = 0; c < msg_chunks; c++) {
            int k;
            pay[0] = (uint8_t)c;
            for (k = 0; k < 128; k++) {
                uint32_t gp = (uint32_t)c * 128u + (uint32_t)k;
                uint32_t r = gp / 352u;
                uint32_t ir = gp % 352u;
                pay[1 + k] = (gp < msg_total && ir >= 16u && ir < 32u && (k % 16) < 8)
                             ? (uint8_t)(0x3Cu + (r & 0x0Fu))
                             : 0u;
            }
            push_link_frame(0x22u, pay, 129);
            step(1);
        }
        /* 为第 10 条槽位写入一条特征详情，检验跨重启 RLE 存储与按需解压 */
        for (c = 0; c < 20; c++) {
            int k;
            pay[0] = 10u;
            pay[1] = (uint8_t)c;
            for (k = 0; k < 128; k++) {
                pay[2 + k] = (c == 5 && (k % 16) == 4) ? 0xFFu : 0u;
            }
            push_link_frame(0x23u, pay, 130);
            step(1);
        }
        /* 再通过 0x2D(0x01) 头插 + 0x2D(0x02) 写入第 13 条最新消息 */
        pay[0] = 1u;
        push_link_frame(0x2Du, pay, 1);
        step(1);
        for (c = 0; c < 3; c++) {
            int len_c = (c == 2) ? 96 : 128;
            pay[0] = 2u;
            pay[1] = (uint8_t)c;
            memset(pay + 2, (c == 0) ? 0xAAu : 0x00u, (size_t)len_c);
            push_link_frame(0x2Du, pay, 2 + len_c);
            step(1);
        }

        /* 冷重启 ui_init() 验证断电不丢失：共 13 条消息 + 原槽10变为槽11详情 + 相册图均保留 */
        ui_init();
        hal_host_set_frame_step_us(16000);
        ui_mem_get(&ram0, NULL);
        link_ext_pool_stats(&p_tot, &p_used, &p_free, &p_mb, &p_ib, &p_mn);
        REQUIRE(p_tot == LINK_EXT_POOL_SIZE && p_mn == 13u && p_ib == 0u && p_mb > 0u &&
                settings_get()->img_on_chip == 1u && p_used + p_free <= p_tot,
                "on-chip hot-swap keeps EEPROM image bytes 0 while dynamic ext ROM holds 13 compressed messages");
        REQUIRE(link_msg_count() == 13 && settings_get()->img_ready == 1u,
                "13 messages and on-chip dial photo survived cold boot");

        /* 打开消息页（断开连接状态）并下滚至第 11 条读取压缩详情 */
        ui_nav_goto(UI_PAGE_MSG);
        step(2);
        key_tap(HAL_KEY2);
        step(26);
        for (c = 0; c < 11; c++) {
            key_tap(HAL_KEY3);
            step(2);
        }
        step(24);
        key_tap(HAL_KEY2); /* 进入槽 11 详情 */
        step(18);
        REQUIRE(link_msg_detail_ready(), "slot 11 compressed detail restored offline after reboot");
        REQUIRE(hal_host_pixel((UI_W - 128) / 2 + 32, (UI_H - 160) / 2 + 40) != UI_COL_BG,
                "restored detail pixel mismatch");
        hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(36);
        hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(36);
        ui_mem_get(&ram1, NULL);
        REQUIRE(ram1 == ram0, "dynamic RAM window/detail exited without leaking");

        /* 6. 测试超片内阈值 (>hal_int_img_cap) 的大图回退到 AT24C256，以及清除通知后片外底图保留 */
        {
            static uint8_t img_big[32 + UI_H * 2 + UI_H * (UI_W + 8)];
            uint8_t p8[8];
            int bp = 32 + UI_H * 2;
            int big_tot, big_chunks, row_b = ((UI_W + 1) / 2);
            memset(img_big, 0, sizeof img_big);
            img_big[0] = (uint8_t)(UI_RGB565(12, 28, 36) & 0xFFu);
            img_big[1] = (uint8_t)(UI_RGB565(12, 28, 36) >> 8);
            img_big[2] = (uint8_t)(UI_COL_MINT & 0xFFu);
            img_big[3] = (uint8_t)(UI_COL_MINT >> 8);
            bp = 32 + UI_H * 2;
            for (y = 0; y < UI_H; y++) {
                uint8_t ci = (y > UI_H / 2) ? 1u : 0u;
                uint8_t val = (uint8_t)(ci | (ci << 4));
                if ((uint32_t)bp < hal_int_img_cap() + 1024u) {
                    int seg = 0;
                    while (seg < UI_W) {
                        img_big[bp++] = 1u; /* literal 2 px -> 2 B per 2 px */
                        img_big[bp++] = val;
                        seg += 2;
                    }
                    if ((UI_W & 1) != 0) {
                        img_big[bp++] = 0u;
                        img_big[bp++] = val;
                    }
                } else {
                    int w0 = UI_W <= 129 ? UI_W : 128;
                    int rem = UI_W - w0;
                    img_big[bp++] = (uint8_t)(0x80u | (w0 - 2));
                    img_big[bp++] = ci;
                    if (rem >= 2) {
                        img_big[bp++] = (uint8_t)(0x80u | (rem - 2));
                        img_big[bp++] = ci;
                    } else if (rem == 1) {
                        img_big[bp++] = 0u;
                        img_big[bp++] = val;
                    }
                }
                rx_n = (uint16_t)(bp - (32 + UI_H * 2));
                img_big[32 + y * 2] = (uint8_t)(rx_n & 0xFFu);
                img_big[32 + y * 2 + 1] = (uint8_t)(rx_n >> 8);
            }
            (void)row_b;
            big_tot = bp;
            p8[0] = (uint8_t)(UI_W & 0xFFu); p8[1] = (uint8_t)(UI_W >> 8);
            p8[2] = (uint8_t)(UI_H & 0xFFu); p8[3] = (uint8_t)(UI_H >> 8);
            p8[4] = UI_EXT_FMT_PAL4_RLE;
            p8[5] = (uint8_t)(big_tot & 0xFFu); p8[6] = (uint8_t)(big_tot >> 8);
            p8[7] = 1u;
            push_link_frame(0x28u, p8, 8);
            step(1);
            big_chunks = (big_tot + 127) / 128;
            for (c = 0; c < big_chunks; c++) {
                int left = big_tot - c * 128;
                int n = left > 128 ? 128 : left;
                pay[0] = (uint8_t)c;
                memcpy(pay + 1, img_big + c * 128, (size_t)n);
                push_link_frame(0x29u, pay, 1 + n);
                step(1);
            }
            ui_init();
            hal_host_set_frame_step_us(16000);
            step(4);
            REQUIRE(settings_get()->img_ready == 1u && settings_get()->img_on_chip == 0u &&
                    hal_host_pixel(4, UI_H - 12) == UI_COL_MINT,
                    "oversized image automatically falls back to AT24C256 EEPROM across reboot");
        }
        ui_nav_goto(UI_PAGE_MSG);
        step(2);
        key_tap(HAL_KEY2);
        step(26);
        for (c = 0; c < 13; c++) {
            key_tap(HAL_KEY3);
            step(2);
        }
        step(20);
        key_tap(HAL_KEY2); /* 触发列表末尾“清除通知” */
        step(8);
        link_ext_pool_stats(&p_tot, &p_used, &p_free, &p_mb, &p_ib, &p_mn);
        REQUIRE(link_msg_count() == 0 && p_mn == 0u && p_mb == 0u && p_ib > 0u,
                "messages page clear-notifications action emptied dynamic ROM messages while keeping dial image");
        {
            int px, py, empty_ink = 0;
            for (py = 50; py < UI_H - 12 && !empty_ink; py++) {
                for (px = 8; px < UI_W - 8; px++) {
                    if (hal_host_pixel(px, py) == UI_COL_MUTED_LIGHT) {
                        empty_ink = 1;
                        break;
                    }
                }
            }
            REQUIRE(empty_ink, "cleared messages displays STHupo empty state");
        }
        hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(36);

        /* 7. 验证 Plan B 3D 指南针 (ARM M4 PIC -> SRAM 运行) 与关于彩图包的 0x2F 持久化与动态 RAM 释放 */
        {
            uint32_t m_base, m_comp, m_after_comp, m_c3d, m_after_c3d;
            uint8_t c_asset[6 + 128];
            int16_t aw = 0, ah = 0;
            const uint8_t *abits;
            int chunk;
            uint16_t c3d_len = (uint16_t)sizeof compass3d_m4_blob;
            uint16_t c3d_crc = settings_crc16(compass3d_m4_blob, c3d_len);
            uint16_t abt_len = (uint16_t)sizeof about_art_blob;
            uint16_t abt_crc = settings_crc16(about_art_blob, abt_len);
            uint16_t off = 0u;

            for (chunk = 0; chunk < 4; chunk++) {
                c_asset[0] = LINK_ASSET_COMPASS;
                c_asset[1] = (uint8_t)chunk;
                c_asset[2] = 64; c_asset[3] = 0;
                c_asset[4] = 64; c_asset[5] = 0;
                memset(c_asset + 6, (uint8_t)(0x5Au + chunk), 128);
                push_link_frame(0x2Fu, c_asset, 6 + 128);
                step(1);
            }
            ui_init();
            hal_host_set_frame_step_us(16000);
            ui_mem_get(&m_base, NULL);
            link_enter_compass();
            ui_mem_get(&m_comp, NULL);
            abits = link_compass_asset_mask(&aw, &ah);
            REQUIRE(link_compass_asset_ready() && aw == 64 && ah == 64 && abits != NULL &&
                    abits[0] == 0x5Au && abits[128] == 0x5Bu && m_comp > m_base,
                    "compass 0x2F legacy 1bpp fallback persisted and loaded into dynamic RAM");
            link_leave_compass();
            ui_mem_get(&m_after_comp, NULL);
            REQUIRE(!link_compass_asset_ready() && m_after_comp == m_base,
                    "leaving compass freed legacy fallback RAM back to baseline");

            /* 下发 ARM Cortex-M4 3D 指南针机器码 (ID=3) 与关于插图包 (ID=4) */
            for (chunk = 0, off = 0u; off < c3d_len; chunk++) {
                uint16_t take = (uint16_t)(c3d_len - off) > 128u ? 128u : (uint16_t)(c3d_len - off);
                c_asset[0] = LINK_ASSET_COMPASS3D;
                c_asset[1] = (uint8_t)chunk;
                c_asset[2] = (uint8_t)(c3d_len & 0xFFu); c_asset[3] = (uint8_t)(c3d_len >> 8);
                c_asset[4] = (uint8_t)(c3d_crc & 0xFFu); c_asset[5] = (uint8_t)(c3d_crc >> 8);
                memcpy(c_asset + 6, compass3d_m4_blob + off, take);
                push_link_frame(0x2Fu, c_asset, (int)(6u + take));
                step(1);
                off = (uint16_t)(off + take);
            }
            for (chunk = 0, off = 0u; off < abt_len; chunk++) {
                uint16_t take = (uint16_t)(abt_len - off) > 128u ? 128u : (uint16_t)(abt_len - off);
                c_asset[0] = LINK_ASSET_ABOUT;
                c_asset[1] = (uint8_t)chunk;
                c_asset[2] = (uint8_t)(abt_len & 0xFFu); c_asset[3] = (uint8_t)(abt_len >> 8);
                c_asset[4] = (uint8_t)(abt_crc & 0xFFu); c_asset[5] = (uint8_t)(abt_crc >> 8);
                memcpy(c_asset + 6, about_art_blob + off, take);
                push_link_frame(0x2Fu, c_asset, (int)(6u + take));
                step(1);
                off = (uint16_t)(off + take);
            }

            ui_init();
            hal_host_set_frame_step_us(16000);
            REQUIRE(link_about_asset_ready(), "about ABT2 asset persisted in AT24C256 across reboot");
            {
                uint32_t m_abt = 0, m_after_abt = 0;
                uint16_t abt_tot = 0u;
                link_enter_about();
                ui_mem_get(&m_abt, NULL);
                REQUIRE(link_about_blob(&abt_tot) != NULL && abt_tot == abt_len && m_abt > m_base,
                        "about ABT2 asset loaded into ui_mem SRAM on enter");
                link_leave_about();
                ui_mem_get(&m_after_abt, NULL);
                REQUIRE(link_about_blob(NULL) == NULL && m_after_abt == m_base,
                        "leaving about freed ABT2 SRAM back to baseline");
            }
            ui_mem_get(&m_base, NULL);
            link_enter_compass();
            ui_mem_get(&m_c3d, NULL);
            REQUIRE(link_compass3d_ready() && m_c3d > m_base,
                    "Plan B 3D compass M4 PIC blob loaded and CRC-verified into dynamic SRAM");
            link_leave_compass();
            ui_nav_goto(UI_PAGE_SPORT);
            step(2);
            key_tap(HAL_KEY2);
            step(24);
            key_tap(HAL_KEY3);
            {
                ui_render_stats_t s_cslide;
                ui_render_get_stats(&s_cslide);
                REQUIRE(s_cslide.pixels > 0u && link_compass_loading() && !link_compass3d_ready(),
                        "3D compass slides without stalling while C3D1 loads incrementally");
            }
            step(24);
            REQUIRE(link_compass3d_ready(), "3D compass ready once push slide settles");
            REQUIRE(!save("shot_sport_compass3d.bmp"), "Plan B 3D compass screen shot");
            hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(30);
            link_leave_compass();
            ui_mem_get(&m_after_c3d, NULL);
            REQUIRE(!link_compass3d_ready() && m_after_c3d == m_base,
                    "leaving 3D compass freed all SRAM back to baseline");

            /* 7b. 测试 SHD1/SPR1/ANL1/SYS1 0x2F 下发与状态上报 + 0x0B 音乐包绝不抢占前台表盘页 */
            {
                const struct {
                    uint8_t id;
                    const uint8_t *blob;
                    uint16_t len;
                } ext_mods[4] = {
                    { LINK_ASSET_HEADERS, headers_sthupo_blob, (uint16_t)sizeof headers_sthupo_blob },
                    { LINK_ASSET_SPORT, sport_mod_m4_blob, (uint16_t)sizeof sport_mod_m4_blob },
                    { LINK_ASSET_ANALOG, analog_mod_m4_blob, (uint16_t)sizeof analog_mod_m4_blob },
                    { LINK_ASSET_SYS, sys_mod_m4_blob, (uint16_t)sizeof sys_mod_m4_blob },
                };
                int mi, mc;
                uint16_t off_m;
                const uint8_t *rx_ep = NULL;
                (void)hal_ext_flash_erase(LINK_EXT_OFF_CFG, LINK_EXT_OFF_POOL);
                ui_init();
                hal_host_set_frame_step_us(16000);
                for (mi = 0; mi < 4; mi++) {
                    uint16_t m_len = ext_mods[mi].len;
                    uint16_t m_crc = settings_crc16(ext_mods[mi].blob, m_len);
                    for (mc = 0, off_m = 0u; off_m < m_len; mc++) {
                        uint16_t take = (uint16_t)(m_len - off_m) > 128u ? 128u : (uint16_t)(m_len - off_m);
                        c_asset[0] = ext_mods[mi].id;
                        c_asset[1] = (uint8_t)mc;
                        c_asset[2] = (uint8_t)(m_len & 0xFFu); c_asset[3] = (uint8_t)(m_len >> 8);
                        c_asset[4] = (uint8_t)(m_crc & 0xFFu); c_asset[5] = (uint8_t)(m_crc >> 8);
                        memcpy(c_asset + 6, ext_mods[mi].blob + off_m, take);
                        push_link_frame(0x2Fu, c_asset, (int)(6u + take));
                        step(1);
                        off_m = (uint16_t)(off_m + take);
                    }
                }
                /* 检查 SHD1 同时查到中文标题字模与数字 '8' */
                link_hdr_active(1);
                {
                    int16_t hw = 0, hh = 0, hadv = 0;
                    const uint8_t *hrle = NULL;
                    REQUIRE(link_hdr_lookup(0x8BBEu, &hw, &hh, &hadv, NULL, NULL, &hrle) && hrle != NULL,
                            "SHD1 lookup returns Chinese header glyph from AT24C256");
                    REQUIRE(link_hdr_lookup((uint32_t)'8', &hw, &hh, &hadv, NULL, NULL, &hrle) && hrle != NULL,
                            "SHD1 lookup returns STHupo clock digit '8' from AT24C256");
                }
                link_hdr_active(0);

                /* 验证停留在表盘页时收到 0x0B chunk 0：绝不跳转至音乐页，且 0x2E bit4 = 0（未启动音乐） */
                ui_nav_goto(UI_PAGE_FACE);
                step(4);
                hal_host_link_clear();
                {
                    uint8_t m_pay[64];
                    memset(m_pay, 0, sizeof m_pay);
                    m_pay[0] = 0u;
                    push_link_frame(0x0Bu, m_pay, 32);
                    step(2);
                }
                REQUIRE(!ui_app_music_active(),
                        "0x0B music chunk never force-opens music UI when user is on watch face");
                pay[0] = 0u;
                push_link_frame(0x2Du, pay, 1);
                step(2);
                hal_host_link_tx_take(wire, sizeof wire, &tx_n);
                for (c = 0; c + 16 <= tx_n; c++) {
                    if (wire[c] == 0xA5u && wire[c + 1] == 0x2Eu && wire[c + 2] == 13u) {
                        rx_ep = wire + c;
                    }
                }
                REQUIRE(rx_ep != NULL && (rx_ep[15] & 0xE2u) == 0xE2u && (rx_ep[15] & 0x10u) == 0u,
                        "0x2E reports SHD1/SPR1/ANL1/SYS1 ready (0xE2) and watchMusicActive=0 outside Music UI");

                /* 显式打开音乐界面后，0x2E bit4 = 1 且 0x0B 正常接收 */
                ui_nav_goto(UI_PAGE_MUSIC);
                step(2);
                key_tap(HAL_KEY2);
                step(24);
                hal_host_link_clear();
                pay[0] = 0u;
                push_link_frame(0x2Du, pay, 1);
                step(2);
                hal_host_link_tx_take(wire, sizeof wire, &tx_n);
                rx_ep = NULL;
                for (c = 0; c + 16 <= tx_n; c++) {
                    if (wire[c] == 0xA5u && wire[c + 1] == 0x2Eu && wire[c + 2] == 13u) {
                        rx_ep = wire + c;
                    }
                }
                REQUIRE(ui_app_music_active() && rx_ep != NULL && (rx_ep[15] & 0x10u) != 0u,
                        "entering Music UI sets watchMusicActive=1 in 0x2E");
                hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(30);
            }
        }

        /* 8. 验证 1207 转子马达震动场景（按键/长按/指南针校准提示/新消息/设置开关静音）+ “关于”放大彩图页 */
        {
            uint16_t bp = 0, bm = 0;
            uint32_t bc0 = 0, bc1 = 0;
            uint8_t ext1[1] = {1u};
            uint8_t a_pkt[6 + 128];
            uint16_t abt_len = (uint16_t)sizeof about_art_blob;
            uint16_t abt_crc = settings_crc16(about_art_blob, abt_len);
            uint16_t a_off = 0u;
            int a_ch = 0;
            int px, py, about_ink = 0, intro_ink = 0;

            (void)hal_ext_flash_erase(LINK_EXT_OFF_CFG, LINK_EXT_OFF_POOL);
            ui_init();
            hal_host_set_frame_step_us(16000);
            /* 默认震动关闭，短按不震；开启后短按 KEY3 进页应触发 38ms 单震 */
            REQUIRE(settings_get()->key_beep == 0u, "vibration defaults off on clean boot");
            hal_host_beep_get(NULL, NULL, &bc0);
            key_tap(HAL_KEY3);
            hal_host_beep_get(NULL, NULL, &bc1);
            REQUIRE(bc1 == bc0, "short press stays silent when vibration defaults off");
            ui_nav_goto(UI_PAGE_FACE);
            step(2);
            settings_get()->key_beep = 1u;
            hal_host_beep_get(NULL, NULL, &bc0);
            key_tap(HAL_KEY3);
            hal_host_beep_get(&bp, &bm, &bc1);
            REQUIRE(bc1 > bc0 && bp == HAL_VIB_SINGLE && bm == 38u,
                    "short press triggers 38ms rotor single buzz");

            /* 长按 KEY2 进表盘编辑 -> 130ms 长震动 */
            ui_nav_goto(UI_PAGE_FACE);
            step(2);
            hal_host_set_keys(HAL_KEY2);
            step(32);
            hal_host_set_keys(0);
            hal_host_beep_get(&bp, &bm, &bc1);
            REQUIRE(bp == HAL_VIB_SINGLE && bm == 130u,
                    "long press triggers 130ms long rotor buzz");
            hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(22);

            /* 指南针点击启动校准 -> 双震提示 */
            ui_nav_goto(UI_PAGE_SPORT);
            step(2);
            key_tap(HAL_KEY2);
            step(24);
            key_tap(HAL_KEY3);
            hal_host_beep_get(&bp, &bm, &bc1);
            REQUIRE(bp == HAL_VIB_SINGLE && bm == 38u,
                    "entering compass plays only key-tap 38ms tick, no auto buzz");
            step(24);
            key_tap(HAL_KEY2);
            hal_host_beep_get(&bp, &bm, &bc1);
            REQUIRE(bp == HAL_VIB_DOUBLE && bm == 65u,
                    "compass calibration prompt triggers double buzz");
            hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(30);

            /* 新消息头插推送 0x2D [0x01] -> 经典功能机三连短信震 */
            push_link_frame(0x2Du, ext1, 1);
            step(1);
            hal_host_beep_get(&bp, &bm, &bc1);
            REQUIRE(bp == HAL_VIB_SMS && bm == 70u,
                    "new message triggers triple SMS buzz");

            /* 下发“关于”放大彩图+华文琥珀简介片外包并打开设置第 8 项（索引 7）截图验证 */
            for (a_ch = 0, a_off = 0u; a_off < abt_len; a_ch++) {
                uint16_t take = (uint16_t)(abt_len - a_off) > 128u ? 128u : (uint16_t)(abt_len - a_off);
                a_pkt[0] = LINK_ASSET_ABOUT;
                a_pkt[1] = (uint8_t)a_ch;
                a_pkt[2] = (uint8_t)(abt_len & 0xFFu); a_pkt[3] = (uint8_t)(abt_len >> 8);
                a_pkt[4] = (uint8_t)(abt_crc & 0xFFu); a_pkt[5] = (uint8_t)(abt_crc >> 8);
                memcpy(a_pkt + 6, about_art_blob + a_off, take);
                push_link_frame(0x2Fu, a_pkt, (int)(6u + take));
                step(1);
                a_off = (uint16_t)(a_off + take);
            }
            REQUIRE(link_about_asset_ready(), "about art bundle ready for screenshot");

            ui_nav_goto(UI_PAGE_SETTINGS);
            step(2);
            key_tap(HAL_KEY2);
            step(24);
            for (c = 0; c < 7; c++) {
                key_tap(HAL_KEY3);
                step(2);
            }
            step(20);
            {
                uint32_t m_pre_abt = 0, m_cap = 0, m_in_abt = 0;
                ui_mem_get(&m_pre_abt, &m_cap);
                key_tap(HAL_KEY2);
                {
                    ui_render_stats_t s_aslide;
                    ui_render_get_stats(&s_aslide);
                    REQUIRE(s_aslide.pixels > 0u && link_about_loading() && link_about_blob(NULL) == NULL,
                            "about page animates smoothly while ABT2 loads incrementally");
                }
                step(24);
                REQUIRE(!save("shot_about.bmp"), "about page enlarged Pinkie + STHupo art shot");
                {
                    ui_render_stats_t s_abt;
                    ui_mem_get(&m_in_abt, &m_cap);
                    REQUIRE(link_about_blob(NULL) != NULL &&
                            m_in_abt >= m_pre_abt + sizeof about_art_blob &&
                            m_in_abt <= m_cap,
                            "about page cached ABT2 inside ui_mem SRAM within budget");
                    step(2);
                    ui_render_get_stats(&s_abt);
                    REQUIRE(s_abt.pixels == 0u, "about settled screen uses zero dirty pixels (true partial refresh)");
                }
                for (py = 29; py < 102 && !about_ink; py++) {
                    for (px = UI_W / 2 - 25; px < UI_W / 2 + 25; px++) {
                        if (hal_host_pixel(px, py) != UI_COL_BG) {
                            about_ink = 1;
                            break;
                        }
                    }
                }
                for (py = 100; py < 142 && !intro_ink; py++) {
                    for (px = 10; px < UI_W - 10; px++) {
                        if (hal_host_pixel(px, py) != UI_COL_BG) {
                            intro_ink = 1;
                            break;
                        }
                    }
                }
                REQUIRE(about_ink && intro_ink, "about page drew SRAM-cached enlarged Pinkie + STHupo intro");

                /* 从关于退出到设置列表第 3 项（索引 2 = 震动）关闭开关后，再操作/来消息均不得震动 */
                hal_host_set_keys(HAL_KEY2); step(32); hal_host_set_keys(0); step(28);
                {
                    uint32_t m_after_abt = 0;
                    ui_mem_get(&m_after_abt, NULL);
                    REQUIRE(link_about_blob(NULL) == NULL && m_after_abt <= m_pre_abt,
                            "leaving about page freed ABT2 SRAM buffer");
                }
            }
            for (c = 0; c < 5; c++) {
                key_tap(HAL_KEY1);
                step(2);
            }
            step(20);
            REQUIRE(settings_get()->key_beep == 1u, "settings index 2 vibration currently on");
            key_tap(HAL_KEY2); /* 关闭震动 */
            step(4);
            REQUIRE(settings_get()->key_beep == 0u, "settings index 2 toggled vibration off");
            key_tap(HAL_KEY2); /* 开启震动 */
            step(4);
            REQUIRE(settings_get()->key_beep == 1u, "settings index 2 toggled vibration on");
            key_tap(HAL_KEY2); /* 再次关闭震动 */
            step(4);
            REQUIRE(settings_get()->key_beep == 0u, "settings index 2 toggled vibration off again");
            hal_host_beep_get(NULL, NULL, &bc0);
            key_tap(HAL_KEY3);
            push_link_frame(0x2Du, ext1, 1);
            step(2);
            hal_host_beep_get(NULL, NULL, &bc1);
            REQUIRE(bc1 == bc0, "vibration muted when switch is OFF");
        }
    }

    /* 清空测试写入的 EEPROM 设置与目录槽位，避免干扰其他独立测试默认初值 */
    (void)hal_ext_flash_erase(LINK_EXT_OFF_CFG, LINK_EXT_OFF_POOL);
    ui_init();
    printf("EEPROM persist / priority / compressed dials PASS\n");
    return 0;
}

static int test_imu9_fusion(void) {
    imu9_state_t s;
    hal_imu_t imu;
    hal_mag_t mag;
    uint32_t t = 1000u;
    int i;

    imu9_init();
    memset(&imu, 0, sizeof imu);
    imu.az = 1000;
    imu.temp = 285;
    mag.x = 0; mag.y = 300; mag.z = -500; /* 面朝正北 */
    imu9_feed(&imu, &mag, t);
    imu9_get(&s);
    REQUIRE(s.fused_axes == 9 &&
            angle_diff_deg((int)s.heading_deg, 0) <= 2 &&
            s.roll_d10 >= -20 && s.roll_d10 <= 20 &&
            s.pitch_d10 >= -20 && s.pitch_d10 <= 20,
            "9-axis instant triad north alignment");

    /* 顺时针转向东 90°：+X体轴指东变指南，+Y体轴指北变指东，磁场沿-X_b */
    imu9_reset();
    mag.x = -300; mag.y = 0; mag.z = -500;
    imu9_feed(&imu, &mag, t += 20u);
    imu9_get(&s);
    REQUIRE(angle_diff_deg((int)s.heading_deg, 90) <= 2,
            "9-axis triad east heading 90 deg");

    /* 抬表 45° (+pitch around +X_b)：az=707, ay=+707；北方在Body +Y_b/+Z_b投影 */
    imu9_reset();
    imu.ax = 0; imu.ay = 707; imu.az = 707;
    /* 北(0,300,-500) 沿 Pitch=+45°(+Y->+Z) 逆变换到体轴: my=300*c-(-500)*s=565? Wait R_x(+45)^T*(0,B_h,B_z) */
    mag.x = 0; mag.y = -141; mag.z = -566; /* R_x(+45)^T * (0, 300, -500): y=212-354=-142, z=-212-354=-566 */
    imu9_feed(&imu, &mag, t += 20u);
    imu9_get(&s);
    REQUIRE(s.pitch_d10 >= 435 && s.pitch_d10 <= 465 &&
            s.roll_d10 >= -20 && s.roll_d10 <= 20 &&
            angle_diff_deg((int)s.heading_deg, 0) <= 3,
            "9-axis tilt-compensated north at +45 deg pitch");

    /* 平放静止陀螺零偏学习 (gb = +25, -15, +12 0.1dps) */
    imu9_init();
    imu.ax = 0; imu.ay = 0; imu.az = 1000;
    imu.gx = 25; imu.gy = -15; imu.gz = 12;
    mag.x = 0; mag.y = 300; mag.z = -500;
    for (i = 0; i < 45; i++) {
        imu9_feed(&imu, &mag, t += 20u);
    }
    imu9_get(&s);
    REQUIRE(s.gyro_cal == 1 && s.gbx == 25 && s.gby == -15 && s.gbz == 12 &&
            angle_diff_deg((int)s.heading_deg, 0) <= 2,
            "9-axis gyro bias convergence & stationary lock");

    /* 3D 磁硬铁校准 */
    imu9_cal_start();
    for (i = -1; i <= 1; i += 2) {
        mag.x = (int16_t)(120 + i * 300); mag.y = -180; mag.z = 90;
        imu9_feed(&imu, &mag, t += 20u);
        mag.x = 120; mag.y = (int16_t)(-180 + i * 300); mag.z = 90;
        imu9_feed(&imu, &mag, t += 20u);
        mag.x = 120; mag.y = -180; mag.z = (int16_t)(90 + i * 500);
        imu9_feed(&imu, &mag, t += 20u);
    }
    imu9_cal_start(); /* 完成校准 */
    mag.x = 120 + 0; mag.y = -180 + 300; mag.z = 90 - 500;
    imu9_feed(&imu, &mag, t += 20u);
    imu9_get(&s);
    REQUIRE(s.mag_cal == 2 && s.mox == 120 && s.moy == -180 && s.moz == 90 &&
            angle_diff_deg((int)s.heading_deg, 0) <= 2,
            "9-axis hard-iron calibration");

    /* 磁力计失联退化到 6 轴 */
    t += 1100u;
    imu9_feed(&imu, NULL, t);
    imu9_get(&s);
    REQUIRE(s.fused_axes == 6 && s.imu_ok == 1 && s.mag_ok == 0,
            "6-axis fallback when magnetometer drops");

    printf("IMU9 9-axis fusion & calibration PASS\n");
    return 0;
}

int host_render_test(const char *shots) {
    int result;
    output_dir = shots;
    hal_host_init(0);
    hal_host_use_virtual_time();
    result = test_clock_and_detector();
    if (!result) result = test_core_drawing();
    if (!result) result = test_scaled_edges();
    if (!result) result = test_anim_stop();
    if (!result) result = test_anim_slots();
    if (!result) result = test_arena();
    if (!result) result = test_perf_request();
    if (!result) result = test_dirty();
    if (!result) result = test_full_frame_fps();
    if (!result) result = test_async_and_preempt();
    if (!result) result = test_face_snapshot_and_keys();
    if (!result) result = test_ext_flash_and_stream();
    if (!result) result = test_persist_and_compressed_dials();
    if (!result) result = test_imu9_fusion();
    ui_set_service(NULL);
    hal_host_set_keys(0);
    hal_host_shutdown();
    if (!result) printf("Renderer PASS %dx%d\n", UI_W, UI_H);
    return result;
}
