#include "ui.h"

/* 蓝牙透传串口。
   手机 -> 手表：
     01 校时    年低 年高 月 日 时 分 秒
     02 消息    槽号 0..2，后面是 UTF-8
     03 歌词    UTF-8
     04 播放态  0 暂停 1 播放
     05 亮度    0 低 1 中 2 高
     06 要画面  无数据。下一帧回一张图
     07 按键    1 字节，bit0 上一项 bit1 确定 bit2 下一项。仅调试模式生效
     08 调试    1 开 0 关
   手表 -> 手机：
     10 控制    1 播放 2 暂停
   帧：A5 命令 长度 数据 异或。异或从帧头算到数据末尾。 */

enum {
    LINK_MARK = 0xA5,
    LINK_TIME = 0x01,
    LINK_MSG = 0x02,
    LINK_LYRIC = 0x03,
    LINK_STATE = 0x04,
    LINK_BRIGHT = 0x05,
    LINK_SNAP = 0x06,
    LINK_KEY = 0x07,
    LINK_DEBUG = 0x08,
    LINK_CTRL = 0x10,
    LINK_IMG = 0x11,
    LINK_PAY = 28,
    LINK_MSG_N = 3,
    LINK_MSG_LEN = 24,
    LINK_LYRIC_LEN = 28,
    ST_MARK = 0,
    ST_CMD = 1,
    ST_LEN = 2,
    ST_PAY = 3,
    ST_XOR = 4,
    ACT_PLAY = 1,
    ACT_PAUSE = 2
};

static char msgs[LINK_MSG_N][LINK_MSG_LEN];
static char lyric[LINK_LYRIC_LEN];
static int playing;
static int bright;
static int snap_arm;
static int snap_on;
static uint8_t link_keys;
static uint8_t link_down;
static uint8_t link_up;
static int ack_ok;
static int ack_bad;
static uint8_t snap_xor;
static uint32_t snap_left;
static int st;
static uint8_t cmd;
static uint8_t len;
static uint8_t got;
static uint8_t pay[LINK_PAY];
static uint8_t sum;

static void copy_text(char *dst, int cap, const uint8_t *src, int n) {
    int i;

    if (cap <= 0) {
        return;
    }
    if (n > cap - 1) {
        n = cap - 1;
    }
    for (i = 0; i < n; i++) {
        dst[i] = (char)src[i];
    }
    dst[n] = 0;
}

static void copy_cstr(char *dst, int cap, const char *src) {
    int n = 0;
    while (src[n]) {
        n++;
    }
    copy_text(dst, cap, (const uint8_t *)src, n);
}

static void reply(const char *s) {
    int n = 0;
    while (s[n]) {
        n++;
    }
    hal_link_write((const uint8_t *)s, n);
}

static void send_ctrl(uint8_t action) {
    uint8_t frame[5];
    frame[0] = LINK_MARK;
    frame[1] = LINK_CTRL;
    frame[2] = 1;
    frame[3] = action;
    frame[4] = (uint8_t)(frame[0] ^ frame[1] ^ frame[2] ^ frame[3]);
    hal_link_write(frame, 5);
}

static int time_ok(const uint8_t *p) {
    int month = p[2];
    int day = p[3];
    int hour = p[4];
    int minute = p[5];
    int second = p[6];
    return month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
           hour <= 23 && minute <= 59 && second <= 59;
}

static void apply_frame(void) {
    if (cmd == LINK_TIME && len == 7 && time_ok(pay)) {
        hal_clock_t clock;
        clock.year = (uint16_t)(pay[0] | ((uint16_t)pay[1] << 8));
        clock.month = pay[2];
        clock.day = pay[3];
        clock.hour = pay[4];
        clock.minute = pay[5];
        clock.second = pay[6];
        hal_clock_set(&clock);
        return;
    }
    if (cmd == LINK_MSG && len >= 1 && pay[0] < LINK_MSG_N) {
        copy_text(msgs[pay[0]], LINK_MSG_LEN, pay + 1, len - 1);
        return;
    }
    if (cmd == LINK_LYRIC) {
        copy_text(lyric, LINK_LYRIC_LEN, pay, len);
        return;
    }
    if (cmd == LINK_STATE && len == 1) {
        playing = pay[0] ? 1 : 0;
        return;
    }
    if (cmd == LINK_BRIGHT && len == 1 && pay[0] <= 2) {
        bright = pay[0];
        return;
    }
    if (cmd == LINK_SNAP && len == 0) {
        snap_arm = 1;
        return;
    }
    if (cmd == LINK_KEY && len == 1) {
        uint8_t next = (uint8_t)(pay[0] & 0x07u);
        link_down = (uint8_t)(link_down | (next & (uint8_t)~link_keys));
        link_up = (uint8_t)(link_up | (link_keys & (uint8_t)~next));
        link_keys = next;
        return;
    }
    if (cmd == LINK_DEBUG && len == 1) {
        ui_settings_set_debug(pay[0] ? 1 : 0);
    }
}

