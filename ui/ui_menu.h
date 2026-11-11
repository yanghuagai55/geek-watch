#ifndef UI_MENU_H
#define UI_MENU_H

#include "ui.h"

/* 设置、消息这一类应用共用的页面模板。
   第 0 级是外面的图标。
   第 1 级是点进去之后的列表。
   第 2 级是从列表再盖上来的子页。 */

enum {
    UI_MENU_ICON = 0,
    UI_MENU_LIST = 1,
    UI_MENU_CHILD = 2
};

enum {
    UI_MENU_IGNORED = 0,
    UI_MENU_OPENED = 1,
    UI_MENU_ROW = 2
};

typedef struct ui_menu {
    int level;
    int focus;
    int count;
    ui_obj_t scroll;
    int child;
    int child_focus;
    int child_count;
    ui_obj_t child_scroll;
    ui_obj_t zoom;
    ui_obj_t push;
} ui_menu_t;

void ui_menu_init(ui_menu_t *menu, int list_count);
int ui_menu_level(const ui_menu_t *menu);
void ui_menu_tick(ui_menu_t *menu);

int ui_menu_prev(ui_menu_t *menu);
int ui_menu_next(ui_menu_t *menu);
int ui_menu_confirm(ui_menu_t *menu);
void ui_menu_back(ui_menu_t *menu);
int ui_menu_enter_child(ui_menu_t *menu, int child_count);

#endif
