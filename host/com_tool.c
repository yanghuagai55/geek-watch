/* 连上手表串口：校时、调试模式下的三个按键、消息、截取画面。
   帧和固件一致：A5 命令 长度 数据 异或。
   截取画面发 06，收回 A5 11 FF、3 字节长度、宽高和 RGB565，存 host\obj\shot_live.bmp。
   无窗口：com_tool.exe shot
   先开调试、把「你好」发到消息 1，再翻到消息页：com_tool.exe shot scene */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <stdint.h>

enum {
    ID_COM = 100,
    ID_CONNECT,
    ID_DEBUG,
    ID_KEY1,
    ID_KEY2,
    ID_KEY3,
    ID_YEAR,
    ID_MONTH,
    ID_DAY,
    ID_HOUR,
    ID_MIN,
    ID_SEC,
    ID_NOW,
    ID_TIME,
    ID_SLOT,
    ID_MSG,
    ID_SENDMSG,
    ID_SHOT,
    ID_LOG
};

static HANDLE port = INVALID_HANDLE_VALUE;
static HWND wnd;
static HWND log_box;
static WNDPROC key_orig[3];
static int key_bit[3] = {1, 2, 4};
static char rx_acc[96];
static int rx_acc_n;

static void log_line(const wchar_t *text) {
    int n;

    if (!log_box) {
        return;
    }
    n = GetWindowTextLengthW(log_box);
    SendMessageW(log_box, EM_SETSEL, (WPARAM)n, (LPARAM)n);
    SendMessageW(log_box, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageW(log_box, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
}

static int send_frame(uint8_t cmd, const uint8_t *pay, int n) {
    uint8_t buf[40];
    DWORD wrote = 0;
    int i;
    uint8_t sum;

    if (port == INVALID_HANDLE_VALUE || n < 0 || n > 28) {
        log_line(L"还没连接");
        return 0;
    }
    buf[0] = 0xA5;
    buf[1] = cmd;
    buf[2] = (uint8_t)n;
    sum = (uint8_t)(buf[0] ^ buf[1] ^ buf[2]);
    for (i = 0; i < n; i++) {
        buf[3 + i] = pay[i];
        sum ^= pay[i];
    }
    buf[3 + n] = sum;
    if (!WriteFile(port, buf, (DWORD)(4 + n), &wrote, NULL) || wrote != (DWORD)(4 + n)) {
        log_line(L"发送失败");
        return 0;
    }
    return 1;
}

static void send_keys(void) {
    uint8_t mask = 0;
    int i;

    for (i = 0; i < 3; i++) {
        if (GetCapture() == GetDlgItem(wnd, ID_KEY1 + i)) {
            mask = (uint8_t)(mask | key_bit[i]);
        }
    }
    send_frame(0x07, &mask, 1);
}

static LRESULT CALLBACK key_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int id = GetDlgCtrlID(hwnd);
    int index = id - ID_KEY1;
    WNDPROC orig = (index >= 0 && index < 3) ? key_orig[index] : NULL;

    if (msg == WM_LBUTTONDOWN) {
        SetCapture(hwnd);
        send_keys();
    } else if (msg == WM_LBUTTONUP) {
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        send_keys();
    }
    return orig ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

static int read_num(int id, int fallback) {
    wchar_t buf[16];
    GetWindowTextW(GetDlgItem(wnd, id), buf, 16);
    if (buf[0] == 0) {
        return fallback;
    }
    return _wtoi(buf);
}

static void set_num(int id, int value) {
    wchar_t buf[16];
    wsprintfW(buf, L"%d", value);
    SetWindowTextW(GetDlgItem(wnd, id), buf);
}

static void fill_now(void) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    set_num(ID_YEAR, t.wYear);
    set_num(ID_MONTH, t.wMonth);
    set_num(ID_DAY, t.wDay);
    set_num(ID_HOUR, t.wHour);
    set_num(ID_MIN, t.wMinute);
    set_num(ID_SEC, t.wSecond);
}

static void send_time(void) {
    uint8_t pay[7];
    int year = read_num(ID_YEAR, 2026);
    pay[0] = (uint8_t)(year & 255);
    pay[1] = (uint8_t)((year >> 8) & 255);
    pay[2] = (uint8_t)read_num(ID_MONTH, 1);
    pay[3] = (uint8_t)read_num(ID_DAY, 1);
    pay[4] = (uint8_t)read_num(ID_HOUR, 0);
    pay[5] = (uint8_t)read_num(ID_MIN, 0);
    pay[6] = (uint8_t)read_num(ID_SEC, 0);
    if (send_frame(0x01, pay, 7)) {
        log_line(L"已发送校时");
    }
}

/* 固件每个槽最多存 23 字节。在码位边界截断，避免把一个汉字切成方框。 */
static int utf8_prefix(const uint8_t *s, int n, int cap) {
    int i = 0;

    if (n > cap) {
        n = cap;
    }
    while (i < n) {
        int need = 1;
        uint8_t b = s[i];

        if ((b & 0x80) == 0) {
            need = 1;
        } else if ((b & 0xE0) == 0xC0) {
            need = 2;
        } else if ((b & 0xF0) == 0xE0) {
            need = 3;
        } else if ((b & 0xF8) == 0xF0) {
            need = 4;
        } else {
            break;
        }
        if (i + need > n) {
            break;
        }
        i += need;
    }
    return i;
}

static void send_message(void) {
    wchar_t text[48];
    uint8_t raw[160];
    uint8_t pay[28];
    int n;
    int slot = (int)SendMessageW(GetDlgItem(wnd, ID_SLOT), CB_GETCURSEL, 0, 0);

    if (slot < 0) {
        slot = 0;
    }
    GetWindowTextW(GetDlgItem(wnd, ID_MSG), text, 48);
    n = WideCharToMultiByte(CP_UTF8, 0, text, -1, (char *)raw, (int)sizeof raw, NULL, NULL);
    if (n <= 1) {
        n = 0;
    } else {
        n = utf8_prefix(raw, n - 1, 23);
    }
    pay[0] = (uint8_t)slot;
    if (n > 0) {
        memcpy(pay + 1, raw, (size_t)n);
    }
    if (send_frame(0x02, pay, 1 + n)) {
        log_line(L"已发送消息");
    }
}

static void close_port(void) {
    if (wnd) {
        KillTimer(wnd, 1);
    }
    if (port != INVALID_HANDLE_VALUE) {
        CloseHandle(port);
        port = INVALID_HANDLE_VALUE;
    }
    rx_acc_n = 0;
    if (wnd && GetDlgItem(wnd, ID_CONNECT)) {
        SetWindowTextW(GetDlgItem(wnd, ID_CONNECT), L"连接");
    }
}

static void note_rx(void) {
    wchar_t wide[128];
    wchar_t line[140];

    rx_acc[rx_acc_n] = 0;
    if (MultiByteToWideChar(CP_UTF8, 0, rx_acc, -1, wide, 128) <= 0) {
        rx_acc_n = 0;
        return;
    }
    wsprintfW(line, L"收 %s", wide);
    log_line(line);
    rx_acc_n = 0;
}

static void pump_rx(void) {
    uint8_t buf[128];
    DWORD n = 0;
    DWORD i;

    if (port == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!ReadFile(port, buf, sizeof buf, &n, NULL) || n == 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        uint8_t b = buf[i];
        if (b == '\r') {
            continue;
        }
        if (b == '\n') {
            note_rx();
            continue;
        }
        if (rx_acc_n < (int)sizeof rx_acc - 1 && b >= 32 && b < 127) {
            rx_acc[rx_acc_n++] = (char)b;
        }
    }
}

static HANDLE open_com(const wchar_t *name) {
    wchar_t path[32];
    HANDLE h = INVALID_HANDLE_VALUE;
    DCB dcb;
    COMMTIMEOUTS to;
    int try_n;

    if (!name || name[0] == 0) {
        name = L"COM5";
    }
    wsprintfW(path, L"\\\\.\\%s", name);
    for (try_n = 0; try_n < 2; try_n++) {
        h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            break;
        }
        if (try_n == 0) {
            Sleep(400);
        }
    }
    if (h == INVALID_HANDLE_VALUE) {
        return h;
    }
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = 1;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    EscapeCommFunction(h, SETDTR);
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutConstant = 0;
    to.ReadTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 2000;
    to.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &to);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}