void link_snap_request(void) {
    snap_arm = 1;
}

static void snap_raw(uint8_t b) {
    snap_xor ^= b;
    hal_link_write(&b, 1);
}

static void snap_payload(uint8_t b) {
    if (snap_left == 0) {
        return;
    }
    snap_raw(b);
    snap_left--;
}

void link_snap_begin(int w, int h) {
    uint32_t total;

    if (!snap_arm || w <= 0 || h <= 0) {
        return;
    }
    snap_arm = 0;
    snap_on = 1;
    total = 4u + (uint32_t)w * (uint32_t)h * 2u;
    snap_left = total;
    snap_xor = 0;
    snap_raw(LINK_MARK);
    snap_raw(LINK_IMG);
    snap_raw(0xFF);
    snap_raw((uint8_t)(total & 255u));
    snap_raw((uint8_t)((total >> 8) & 255u));
    snap_raw((uint8_t)((total >> 16) & 255u));
    snap_payload((uint8_t)w);
    snap_payload((uint8_t)(w >> 8));
    snap_payload((uint8_t)h);
    snap_payload((uint8_t)(h >> 8));
}

void link_snap_pixels(const uint16_t *px, int n) {
    int i;

    if (!snap_on || !px || n <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        snap_payload((uint8_t)(px[i] >> 8));
        snap_payload((uint8_t)px[i]);
    }
}

void link_snap_end(void) {
    uint8_t x;

    if (!snap_on) {
        return;
    }
    x = snap_xor;
    hal_link_write(&x, 1);
    snap_on = 0;
    snap_left = 0;
}

static void feed(uint8_t b) {
    if (st == ST_MARK) {
        if (b == LINK_MARK) {
            sum = b;
            st = ST_CMD;
        }
        return;
    }
    if (st == ST_CMD) {
        cmd = b;
        sum ^= b;
        st = ST_LEN;
        return;
    }
    if (st == ST_LEN) {
        len = b;
        sum ^= b;
        got = 0;
        if (len > LINK_PAY) {
            st = ST_MARK;
            ack_bad++;
            return;
        }
        st = len == 0 ? ST_XOR : ST_PAY;
        return;
    }
    if (st == ST_PAY) {
        pay[got++] = b;
        sum ^= b;
        if (got >= len) {
            st = ST_XOR;
        }
        return;
    }
    if (b == sum) {
        apply_frame();
        ack_ok++;
    } else {
        ack_bad++;
    }
    st = ST_MARK;
}

void link_init(void) {
    copy_cstr(msgs[0], LINK_MSG_LEN, "消息1");
    copy_cstr(msgs[1], LINK_MSG_LEN, "消息2");
    copy_cstr(msgs[2], LINK_MSG_LEN, "消息3");
    lyric[0] = 0;
    playing = 0;
    bright = 1;
    st = ST_MARK;
}

void link_poll(void) {
    uint8_t b;
    int guard = 0;

    while (guard < 64 && hal_link_read(&b)) {
        feed(b);
        guard++;
    }
}

const char *link_message(int index) {
    if (index < 0 || index >= LINK_MSG_N) {
        return "";
    }
    return msgs[index];
}

int link_brightness(void) {
    return bright;
}

const char *link_media_lyric(void) {
    return lyric;
}

int link_media_playing(void) {
    return playing;
}

uint8_t link_key_level(void) {
    return link_keys;
}

void link_key_edges(uint8_t *down, uint8_t *up) {
    if (down) {
        *down = link_down;
    }
    if (up) {
        *up = link_up;
    }
    link_down = 0;
    link_up = 0;
}

void link_ack_flush(void) {
    while (ack_ok > 0) {
        reply("ok\r\n");
        ack_ok--;
    }
    while (ack_bad > 0) {
        reply("bad\r\n");
        ack_bad--;
    }
}

void link_media_play(void) {
    playing = 1;
    send_ctrl(ACT_PLAY);
}

void link_media_pause(void) {
    playing = 0;
    send_ctrl(ACT_PAUSE);
}

void link_media_toggle(void) {
    if (playing) {
        link_media_pause();
    } else {
        link_media_play();
    }
}
