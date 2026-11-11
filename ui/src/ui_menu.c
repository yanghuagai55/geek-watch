#include "ui_menu.h"

static int move_focus(ui_obj_t *scroll, int *focus, int dir, int count) {
    int next;

    if (count <= 0) {
        return 0;
    }
    next = *focus + dir;
    if (next < 0 || next >= count) {
        return 0;
    }
    ui_anim_play_range(UI_ANIM_STACK, scroll, scroll->x, (int16_t)(next * 256));
    *focus = next;
    return 1;
}

void ui_menu_init(ui_menu_t *menu, int list_count) {
    if (!menu) {
        return;
    }
    menu->level = UI_MENU_ICON;
    menu->focus = 0;
    menu->count = list_count;
    menu->child = 0;
    menu->child_focus = 0;
    menu->child_count = 0;
}

int ui_menu_level(const ui_menu_t *menu) {
    return menu ? menu->level : UI_MENU_ICON;
}

void ui_menu_tick(ui_menu_t *menu) {
    if (!menu) {
        return;
    }
    if (menu->level == UI_MENU_CHILD && menu->push.x <= 0 &&
        !ui_anim_busy(&menu->push)) {
        menu->level = UI_MENU_LIST;
    }
    if (menu->level == UI_MENU_LIST && menu->zoom.scale == 0 &&
        !ui_anim_busy(&menu->zoom)) {
        menu->level = UI_MENU_ICON;
    }
}

void ui_menu_back(ui_menu_t *menu) {
    if (!menu || menu->level == UI_MENU_ICON) {
        return;
    }
    if (menu->level == UI_MENU_CHILD) {
        ui_anim_play_range(UI_ANIM_STACK, &menu->push, menu->push.x, 0);
        return;
    }
    ui_anim_play_range(UI_ANIM_ZOOM_OUT, &menu->zoom, (int16_t)menu->zoom.scale, 0);
}

int ui_menu_prev(ui_menu_t *menu) {
    if (!menu || menu->level == UI_MENU_ICON) {
        return 0;
    }
    if (menu->level == UI_MENU_CHILD) {
        if (!move_focus(&menu->child_scroll, &menu->child_focus, -1, menu->child_count)) {
            ui_menu_back(menu);
        }
        return 1;
    }
    if (menu->zoom.scale < 255 || ui_anim_busy(&menu->zoom)) {
        ui_menu_back(menu);
        return 1;
    }
    if (!move_focus(&menu->scroll, &menu->focus, -1, menu->count)) {
        ui_menu_back(menu);
    }
    return 1;
}

int ui_menu_next(ui_menu_t *menu) {
    if (!menu || menu->level == UI_MENU_ICON) {
        return 0;
    }
    if (menu->level == UI_MENU_CHILD) {
        move_focus(&menu->child_scroll, &menu->child_focus, 1, menu->child_count);
        return 1;
    }
    if (menu->zoom.scale < 255 || ui_anim_busy(&menu->zoom)) {
        return 1;
    }
    move_focus(&menu->scroll, &menu->focus, 1, menu->count);
    return 1;
}

int ui_menu_confirm(ui_menu_t *menu) {
    if (!menu || menu->level == UI_MENU_CHILD) {
        return UI_MENU_IGNORED;
    }
    if (menu->level == UI_MENU_ICON) {
        menu->level = UI_MENU_LIST;
        menu->focus = 0;
        menu->scroll.x = 0;
        ui_anim_play(UI_ANIM_ZOOM_IN, &menu->zoom);
        return UI_MENU_OPENED;
    }
    if (menu->zoom.scale < 255 || ui_anim_busy(&menu->zoom)) {
        return UI_MENU_IGNORED;
    }
    return UI_MENU_ROW;
}

int ui_menu_enter_child(ui_menu_t *menu, int child_count) {
    if (!menu || menu->level != UI_MENU_LIST) {
        return 0;
    }
    if (menu->zoom.scale < 255 || ui_anim_busy(&menu->zoom)) {
        return 0;
    }
    menu->child = menu->focus;
    menu->child_focus = 0;
    menu->child_count = child_count;
    menu->child_scroll.x = 0;
    menu->level = UI_MENU_CHILD;
    ui_anim_play_range(UI_ANIM_STACK, &menu->push, menu->push.x, 256);
    return 1;
}
