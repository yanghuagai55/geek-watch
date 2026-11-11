#include "ui_internal.h"

#include "hal.h"

/* 预设只描述“哪个属性、从几到几、花多久”。
   缓出：开头走得快，结尾慢下来。全程整数，两边芯片都没有硬件浮点。 */

enum { PROP_X = 0, PROP_OPACITY = 1, PROP_SCALE = 2, SLOT_N = 4 };

typedef struct preset {
    uint8_t prop;
    int16_t from;
    int16_t to;
    uint16_t dur;
} preset_t;

typedef struct slot {
    ui_obj_t *obj;
    uint32_t t0;
    uint16_t dur;
    int16_t from;
    int16_t to;
    uint8_t prop;
    uint8_t active;
} slot_t;

static const preset_t presets[] = {
    {PROP_X, 0, -HAL_LCD_W, 220},
    {PROP_X, 0, HAL_LCD_W, 220},
    {PROP_OPACITY, 0, 255, 180},
    {PROP_OPACITY, 255, 0, 180},
    {PROP_SCALE, 0, 255, 280},
    {PROP_SCALE, 255, 0, 240},
    {PROP_X, 0, 0, 200},
};

static slot_t *slots;

static uint8_t ease_out(uint32_t t) {
    uint32_t u;
    uint32_t u3;
    uint32_t e;

    if (t >= 256) {
        return 255;
    }
    u = 256 - t;
    u3 = u * u * u;
    e = 256 - u3 / (256u * 256u);
    if (e > 255) {
        e = 255;
    }
    return (uint8_t)e;
}

static void apply(slot_t *s, int16_t value) {
    if (!s->obj) {
        return;
    }
    if (s->prop == PROP_X) {
        s->obj->x = value;
        return;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    if (s->prop == PROP_SCALE) {
        s->obj->scale = (uint8_t)value;
        return;
    }
    s->obj->opacity = (uint8_t)value;
}

static const char *anim_name(ui_anim_id_t id) {
    switch (id) {
    case UI_ANIM_SLIDE_LEFT:
        return "ui_anim_play(slide_left)";
    case UI_ANIM_SLIDE_RIGHT:
        return "ui_anim_play(slide_right)";
    case UI_ANIM_FADE_IN:
        return "ui_anim_play(fade_in)";
    case UI_ANIM_FADE_OUT:
        return "ui_anim_play(fade_out)";
    case UI_ANIM_ZOOM_IN:
        return "ui_anim_play(zoom_in)";
    case UI_ANIM_ZOOM_OUT:
        return "ui_anim_play(zoom_out)";
    case UI_ANIM_STACK:
        return "ui_anim_play(stack)";
    default:
        return "ui_anim_play";
    }
}

void ui_anim_init(void) {
    slots = ui_mem_alloc(sizeof(slot_t) * SLOT_N, 4);
}

uint16_t ui_anim_duration_ms(ui_anim_id_t id) {
    if ((unsigned)id >= sizeof presets / sizeof presets[0]) {
        return 0;
    }
    return presets[id].dur;
}

void ui_anim_play_range(ui_anim_id_t id, ui_obj_t *obj, int16_t from, int16_t to) {
    const preset_t *p;
    int idx = -1;
    int free_i = -1;
    int i;
    slot_t *s;

    if (!slots || !obj || (unsigned)id >= sizeof presets / sizeof presets[0]) {
        return;
    }
    p = &presets[id];
    for (i = 0; i < SLOT_N; i++) {
        if (slots[i].obj == obj) {
            idx = i;
            break;
        }
        if (!slots[i].active && free_i < 0) {
            free_i = i;
        }
    }
    if (idx < 0) {
        idx = free_i >= 0 ? free_i : 0;
    }
    s = &slots[idx];
    s->obj = obj;
    s->t0 = hal_tick_ms();
    s->dur = p->dur;
    s->from = from;
    s->to = to;
    s->prop = p->prop;
    s->active = 1;
    apply(s, from);
    ui_trace_emit(anim_name(id));
}

void ui_anim_play(ui_anim_id_t id, ui_obj_t *obj) {
    if ((unsigned)id >= sizeof presets / sizeof presets[0]) {
        return;
    }
    ui_anim_play_range(id, obj, presets[id].from, presets[id].to);
}

int ui_anim_busy(const ui_obj_t *obj) {
    int i;

    if (!slots || !obj) {
        return 0;
    }
    for (i = 0; i < SLOT_N; i++) {
        if (slots[i].active && slots[i].obj == obj) {
            return 1;
        }
    }
    return 0;
}

void ui_anim_tick(uint32_t now_ms) {
    int i;

    if (!slots) {
        return;
    }
    for (i = 0; i < SLOT_N; i++) {
        slot_t *s = &slots[i];
        uint32_t elapsed;
        uint32_t t;
        int32_t value;

        if (!s->active || !s->obj) {
            continue;
        }
        elapsed = now_ms - s->t0;
        if (s->dur == 0 || elapsed >= s->dur) {
            apply(s, s->to);
            s->active = 0;
            s->obj = 0;
            continue;
        }
        t = elapsed * 256u / s->dur;
        value = (int32_t)s->from +
                ((int32_t)s->to - s->from) * (int32_t)ease_out(t) / 255;
        apply(s, (int16_t)value);
    }
}
