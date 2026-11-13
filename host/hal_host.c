#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "hal.h"
#include "hal_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Assemble dirty rectangles into a persistent framebuffer. Only the host
   owns a full frame; firmware uses borrowed asynchronous band buffers. */

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
static uint64_t virtual_us;
static uint32_t frame_step_us = 16000;
static uint32_t async_delay_us;
static struct {
    const uint16_t *pixels;
    uint16_t snapshot[FB_W * FB_H];
    int x, y, w, h, corrupted;
    uint32_t due_us;
} transfer;
static hal_host_display_stats_t display_stats;
static const uint16_t *last_pixels;
static void finish_transfer(void);
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
static uint8_t link_tx[FB_W * FB_H * 2 + 4096];
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
    use_virtual = 0;
    virtual_us = 0;
    frame_step_us = 16000;
    async_delay_us = 0;
    transfer.pixels = NULL;
    key_level = test_keys = 0;
    clock_override = 0;
    cpu_smooth = cpu_shown = cpu_shown_ms = 0;
    cpu_shown_ok = 0;
    hal_host_link_clear();
    hal_host_display_reset_stats();
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
    while (hal_display_busy()) {
        hal_idle();
    }
    if (have_window) {
        timeEndPeriod(1);
        have_window = 0;
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
    }
    if (dib) DeleteObject(dib);
    if (mem_dc) DeleteDC(mem_dc);
    dib = NULL;
    mem_dc = NULL;
    dib_bits = NULL;
    hwnd = NULL;
}

void hal_host_use_virtual_time(void) {
    use_virtual = 1;
    virtual_us = 0;
}

void hal_host_set_time_us(uint64_t us) {
    virtual_us = us;
}

void hal_host_advance_us(uint32_t us) {
    virtual_us += us;
    finish_transfer();
}

void hal_host_set_frame_step_us(uint32_t us) {
    frame_step_us = us;
}

void hal_host_set_async_delay_us(uint32_t us) {
    while (hal_display_busy()) hal_idle();
    async_delay_us = us;
}

void hal_host_display_reset_stats(void) {
    memset(&display_stats, 0, sizeof display_stats);
    display_stats.min_x = FB_W;
    display_stats.min_y = FB_H;
    last_pixels = NULL;
}

void hal_host_display_get_stats(hal_host_display_stats_t *out) {
    if (out) *out = display_stats;
}

void hal_host_copy_frame(uint16_t *out) {
    if (out) memcpy(out, fb, sizeof fb);
}

void hal_host_link_clear(void) {
    link_rx_r = link_rx_n = link_tx_n = 0;
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
        return (uint32_t)(virtual_us / 1000u);
    }
    return GetTickCount();
}

uint32_t hal_tick_us(void) {
    LARGE_INTEGER now;
    if (use_virtual) return (uint32_t)virtual_us;
    if (!meter_ready) {
        QueryPerformanceFrequency(&meter_freq);
        meter_ready = meter_freq.QuadPart != 0;
    }
    if (!meter_ready) return GetTickCount() * 1000u;
    QueryPerformanceCounter(&now);
    return (uint32_t)(((uint64_t)now.QuadPart / (uint64_t)meter_freq.QuadPart) * 1000000ull +
                     ((uint64_t)now.QuadPart % (uint64_t)meter_freq.QuadPart) * 1000000ull /
                     (uint64_t)meter_freq.QuadPart);
}

void hal_idle(void) {
    if (use_virtual) virtual_us += 50;
    else Sleep(0);
    finish_transfer();
    if (have_window) hal_host_pump();
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
    for (i = 0; i < len && link_tx_n < (int)sizeof link_tx; i++) {
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

static void copy_region(int x, int y, int w, int h, const uint16_t *pixels) {
    int row, col;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            int dx = x + col, dy = y + row;
            if (dx >= 0 && dx < FB_W && dy >= 0 && dy < FB_H)
                fb[dy * FB_W + dx] = pixels[row * w + col];
        }
    }
    /* Coalesce repaint requests; dirty updates need not reach the bottom. */
    if (have_window) InvalidateRect(hwnd, NULL, FALSE);
}

static void finish_transfer(void) {
    if (!transfer.pixels) return;
    if (!transfer.corrupted && memcmp(transfer.pixels, transfer.snapshot,
            (size_t)transfer.w * transfer.h * sizeof(uint16_t)) != 0) {
        transfer.corrupted = 1;
        display_stats.overwrites++;
    }
    if ((int32_t)(hal_tick_us() - transfer.due_us) < 0) return;
    /* Read the BORROWED memory on completion, never the safety snapshot. */
    copy_region(transfer.x, transfer.y, transfer.w, transfer.h, transfer.pixels);
    transfer.pixels = NULL;
    display_stats.completed++;
}

