#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "hal.h"
#include "hal_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 把 8 行像素拼成 240x240，再放大画到窗口左半边。
   右半边交给 host/panel.c，那一段不进固件。 */

enum {
    FB_W = HAL_LCD_W,
    FB_H = HAL_LCD_H,
    VIEW = 4,
    PANEL_W = 360
};

static uint16_t fb[FB_W * FB_H];
static uint32_t *dib_bits;
static HBITMAP dib;
static HDC mem_dc;
static HWND hwnd;
static int have_window;
static volatile int running;
static uint8_t key_level;
static uint8_t test_keys;
static int use_virtual;
static uint32_t virtual_ms;
static DWORD frame_start;
static LARGE_INTEGER meter_t0;
static LARGE_INTEGER meter_freq;
static int meter_ready;
static uint32_t cpu_smooth;
static uint32_t cpu_shown;
static uint32_t cpu_shown_ms;
static int cpu_shown_ok;
static void (*painter)(void *hdc, int x, int panel_w, int height);
static int clock_override;
static hal_clock_t clock_base;
static uint32_t clock_base_ms;
enum { HOST_LINK_N = 128 };
static uint8_t link_rx[HOST_LINK_N];
static int link_rx_r;
static int link_rx_n;
static uint8_t link_tx[HOST_LINK_N];
static int link_tx_n;

static int month_days(int year, int month) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = days[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        n = 29;
    }
    return n;
}

static void add_seconds(hal_clock_t *t, uint32_t sec) {
    uint32_t s = t->second + sec;
    t->second = (uint8_t)(s % 60u);
    s = t->minute + s / 60u;
    t->minute = (uint8_t)(s % 60u);
    s = t->hour + s / 60u;
    t->hour = (uint8_t)(s % 24u);
    s = s / 24u;
    while (s > 0) {
        int md = month_days(t->year, t->month);
        int left = md - t->day;
        if (s <= (uint32_t)left) {
            t->day = (uint8_t)(t->day + s);
            break;
        }
        s -= (uint32_t)left + 1u;
        t->day = 1;
        t->month++;
        if (t->month > 12) {
            t->month = 1;
            t->year++;
        }
    }
}

static void convert_fb(void) {
    int i;

    if (!dib_bits) {
        return;
    }
    for (i = 0; i < FB_W * FB_H; i++) {
        uint16_t p = fb[i];
        uint32_t r = (p >> 11) & 31u;
        uint32_t g = (p >> 5) & 63u;
        uint32_t b = p & 31u;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        dib_bits[i] = (r << 16) | (g << 8) | b;
    }
}

static void paint_window(HDC hdc) {
    HGDIOBJ old;

    convert_fb();
    if (mem_dc && dib) {
        old = SelectObject(mem_dc, dib);
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchBlt(hdc, 0, 0, FB_W * VIEW, FB_H * VIEW,
                   mem_dc, 0, 0, FB_W, FB_H, SRCCOPY);
        SelectObject(mem_dc, old);
    }
    if (painter) {
        painter(hdc, FB_W * VIEW, PANEL_W, FB_H * VIEW);
    }
}

static LRESULT CALLBACK wnd_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(window, &ps);
        paint_window(hdc);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (lp & (1 << 30)) {
            return 0;
        }
        if (wp == VK_LEFT || wp == '1') {
            key_level |= HAL_KEY1;
        }
        if (wp == VK_RETURN || wp == VK_SPACE || wp == '2') {
            key_level |= HAL_KEY2;
        }
        if (wp == VK_RIGHT || wp == '3') {
            key_level |= HAL_KEY3;
        }
        if (wp == VK_ESCAPE) {
            DestroyWindow(window);
        }
        return 0;
    case WM_KEYUP:
        if (wp == VK_LEFT || wp == '1') {
            key_level = (uint8_t)(key_level & (0xFFu ^ HAL_KEY1));
        }
        if (wp == VK_RETURN || wp == VK_SPACE || wp == '2') {
            key_level = (uint8_t)(key_level & (0xFFu ^ HAL_KEY2));
        }
        if (wp == VK_RIGHT || wp == '3') {
            key_level = (uint8_t)(key_level & (0xFFu ^ HAL_KEY3));
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        running = 0;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, msg, wp, lp);
    }
}

