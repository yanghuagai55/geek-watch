#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "panel.h"
#include "ui.h"

#include <stdio.h>

/* 右侧说明只用系统字体，方便阅读。左边手表上的字是点阵，那一份才会进固件。 */

enum { TRACE_N = 8, TRACE_LEN = 80 };

static char trace_lines[TRACE_N][TRACE_LEN];
static int trace_count;
static HFONT font_title;
static HFONT font_body;
static HBRUSH panel_brush;

void host_panel_trace(const char *message) {
    int slot;

    if (!message) {
        return;
    }
    slot = trace_count % TRACE_N;
    snprintf(trace_lines[slot], TRACE_LEN, "%s", message);
    trace_count++;
}

static void text_w(HDC hdc, int x, int y, const wchar_t *s) {
    TextOutW(hdc, x, y, s, lstrlenW(s));
}

static void ensure_fonts(void) {
    if (font_body) {
        return;
    }
    font_title = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI");
    font_body = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            L"Microsoft YaHei UI");
}

void host_panel_paint(void *hdc_ptr, int x, int panel_w, int height) {
    HDC hdc = (HDC)hdc_ptr;
    RECT rect;
    HGDIOBJ old_font;
    int y = 16;
    int i;
    int shown;
    int start;
    uint32_t used = 0;
    uint32_t total = 0;
    uint16_t cpu;
    wchar_t line[96];

    ensure_fonts();
    rect.left = x;
    rect.top = 0;
    rect.right = x + panel_w;
    rect.bottom = height;
    if (!panel_brush) {
        panel_brush = CreateSolidBrush(RGB(16, 20, 28));
    }
    SetBkMode(hdc, TRANSPARENT);
    FillRect(hdc, &rect, panel_brush);
    IntersectClipRect(hdc, x, 0, x + panel_w, height);

    old_font = SelectObject(hdc, font_title);
    SetTextColor(hdc, RGB(236, 240, 244));
    text_w(hdc, x + 16, y, L"封装好的函数");
    y += 32;
    SelectObject(hdc, font_body);
    SetTextColor(hdc, RGB(150, 162, 176));
    text_w(hdc, x + 16, y, L"左边是固件画的表盘，右边不进单片机。");
    y += 28;

    SetTextColor(hdc, RGB(70, 220, 130));
    text_w(hdc, x + 16, y, L"主循环");
    y += 20;
    SetTextColor(hdc, RGB(236, 240, 244));
    text_w(hdc, x + 16, y, L"ui_init()    ui_frame()");
    y += 26;

    SetTextColor(hdc, RGB(70, 220, 130));
    text_w(hdc, x + 16, y, L"页面和动画");
    y += 20;
    SetTextColor(hdc, RGB(236, 240, 244));
    text_w(hdc, x + 16, y, L"ui_nav_goto(页面)");
    y += 18;
    text_w(hdc, x + 16, y, L"ui_anim_play(预设, 对象)");
    y += 18;
    text_w(hdc, x + 16, y, L"ui_svg_draw()  ui_image_draw()");
    y += 18;
    text_w(hdc, x + 16, y, L"ui_text_draw()  插在图片下面");
    y += 18;
    text_w(hdc, x + 16, y, L"ui_song_draw()  华文琥珀");
    y += 18;
    text_w(hdc, x + 16, y, L"link_media_toggle()  蓝牙播放");
    y += 26;

    SetTextColor(hdc, RGB(70, 220, 130));
    text_w(hdc, x + 16, y, L"调试");
    y += 20;
    SetTextColor(hdc, RGB(236, 240, 244));
    text_w(hdc, x + 16, y, L"ui_settings_set_debug(0/1)");
    y += 18;
    SetTextColor(hdc, RGB(150, 162, 176));
    text_w(hdc, x + 16, y, L"打开后，左下角一直显示");
    y += 26;

    SetTextColor(hdc, RGB(70, 220, 130));
    text_w(hdc, x + 16, y, L"换芯片只改这些");
    y += 20;
    SetTextColor(hdc, RGB(236, 240, 244));
    text_w(hdc, x + 16, y, L"hal_display_flush()  双缓冲 / 4 行");
    y += 18;
    text_w(hdc, x + 16, y, L"hal_tick_ms()  hal_key_level()");
    y += 18;
    text_w(hdc, x + 16, y, L"hal_cpu_tenths()  hal_clock_get()");
    y += 26;

    SetTextColor(hdc, RGB(70, 220, 130));
    text_w(hdc, x + 16, y, L"按键");
    y += 20;
    SetTextColor(hdc, RGB(236, 240, 244));
    text_w(hdc, x + 16, y, L"左键 上一页 / 返回");
    y += 18;
    text_w(hdc, x + 16, y, L"右键 下一页    回车 进入");
    y += 18;
    SetTextColor(hdc, RGB(150, 162, 176));
    text_w(hdc, x + 16, y, L"长按回车返回。表盘不显示圆点");
    y += 26;

    ui_mem_get(&used, &total);
    cpu = hal_cpu_tenths();
    SetTextColor(hdc, RGB(70, 220, 130));
    text_w(hdc, x + 16, y, L"和左下角是同一组数");
    y += 20;
    SetTextColor(hdc, RGB(236, 240, 244));
    swprintf(line, 96, L"RAM %lu / %lu", (unsigned long)used, (unsigned long)total);
    text_w(hdc, x + 16, y, line);
    y += 18;
    swprintf(line, 96, L"CPU %u.%u%%", cpu / 10u, cpu % 10u);
    text_w(hdc, x + 16, y, line);
    y += 18;
    SetTextColor(hdc, RGB(150, 162, 176));
    text_w(hdc, x + 16, y, L"CPU 每 0.5 秒才变一次");
    y += 24;

    SetTextColor(hdc, RGB(70, 220, 130));
    text_w(hdc, x + 16, y, L"刚才调用了");
    y += 20;
    SetTextColor(hdc, RGB(236, 240, 244));
    shown = trace_count < TRACE_N ? trace_count : TRACE_N;
    start = trace_count < TRACE_N ? 0 : trace_count % TRACE_N;
    if (shown == 0) {
        SetTextColor(hdc, RGB(150, 162, 176));
        text_w(hdc, x + 16, y, L"还没有。回车进入设置。");
    }
    for (i = 0; i < shown; i++) {
        wchar_t wide[TRACE_LEN];
        const char *src = trace_lines[(start + i) % TRACE_N];
        MultiByteToWideChar(CP_UTF8, 0, src, -1, wide, TRACE_LEN);
        text_w(hdc, x + 16, y, wide);
        y += 18;
    }
    SelectObject(hdc, old_font);
}