void hal_display_flush(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint16_t *pixels) {
    if (transfer.pixels) {
        display_stats.submit_while_busy++;
        return;
    }
    if (!pixels || w <= 0 || h <= 0 || (uint32_t)w * h > FB_W * FB_H) {
        display_stats.invalid_regions++;
        return;
    }
    if (x < 0 || y < 0 || x + w > FB_W || y + h > FB_H)
        display_stats.invalid_regions++;
    display_stats.calls++;
    display_stats.pixels += (uint32_t)w * h;
    if (last_pixels && last_pixels != pixels) display_stats.pointer_changes++;
    last_pixels = pixels;
    if (x < display_stats.min_x) display_stats.min_x = x;
    if (y < display_stats.min_y) display_stats.min_y = y;
    if (x + w > display_stats.max_x) display_stats.max_x = x + w;
    if (y + h > display_stats.max_y) display_stats.max_y = y + h;
    if (w > display_stats.max_w) display_stats.max_w = w;
    if (h > display_stats.max_h) display_stats.max_h = h;
    if (!async_delay_us) {
        copy_region(x, y, w, h, pixels);
        display_stats.completed++;
        return;
    }
    transfer.x = x;
    transfer.y = y;
    transfer.w = w;
    transfer.h = h;
    transfer.corrupted = 0;
    memcpy(transfer.snapshot, pixels, (size_t)w * h * sizeof(uint16_t));
    transfer.due_us = hal_tick_us() + async_delay_us;
    transfer.pixels = pixels;
}

int hal_display_busy(void) {
    display_stats.busy_polls++;
    /* Deterministic progress also catches legacy spin-waits without idle. */
    if (use_virtual && transfer.pixels) virtual_us += 25;
    finish_transfer();
    return transfer.pixels != NULL;
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
        hal_host_advance_us(frame_step_us);
        return;
    }
    now = GetTickCount();
    elapsed = now - frame_start;
    if (elapsed < 16) {
        Sleep(16 - elapsed);
    }
    frame_start = GetTickCount();
}

/* ---- 传感器/执行器仿真 ----
   给可移植 UI 核心提供假数据，让计步/音乐/设置/关于能在电脑上整机验证。 */
static int sim_walking = 1;
static uint32_t sim_raise_ms;
static uint16_t sim_beep_freq;
static uint16_t sim_beep_ms;
static int sim_led;
static uint8_t sim_backlight = 200;

void hal_host_set_walking(int on) {
    sim_walking = on ? 1 : 0;
}

void hal_host_raise_wrist(void) {
    sim_raise_ms = hal_tick_ms();
    if (sim_raise_ms == 0) {
        sim_raise_ms = 1;
    }
}

int hal_imu_read(hal_imu_t *out) {
    uint32_t t = hal_tick_ms();
    uint32_t phase;
    int swing;

    if (!out) {
        return 0;
    }
    if (sim_walking) {
        /* 2Hz 步伐：竖直轴 900..1300mg 三角冲击，峰在每 500ms 中点。 */
        phase = t % 500u;
        swing = (int)((phase < 250u ? phase : 500u - phase) * 400u / 250u);
        out->az = (int16_t)(900 + swing);
        out->ax = (int16_t)((int)(t % 7u) * 4 - 12);
        out->ay = (int16_t)((int)(t % 5u) * 3 - 6);
        out->gx = 0;
        out->gy = 0;
        out->gz = (int16_t)(swing - 200);
    } else {
        out->ax = 0;
        out->ay = 0;
        out->az = 1000;
        out->gx = out->gy = out->gz = 0;
    }
    if (sim_raise_ms && (t - sim_raise_ms) < 400u) {
        out->gy = 1500;   /* 抬腕：俯仰角速度突发 */
        out->az = 1300;
    }
    out->temp = 280;
    return 1;
}

int hal_mag_read(hal_mag_t *out) {
    if (!out) {
        return 0;
    }
    out->x = 200;
    out->y = -150;
    out->z = 400;
    return 1;
}

int hal_baro_read(hal_baro_t *out) {
    if (!out) {
        return 0;
    }
    out->pressure = 101325;
    out->temp = 250;
    return 1;
}

uint16_t hal_battery_mv(void) {
    return 4020;
}

uint8_t hal_battery_pct(void) {
    return 80;
}

int hal_i2c_scan(uint8_t *out, int max) {
    static const uint8_t ids[] = {0x68, 0x0D, 0x46};
    int n = 0;

    if (!out || max <= 0) {
        return 0;
    }
    while (n < max && n < (int)sizeof ids) {
        out[n] = ids[n];
        n++;
    }
    return n;
}

const char *hal_mag_name(void) {
    return "QMC5883P";
}

int hal_debug_snapshot(char *out, int cap) {
    if (!out || cap <= 0) {
        return 0;
    }
    return snprintf(out, (size_t)cap,
                    "I2C=2C,68;MPU=1;QMC5883P@2C;ID=80;ST=01;C1=55;C2=08;"
                    "M=20,-15,40;BAT=4020");
}

int hal_ble_connected(void) {
    return 1;
}

void hal_beep(uint16_t freq_hz, uint16_t ms) {
    sim_beep_freq = freq_hz;
    sim_beep_ms = ms;
}

void hal_led_set(int on) {
    sim_led = on ? 1 : 0;
}

void hal_led_mode(int mode) {
    sim_led = mode ? 1 : 0;
}

void hal_backlight_set(uint8_t level) {
    sim_backlight = level;
}

const char *hal_fw_version(void) {
    return "1.0.0";
}

const char *hal_fw_build(void) {
    return __DATE__ " " __TIME__;
}

const char *hal_mcu_name(void) {
    return "AT32F421K8T7";
}

uint32_t hal_sysclk_mhz(void) {
    return 120;
}

const char *hal_clock_src(void) {
    return "120MHz HEXT";
}