static int create_window(void) {
    WNDCLASSW wc;
    RECT rect;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    BITMAPINFO info;
    HDC screen;

    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WatchUiHost";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 0;
    }

    rect.left = 0;
    rect.top = 0;
    rect.right = FB_W * VIEW + PANEL_W;
    rect.bottom = FB_H * VIEW;
    AdjustWindowRect(&rect, style, FALSE);

    hwnd = CreateWindowW(wc.lpszClassName, L"手表界面抽象层",
                         style, 40, 30,
                         rect.right - rect.left, rect.bottom - rect.top,
                         NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        return 0;
    }

    screen = GetDC(hwnd);
    mem_dc = CreateCompatibleDC(screen);
    ZeroMemory(&info, sizeof info);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = FB_W;
    info.bmiHeader.biHeight = -FB_H;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&dib_bits, NULL, 0);
    ReleaseDC(hwnd, screen);
    if (!dib) {
        return 0;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    have_window = 1;
    running = 1;
    timeBeginPeriod(1);
    return 1;
}

void hal_host_init(int create_window_flag) {
    int i;

    QueryPerformanceFrequency(&meter_freq);
    meter_ready = meter_freq.QuadPart != 0;
    frame_start = GetTickCount();
    for (i = 0; i < FB_W * FB_H; i++) {
        fb[i] = 0;
    }
    if (create_window_flag) {
        SetProcessDPIAware();
        if (!create_window()) {
            fprintf(stderr, "window failed\n");
        }
    }
}

void hal_host_shutdown(void) {
    if (have_window) {
        timeEndPeriod(1);
        have_window = 0;
    }
}

void hal_host_use_virtual_time(void) {
    use_virtual = 1;
    virtual_ms = 0;
}

void hal_host_set_keys(uint8_t level) {
    test_keys = level;
}

uint16_t hal_host_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) {
        return 0;
    }
    return fb[y * FB_W + x];
}

int hal_host_save_bmp(const char *path) {
    const int scale = 3;
    int width = FB_W * scale;
    int height = FB_H * scale;
    int stride = (width * 3 + 3) & ~3;
    uint8_t *pixels;
    FILE *fp;
    uint8_t header[54];
    uint32_t file_size;
    int y, x, sy, sx;

    if (!path) {
        return 0;
    }
    pixels = (uint8_t *)calloc((size_t)stride * (size_t)height, 1);
    if (!pixels) {
        return 0;
    }
    for (y = 0; y < FB_H; y++) {
        for (x = 0; x < FB_W; x++) {
            uint16_t p = fb[y * FB_W + x];
            uint8_t r = (uint8_t)(((p >> 11) & 31) << 3);
            uint8_t g = (uint8_t)(((p >> 5) & 63) << 2);
            uint8_t b = (uint8_t)((p & 31) << 3);
            for (sy = 0; sy < scale; sy++) {
                for (sx = 0; sx < scale; sx++) {
                    int py = height - 1 - (y * scale + sy);
                    uint8_t *dst = pixels + py * stride + (x * scale + sx) * 3;
                    dst[0] = b;
                    dst[1] = g;
                    dst[2] = r;
                }
            }
        }
    }
    fp = fopen(path, "wb");
    if (!fp) {
        free(pixels);
        return 0;
    }
    file_size = 54u + (uint32_t)stride * (uint32_t)height;
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (uint8_t)file_size;
    header[3] = (uint8_t)(file_size >> 8);
    header[4] = (uint8_t)(file_size >> 16);
    header[5] = (uint8_t)(file_size >> 24);
    header[10] = 54;
    header[14] = 40;
    header[18] = (uint8_t)width;
    header[19] = (uint8_t)(width >> 8);
    header[22] = (uint8_t)height;
    header[23] = (uint8_t)(height >> 8);
    header[26] = 1;
    header[28] = 24;
    fwrite(header, 1, 54, fp);
    fwrite(pixels, 1, (size_t)stride * (size_t)height, fp);
    fclose(fp);
    free(pixels);
    return 1;
}

int hal_host_running(void) {
    return running;
}