static int save_bmp565(const wchar_t *path, int w, int h, const uint8_t *rgb565) {
    FILE *fp;
    uint8_t header[54];
    uint8_t *line;
    int x, y;
    int row = (w * 3 + 3) & ~3;
    uint32_t file_size;

    CreateDirectoryW(L"host", NULL);
    CreateDirectoryW(L"host\\obj", NULL);
    line = (uint8_t *)malloc((size_t)row);
    if (!line) {
        return 0;
    }
    fp = _wfopen(path, L"wb");
    if (!fp) {
        free(line);
        return 0;
    }
    file_size = 54u + (uint32_t)row * (uint32_t)h;
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (uint8_t)file_size;
    header[3] = (uint8_t)(file_size >> 8);
    header[4] = (uint8_t)(file_size >> 16);
    header[5] = (uint8_t)(file_size >> 24);
    header[10] = 54;
    header[14] = 40;
    header[18] = (uint8_t)w;
    header[19] = (uint8_t)(w >> 8);
    header[22] = (uint8_t)h;
    header[23] = (uint8_t)(h >> 8);
    header[26] = 1;
    header[28] = 24;
    fwrite(header, 1, 54, fp);
    for (y = h - 1; y >= 0; y--) {
        memset(line, 0, (size_t)row);
        for (x = 0; x < w; x++) {
            int i = (y * w + x) * 2;
            unsigned int p = ((unsigned int)rgb565[i] << 8) | rgb565[i + 1];
            int r = (int)((p >> 11) & 31u);
            int g = (int)((p >> 5) & 63u);
            int b = (int)(p & 31u);
            line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
            line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
            line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
        }
        fwrite(line, 1, (size_t)row, fp);
    }
    fclose(fp);
    free(line);
    return 1;
}

