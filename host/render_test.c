#include "ui_internal.h"
#include "hal_host.h"

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
        hal_host_display_get_stats(&display);
        REQUIRE(display.calls == 0, "public primitives submitted before flush");
        ui_draw_flush();
        ui_render_get_stats(&render);
        hal_host_display_get_stats(&display);
        REQUIRE(render.items == 4 && render.dropped == 0,
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
    uint8_t checksum = 0;
    int n, i;
    ui_render_get_stats(&stats);
    values[0] = stats.prepare_us;
    values[1] = stats.raster_us;
    values[2] = stats.wait_us;
    values[3] = stats.frame_us;
    values[4] = stats.pixels;
    values[5] = (uint32_t)stats.bands | ((uint32_t)stats.dropped << 16);
    ui_mem_get(&values[6], &values[7]);
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
    }
    ui_init();
    ui_mem_get(&first, &total);
    REQUIRE(total == 4864u + 4u * UI_W * UI_BAND_H, "declared RAM budget");
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
    REQUIRE(!link_media_playing(), "long enter release became short confirm");
    key_tap(HAL_KEY2);
    REQUIRE(link_media_playing(), "short enter after long press was lost");
    printf("Unchanged face / full snapshot / rapid reversal / long press PASS\n");
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
    if (!result) result = test_async_and_preempt();
    if (!result) result = test_face_snapshot_and_keys();
    ui_set_service(NULL);
    hal_host_set_keys(0);
    hal_host_shutdown();
    if (!result) printf("Renderer PASS %dx%d\n", UI_W, UI_H);
    return result;
}