void hal_host_pump(void) {
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            running = 0;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void hal_host_set_painter(void (*fn)(void *hdc, int x, int panel_w, int height)) {
    painter = fn;
}

uint32_t hal_tick_ms(void) {
    if (use_virtual) {
        return virtual_ms;
    }
    return GetTickCount();
}

void hal_clock_set(const hal_clock_t *in) {
    if (!in) {
        return;
    }
    clock_base = *in;
    clock_base_ms = hal_tick_ms();
    clock_override = 1;
}

void hal_clock_get(hal_clock_t *out) {
    SYSTEMTIME st;

    if (!out) {
        return;
    }
    if (clock_override) {
        uint32_t now = hal_tick_ms();
        uint32_t elapsed = now - clock_base_ms;
        *out = clock_base;
        add_seconds(out, elapsed / 1000u);
        return;
    }
    GetLocalTime(&st);
    out->year = st.wYear;
    out->month = (uint8_t)st.wMonth;
    out->day = (uint8_t)st.wDay;
    out->hour = (uint8_t)st.wHour;
    out->minute = (uint8_t)st.wMinute;
    out->second = (uint8_t)st.wSecond;
}

int hal_link_read(uint8_t *out) {
    if (!out || link_rx_n <= 0) {
        return 0;
    }
    *out = link_rx[link_rx_r];
    link_rx_r = (link_rx_r + 1) % HOST_LINK_N;
    link_rx_n--;
    return 1;
}

void hal_link_write(const uint8_t *data, int len) {
    int i;

    if (!data || len <= 0) {
        return;
    }
    for (i = 0; i < len && link_tx_n < HOST_LINK_N; i++) {
        link_tx[link_tx_n++] = data[i];
    }
}

void hal_host_link_rx(const uint8_t *data, int len) {
    int i;

    if (!data || len <= 0) {
        return;
    }
    for (i = 0; i < len && link_rx_n < HOST_LINK_N; i++) {
        int w = (link_rx_r + link_rx_n) % HOST_LINK_N;
        link_rx[w] = data[i];
        link_rx_n++;
    }
}

void hal_host_link_tx_take(uint8_t *dst, int cap, int *out_n) {
    int n = link_tx_n;
    int i;

    if (n > cap) {
        n = cap;
    }
    for (i = 0; i < n; i++) {
        dst[i] = link_tx[i];
    }
    link_tx_n = 0;
    if (out_n) {
        *out_n = n;
    }
}

uint8_t hal_key_level(void) {
    if (use_virtual) {
        return test_keys;
    }
    return key_level;
}

void hal_display_flush(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint16_t *pixels) {
    int row;

    if (!pixels || w <= 0 || h <= 0) {
        return;
    }
    for (row = 0; row < h; row++) {
        int dy = y + row;
        int col;
        if (dy < 0 || dy >= FB_H) {
            continue;
        }
        for (col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= FB_W) {
                continue;
            }
            fb[dy * FB_W + dx] = pixels[row * w + col];
        }
    }
    if (have_window && y + h >= FB_H) {
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
    }
}

int hal_display_busy(void) {
    return 0;
}

void hal_meter_begin(void) {
    if (!meter_ready) {
        QueryPerformanceFrequency(&meter_freq);
        meter_ready = meter_freq.QuadPart != 0;
    }
    QueryPerformanceCounter(&meter_t0);
}

void hal_meter_end(void) {
    LARGE_INTEGER t1;
    uint64_t ticks;
    uint64_t us;
    uint32_t tenths;

    if (!meter_ready || meter_freq.QuadPart == 0) {
        return;
    }
    QueryPerformanceCounter(&t1);
    ticks = (uint64_t)(t1.QuadPart - meter_t0.QuadPart);
    us = ticks * 1000000ull / (uint64_t)meter_freq.QuadPart;
    tenths = (uint32_t)(us * 1000ull / 16667ull);
    if (tenths > 1000) {
        tenths = 1000;
    }
    cpu_smooth = (cpu_smooth * 3u + tenths) / 4u;
    {
        uint32_t now = hal_tick_ms();
        if (!cpu_shown_ok || now - cpu_shown_ms >= 500u) {
            cpu_shown = cpu_smooth;
            cpu_shown_ms = now;
            cpu_shown_ok = 1;
        }
    }
}

uint16_t hal_cpu_tenths(void) {
    return (uint16_t)cpu_shown;
}

void hal_frame_pace(void) {
    DWORD now;
    DWORD elapsed;

    if (use_virtual) {
        virtual_ms += 16;
        return;
    }
    now = GetTickCount();
    elapsed = now - frame_start;
    if (elapsed < 16) {
        Sleep(16 - elapsed);
    }
    frame_start = GetTickCount();
}