static int pull_byte(HANDLE h, uint8_t *hold, int *n, int *at, uint8_t *out, DWORD t0) {
    while (*at >= *n) {
        DWORD nr = 0;

        if (GetTickCount() - t0 >= 8000) {
            return 0;
        }
        if (!ReadFile(h, hold, 1024, &nr, NULL)) {
            return 0;
        }
        if (nr == 0) {
            Sleep(2);
            continue;
        }
        *n = (int)nr;
        *at = 0;
    }
    *out = hold[*at];
    *at += 1;
    return 1;
}

static int recv_shot(HANDLE h, const wchar_t *path) {
    uint8_t hold[1024];
    uint8_t win[3] = {0, 0, 0};
    uint8_t lenb[3];
    uint8_t xor_got = 0;
    uint8_t *pix = 0;
    int n = 0;
    int at = 0;
    int ok = 0;
    DWORD t0 = GetTickCount();

    if (!h || h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    while (GetTickCount() - t0 < 8000) {
        uint8_t b;
        uint8_t xor_calc;
        uint32_t total;
        uint32_t k;
        int w;
        int ht;

        if (!pull_byte(h, hold, &n, &at, &b, t0)) {
            break;
        }
        win[0] = win[1];
        win[1] = win[2];
        win[2] = b;
        if (win[0] != 0xA5 || win[1] != 0x11 || win[2] != 0xFF) {
            continue;
        }
        if (!pull_byte(h, hold, &n, &at, &lenb[0], t0) ||
            !pull_byte(h, hold, &n, &at, &lenb[1], t0) ||
            !pull_byte(h, hold, &n, &at, &lenb[2], t0)) {
            break;
        }
        total = (uint32_t)lenb[0] | ((uint32_t)lenb[1] << 8) | ((uint32_t)lenb[2] << 16);
        if (total < 4 || total > 240u * 320u * 2u + 4u) {
            continue;
        }
        pix = (uint8_t *)malloc(total);
        if (!pix) {
            break;
        }
        xor_calc = (uint8_t)(0xA5 ^ 0x11 ^ 0xFF ^ lenb[0] ^ lenb[1] ^ lenb[2]);
        for (k = 0; k < total; k++) {
            if (!pull_byte(h, hold, &n, &at, &pix[k], t0)) {
                free(pix);
                pix = 0;
                break;
            }
            xor_calc ^= pix[k];
        }
        if (!pix || !pull_byte(h, hold, &n, &at, &xor_got, t0) || xor_got != xor_calc) {
            free(pix);
            pix = 0;
            if (GetTickCount() - t0 >= 8000) {
                break;
            }
            continue;
        }
        w = pix[0] | (pix[1] << 8);
        ht = pix[2] | (pix[3] << 8);
        if (w < 1 || ht < 1 || (uint32_t)w * (uint32_t)ht * 2u + 4u != total) {
            free(pix);
            pix = 0;
            continue;
        }
        ok = save_bmp565(path, w, ht, pix + 4);
        free(pix);
        break;
    }
    return ok;
}

static void request_shot(void) {
    int ok;

    if (port == INVALID_HANDLE_VALUE) {
        log_line(L"还没连接");
        return;
    }
    if (wnd) {
        KillTimer(wnd, 1);
    }
    if (!send_frame(0x06, NULL, 0)) {
        if (wnd) {
            SetTimer(wnd, 1, 40, NULL);
        }
        return;
    }
    ok = recv_shot(port, L"host\\obj\\shot_live.bmp");
    if (wnd) {
        SetTimer(wnd, 1, 40, NULL);
    }
    log_line(ok ? L"已保存 host\\obj\\shot_live.bmp" : L"没收到画面");
}

static void open_port(void) {
    wchar_t name[16];

    close_port();
    GetWindowTextW(GetDlgItem(wnd, ID_COM), name, 16);
    port = open_com(name);
    if (port == INVALID_HANDLE_VALUE) {
        log_line(L"打不开这个串口");
        return;
    }
    SetTimer(wnd, 1, 40, NULL);
    SetWindowTextW(GetDlgItem(wnd, ID_CONNECT), L"断开");
    log_line(L"已连接");
}

static HWND add_text(const wchar_t *text, int x, int y, int w, int h) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, wnd, NULL, NULL, NULL);
}

static HWND add_edit(int id, const wchar_t *text, int x, int y, int w, int h, DWORD extra) {
    return CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | extra,
                         x, y, w, h, wnd, (HMENU)(INT_PTR)id, NULL, NULL);
}

static HWND add_btn(int id, const wchar_t *text, int x, int y, int w, int h) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                         x, y, w, h, wnd, (HMENU)(INT_PTR)id, NULL, NULL);
}

static void build_ui(void) {
    HWND slot;
    HWND key;
    int i;

    add_text(L"串口", 12, 14, 36, 20);
    add_edit(ID_COM, L"COM5", 48, 12, 72, 22, 0);
    add_btn(ID_CONNECT, L"连接", 128, 11, 64, 24);
    CreateWindowW(L"BUTTON", L"调试模式", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  204, 12, 90, 22, wnd, (HMENU)(INT_PTR)ID_DEBUG, NULL, NULL);

    add_text(L"按键（先开调试模式，按住不放就是长按）", 12, 46, 280, 18);
    add_btn(ID_KEY1, L"上一项", 12, 68, 88, 32);
    add_btn(ID_KEY2, L"确定", 108, 68, 88, 32);
    add_btn(ID_KEY3, L"下一项", 204, 68, 88, 32);
    for (i = 0; i < 3; i++) {
        key = GetDlgItem(wnd, ID_KEY1 + i);
        key_orig[i] = (WNDPROC)SetWindowLongPtrW(key, GWLP_WNDPROC, (LONG_PTR)key_proc);
    }

    add_text(L"校时", 12, 114, 40, 18);
    add_edit(ID_YEAR, L"2026", 12, 136, 52, 22, ES_NUMBER);
    add_edit(ID_MONTH, L"1", 68, 136, 36, 22, ES_NUMBER);
    add_edit(ID_DAY, L"1", 108, 136, 36, 22, ES_NUMBER);
    add_edit(ID_HOUR, L"0", 148, 136, 36, 22, ES_NUMBER);
    add_edit(ID_MIN, L"0", 188, 136, 36, 22, ES_NUMBER);
    add_edit(ID_SEC, L"0", 228, 136, 36, 22, ES_NUMBER);
    add_btn(ID_NOW, L"填入现在", 12, 166, 88, 26);
    add_btn(ID_TIME, L"发送校时", 108, 166, 88, 26);

    add_text(L"消息", 12, 206, 40, 18);
    slot = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                         12, 228, 72, 120, wnd, (HMENU)(INT_PTR)ID_SLOT, NULL, NULL);
    SendMessageW(slot, CB_ADDSTRING, 0, (LPARAM)L"消息1");
    SendMessageW(slot, CB_ADDSTRING, 0, (LPARAM)L"消息2");
    SendMessageW(slot, CB_ADDSTRING, 0, (LPARAM)L"消息3");
    SendMessageW(slot, CB_SETCURSEL, 0, 0);
    add_edit(ID_MSG, L"你好", 92, 228, 200, 22, 0);
    add_btn(ID_SENDMSG, L"发送消息", 12, 258, 88, 26);
    add_btn(ID_SHOT, L"截取画面", 108, 258, 88, 26);

    log_box = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                            12, 298, 360, 88, wnd, (HMENU)(INT_PTR)ID_LOG, NULL, NULL);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        wnd = hwnd;
        build_ui();
        fill_now();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_CONNECT:
            if (port == INVALID_HANDLE_VALUE) {
                open_port();
            } else {
                close_port();
                log_line(L"已断开");
            }
            return 0;
        case ID_DEBUG:
            if (port != INVALID_HANDLE_VALUE) {
                uint8_t on = (uint8_t)(SendMessageW(GetDlgItem(wnd, ID_DEBUG), BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (send_frame(0x08, &on, 1)) {
                    log_line(on ? L"调试模式开" : L"调试模式关");
                }
            }
            return 0;
        case ID_NOW:
            fill_now();
            return 0;
        case ID_TIME:
            send_time();
            return 0;
        case ID_SENDMSG:
            send_message();
            return 0;
        case ID_SHOT:
            request_shot();
            return 0;
        default:
            break;
        }
        break;
    case WM_TIMER:
        pump_rx();
        return 0;
    case WM_DESTROY:
        close_port();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void tap_key(uint8_t bit) {
    uint8_t up = 0;

    send_frame(0x07, &bit, 1);
    Sleep(70);
    send_frame(0x07, &up, 1);
    Sleep(340);
}

static int headless_shot(LPCWSTR cmd) {
    int scene = cmd && wcsstr(cmd, L"scene") != NULL;
    int badge = cmd && wcsstr(cmd, L"badge") != NULL;
    int ok;
    uint8_t on = 1;
    static const uint8_t msg[] = {0, 0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD};

    port = open_com(L"COM5");
    if (port == INVALID_HANDLE_VALUE) {
        return 1;
    }
    if (scene) {
        send_frame(0x08, &on, 1);
        Sleep(200);
        send_frame(0x02, msg, (int)sizeof msg);
        Sleep(80);
        tap_key(4);
        tap_key(4);
        tap_key(2);
        Sleep(450);
    } else if (badge) {
        send_frame(0x08, &on, 1);
        Sleep(800);
    }
    if (!send_frame(0x06, NULL, 0)) {
        close_port();
        return 1;
    }
    ok = recv_shot(port, L"host\\obj\\shot_live.bmp");
    close_port();
    return ok ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
    WNDCLASSW wc;
    MSG msg;

    (void)prev;
    if (cmd && wcsstr(cmd, L"shot")) {
        return headless_shot(cmd);
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WatchComTool";
    RegisterClassW(&wc);
    CreateWindowW(L"WatchComTool", L"手表串口",
                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                  CW_USEDEFAULT, CW_USEDEFAULT, 400, 440,
                  NULL, NULL, inst, NULL);
    ShowWindow(wnd, show);
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
