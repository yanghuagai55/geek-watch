#include "ui.h"
#include "settings.h"
#include "sport.h"

#include <string.h>

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
     0A 回主页  无数据，关闭子页并回到表盘
     0B 点阵    块号 0..3 + 128 字节 1bpp（128x32，共 512 字节）
     14 要消息  无数据，请求最近 5 条消息列表（手机回 22）
     15 要详情  1 字节序号 0..4（手机回 23）
     16 离开消息 无数据，恢复媒体推送
     22 列表点阵 块号 0..13 + 128 字节 1bpp（128x112）
     23 详情点阵 块号 0..19 + 128 字节 1bpp（128x160）
     24 清空消息 无数据，列表/详情都回到“无消息”
     25 消息数量 1 字节 0..5，先于 22 下发，限制可选行数
     26 RLE 点阵 类型 1 媒体 2 列表 3 详情 + 块号 + (值, 连续数) 对
        （解出 128 字节后等价于对应的 0B/22/23 分块）
     27 要运动  无数据。回 27：步数/距离/卡路里/活跃分钟/目标（u32×5）
        + 12 个 2 小时桶（u16×12）
     13 传感器  无数据，回一行 DBG ... 调试文本
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
    LINK_PERF = 0x09,
    LINK_HOME = 0x0A,
    LINK_BMP = 0x0B,
    LINK_SETTING = 0x18,
    LINK_GET_SETTINGS = 0x19,
    LINK_SETTINGS_STATE = 0x1A,
    LINK_MSG_LIST_REQ = 0x14,
    LINK_MSG_DETAIL_REQ = 0x15,
    LINK_MSG_LEAVE = 0x16,
    LINK_MSG_LIST_BMP = 0x22,
    LINK_MSG_DETAIL_BMP = 0x23,
    LINK_MSG_CLEAR = 0x24,
    LINK_MSG_COUNT = 0x25,
    LINK_MASK_RLE = 0x26,
    LINK_SPORT = 0x27,
    LINK_STATS = 0x12,
    LINK_SENSOR = 0x13,
    LINK_CTRL = 0x10,
    LINK_IMG = 0x11,
    LINK_PAY = 160,
    LINK_MSG_N = 3,
    LINK_MSG_LEN = 24,
    LINK_LYRIC_LEN = 28,
    LINK_BMP_W = 128,
    LINK_BMP_H = 32,
    LINK_BMP_CHUNK = 128,
    LINK_BMP_CHUNKS = 4,
    LINK_MSG_LIST_H = 112,
    LINK_MSG_DETAIL_H = 160,
    LINK_MSG_LIST_CHUNKS = 14,
    LINK_MSG_DETAIL_CHUNKS = 20,
    LINK_MASK_BYTES = LINK_BMP_W * LINK_MSG_DETAIL_H / 8,
    ST_MARK = 0,
    ST_CMD = 1,
    ST_LEN = 2,
    ST_PAY = 3,
    ST_XOR = 4,
    ACT_PLAY = 1,
    ACT_PAUSE = 2,
    ACT_NEXT = 3,
    ACT_PREV = 4
};

static char msgs[LINK_MSG_N][LINK_MSG_LEN];
static char lyric[LINK_LYRIC_LEN];
/* 媒体标题、消息列表、消息详情共用同一块位图缓冲：
   4bpp 128x32=2048B，1bpp 128x112=1792B，1bpp 128x160=2560B。
   同一时刻只会显示其中一种，手机在消息页期间会暂停媒体推送。 */
static uint8_t mask_buf[LINK_MASK_BYTES];
#define media_mask mask_buf
static int media_mask_ready;
static int msg_list_ready;
static int msg_list_loading;
static int msg_list_empty;
static int msg_detail_ready;
static int msg_detail_loading;
static uint32_t last_rx_ms;
static int playing;
static int bright;
static int snap_arm;
static int snap_on;
static int perf_requested;
static uint8_t link_keys;
static uint8_t link_down;
static uint8_t link_up;
static int ack_ok;
static int ack_bad;
static int no_ack;
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

static void send_empty(uint8_t code) {
    uint8_t frame[4];
    frame[0] = LINK_MARK;
    frame[1] = code;
    frame[2] = 0;
    frame[3] = (uint8_t)(frame[0] ^ frame[1] ^ frame[2]);
    hal_link_write(frame, 4);
}

/* 1A：当前手表设置，供 App 回显。 */
static void send_settings_state(void) {
    settings_t *s = settings_get();
    uint8_t frame[10];
    int i;
    uint8_t check = 0;

    frame[0] = LINK_MARK;
    frame[1] = LINK_SETTINGS_STATE;
    frame[2] = 6;
    frame[3] = (uint8_t)(s->raise_wrist ? 1 : 0);
    frame[4] = (uint8_t)(s->key_beep ? 1 : 0);
    frame[5] = (uint8_t)(s->brightness <= 2 ? s->brightness : 1);
    frame[6] = (uint8_t)(s->bluetooth ? 1 : 0);
    frame[7] = (uint8_t)(s->led_mode <= 2 ? s->led_mode : 1);
    frame[8] = (uint8_t)(s->screen_off <= 4 ? s->screen_off : 0);
    for (i = 0; i < 9; i++) {
        check = (uint8_t)(check ^ frame[i]);
    }
    frame[9] = check;
    hal_link_write(frame, 10);
}

void link_notify_settings(void) {
    send_settings_state();
}

/* 27：运动数据。5 个 u32 小端（步数/距离/卡路里/活跃分钟/目标）
   + 12 个 u16 桶（每 2 小时一段），共 44 字节。 */
static void send_sport_state(void) {
    sport_stats_t s;
    uint32_t bars[SPORT_BARS];
    uint32_t vals[5];
    uint8_t frame[48];
    uint8_t check;
    int i, j;

    sport_get(&s);
    sport_get_bars(bars, SPORT_BARS);
    vals[0] = s.steps;
    vals[1] = s.distance_m;
    vals[2] = s.calories;
    vals[3] = s.active_min;
    vals[4] = s.goal;

    frame[0] = LINK_MARK;
    frame[1] = LINK_SPORT;
    frame[2] = 44;
    check = (uint8_t)(frame[0] ^ frame[1] ^ frame[2]);
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 4; j++) {
            uint8_t b = (uint8_t)(vals[i] >> (j * 8));
            frame[3 + i * 4 + j] = b;
            check = (uint8_t)(check ^ b);
        }
    }
    for (i = 0; i < SPORT_BARS; i++) {
        uint32_t v = bars[i] > 0xFFFFu ? 0xFFFFu : bars[i];
        uint8_t lo = (uint8_t)(v & 0xFFu);
        uint8_t hi = (uint8_t)(v >> 8);
        frame[23 + i * 2] = lo;
        frame[24 + i * 2] = hi;
        check = (uint8_t)(check ^ lo);
        check = (uint8_t)(check ^ hi);
    }
    frame[47] = check;
    hal_link_write(frame, sizeof frame);
}

void link_request_messages(void) {
    msg_list_ready = 0;
    msg_list_loading = 1;
    msg_list_empty = 0;
    msg_detail_ready = 0;
    msg_detail_loading = 0;
    send_empty(LINK_MSG_LIST_REQ);
}

void link_request_message_detail(int index) {
    uint8_t frame[5];
    if (index < 0) {
        index = 0;
    }
    msg_detail_ready = 0;
    msg_detail_loading = 1;
    frame[0] = LINK_MARK;
    frame[1] = LINK_MSG_DETAIL_REQ;
    frame[2] = 1;
    frame[3] = (uint8_t)index;
    frame[4] = (uint8_t)(frame[0] ^ frame[1] ^ frame[2] ^ frame[3]);
    hal_link_write(frame, 5);
}

void link_leave_messages(void) {
    msg_list_ready = 0;
    msg_list_loading = 0;
    msg_list_empty = 0;
    msg_detail_ready = 0;
    msg_detail_loading = 0;
    send_empty(LINK_MSG_LEAVE);
}

/* 块号 0..3，每块 128 字节 = 8 行 1bpp；展开成 4bpp 覆盖位图。 */
static void mask_chunk(uint8_t chunk, const uint8_t *src, int n) {
    const int rows = LINK_BMP_H / LINK_BMP_CHUNKS;
    int r, c;

    /* 用户正在看消息时，媒体点阵只会破坏消息缓冲，直接丢掉。 */
    if (ui_app_message_active()) {
        return;
    }
    if (chunk >= LINK_BMP_CHUNKS || n < LINK_BMP_CHUNK) {
        return;
    }
    if (chunk == 0) {
        media_mask_ready = 0;
        /* 同一块缓冲被媒体占用，消息页回退到“无消息”。 */
        msg_list_ready = 0;
        msg_list_loading = 0;
        msg_list_empty = 0;
        msg_detail_ready = 0;
        msg_detail_loading = 0;
        memset(media_mask, 0, sizeof media_mask);
        /* 新的媒体行到达时自动切到音乐页，让标题/歌词常驻可见。 */
        ui_app_open_music();
    }
    for (r = 0; r < rows; r++) {
        uint8_t *dst = media_mask + (chunk * rows + r) * (LINK_BMP_W / 2);
        const uint8_t *line = src + r * (LINK_BMP_W / 8);
        for (c = 0; c < LINK_BMP_W; c++) {
            uint8_t v = ((line[c >> 3] >> (7 - (c & 7))) & 1) ? 0x0F : 0x00;
            if (c & 1) {
                dst[c >> 1] = (uint8_t)((dst[c >> 1] & 0xF0) | v);
            } else {
                dst[c >> 1] = (uint8_t)((dst[c >> 1] & 0x0F) | (uint8_t)(v << 4));
            }
        }
    }
    if (chunk == LINK_BMP_CHUNKS - 1) {
        media_mask_ready = 1;
    }
}

/* 128x112 列表图，1bpp，14 块；块 0 清空并让页面先显示“无消息”。 */
static void msg_list_chunk(uint8_t chunk, const uint8_t *src, int n) {
    if (!ui_app_message_active()) {
        return;
    }
    if (chunk >= LINK_MSG_LIST_CHUNKS || n < LINK_BMP_CHUNK) {
        return;
    }
    if (chunk == 0) {
        msg_list_ready = 0;
        msg_list_loading = 1;
        msg_list_empty = 0;
        msg_detail_ready = 0;
        msg_detail_loading = 0;
        media_mask_ready = 0;
        memset(mask_buf, 0, LINK_BMP_W * LINK_MSG_LIST_H / 8);
    }
    memcpy(mask_buf + chunk * LINK_BMP_CHUNK, src, LINK_BMP_CHUNK);
    if (chunk == LINK_MSG_LIST_CHUNKS - 1) {
        msg_list_ready = 1;
        msg_list_loading = 0;
    }
}

/* 128x160 详情图，1bpp，20 块；会覆盖列表缓冲。 */
static void msg_detail_chunk(uint8_t chunk, const uint8_t *src, int n) {
    if (!ui_app_message_active()) {
        return;
    }
    if (chunk >= LINK_MSG_DETAIL_CHUNKS || n < LINK_BMP_CHUNK) {
        return;
    }
    if (chunk == 0) {
        msg_detail_ready = 0;
        msg_detail_loading = 1;
        msg_list_ready = 0;
        msg_list_loading = 0;
        msg_list_empty = 0;
        media_mask_ready = 0;
        memset(mask_buf, 0, sizeof mask_buf);
    }
    memcpy(mask_buf + chunk * LINK_BMP_CHUNK, src, LINK_BMP_CHUNK);
    if (chunk == LINK_MSG_DETAIL_CHUNKS - 1) {
        msg_detail_ready = 1;
        msg_detail_loading = 0;
    }
}

/* 0x26：RLE 压缩的 128 字节点阵块，解压后复用原来的分块函数。
   每对字节是（值, 连续次数 1..255），解出正好 128 字节才接收。 */
static void mask_rle(uint8_t type, uint8_t chunk, const uint8_t *src, int n) {
    uint8_t block[LINK_BMP_CHUNK];
    int i = 0;
    int out = 0;

    while (i + 1 < n) {
        uint8_t value = src[i];
        int count = src[i + 1];
        i += 2;
        while (count > 0 && out < LINK_BMP_CHUNK) {
            block[out++] = value;
            count--;
        }
        if (out >= LINK_BMP_CHUNK) {
            break;
        }
    }
    if (out != LINK_BMP_CHUNK) {
        return;
    }
    if (type == 1) {
        mask_chunk(chunk, block, LINK_BMP_CHUNK);
    } else if (type == 2) {
        msg_list_chunk(chunk, block, LINK_BMP_CHUNK);
    } else if (type == 3) {
        msg_detail_chunk(chunk, block, LINK_BMP_CHUNK);
    }
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
    if (cmd == LINK_BMP && len == 1 + LINK_BMP_CHUNK) {
        mask_chunk(pay[0], pay + 1, len - 1);
        return;
    }
    if (cmd == LINK_MSG_LIST_BMP && len == 1 + LINK_BMP_CHUNK) {
        msg_list_chunk(pay[0], pay + 1, len - 1);
        return;
    }
    if (cmd == LINK_MSG_DETAIL_BMP && len == 1 + LINK_BMP_CHUNK) {
        msg_detail_chunk(pay[0], pay + 1, len - 1);
        return;
    }
    if (cmd == LINK_MASK_RLE && len >= 3) {
        mask_rle(pay[0], pay[1], pay + 2, len - 2);
        return;
    }
    if (cmd == LINK_MSG_CLEAR && len == 0) {
        msg_list_ready = 0;
        msg_list_loading = 0;
        msg_list_empty = 1;
        msg_detail_ready = 0;
        msg_detail_loading = 0;
        ui_app_set_message_count(0);
        return;
    }
    if (cmd == LINK_MSG_COUNT && len == 1) {
        int count = pay[0];
        if (count < 0) {
            count = 0;
        }
        if (count > 5) {
            count = 5;
        }
        ui_app_set_message_count(count);
        if (count == 0) {
            msg_list_ready = 0;
            msg_list_loading = 0;
            msg_list_empty = 1;
        } else {
            msg_list_loading = 1;
            msg_list_empty = 0;
        }
        return;
    }
    if (cmd == LINK_LYRIC) {
        copy_text(lyric, LINK_LYRIC_LEN, pay, len);
        media_mask_ready = 0;
        return;
    }
    if (cmd == LINK_STATE && len == 1) {
        playing = pay[0] ? 1 : 0;
        return;
    }
    if (cmd == LINK_BRIGHT && len == 1 && pay[0] <= 2) {
        bright = pay[0];
        ui_settings_apply(3, pay[0]);
        return;
    }
    if (cmd == LINK_SETTING && len == 2) {
        ui_settings_apply(pay[0], pay[1]);
        send_settings_state();
        return;
    }
    if (cmd == LINK_GET_SETTINGS && len == 0) {
        send_settings_state();
        return;
    }
    if (cmd == LINK_SPORT && len == 0) {
        send_sport_state();
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
    if (cmd == LINK_PERF && len == 0) {
        perf_requested = 1;
        return;
    }
    if (cmd == LINK_SENSOR && len == 0) {
        char buf[128];
        int n = hal_debug_snapshot(buf, (int)sizeof buf);
        if (n > 0) {
            hal_link_write((const uint8_t *)"DBG ", 4);
            hal_link_write((const uint8_t *)buf, n);
            hal_link_write((const uint8_t *)"\r\n", 2);
            no_ack = 1;
        }
        return;
    }
    if (cmd == LINK_DEBUG && len == 1) {
        ui_settings_set_debug(pay[0] ? 1 : 0);
    }
    if (cmd == LINK_HOME && len == 0) {
        ui_app_go_home();
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

int link_snap_active(void) {
    return snap_on;
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
        no_ack = 0;
        last_rx_ms = hal_tick_ms();
        apply_frame();
        if (!no_ack) {
            ack_ok++;
        }
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
    media_mask_ready = 0;
    msg_list_ready = 0;
    msg_list_loading = 0;
    msg_list_empty = 0;
    msg_detail_ready = 0;
    msg_detail_loading = 0;
    memset(mask_buf, 0, sizeof mask_buf);
    last_rx_ms = 0;
    playing = 0;
    bright = 1;
    snap_arm = snap_on = perf_requested = 0;
    snap_left = 0;
    link_keys = link_down = link_up = 0;
    ack_ok = ack_bad = 0;
    no_ack = 0;
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

int link_media_mask_ready(void) {
    return media_mask_ready;
}

const uint8_t *link_media_mask(void) {
    return media_mask;
}

int link_msg_list_ready(void) {
    return msg_list_ready;
}

int link_msg_list_empty(void) {
    return msg_list_empty;
}

const uint8_t *link_msg_list_mask(void) {
    return mask_buf;
}

int link_msg_detail_ready(void) {
    return msg_detail_ready;
}

const uint8_t *link_msg_detail_mask(void) {
    return mask_buf;
}

int link_connected(void) {
    uint32_t now = hal_tick_ms();
    return last_rx_ms != 0 && (uint32_t)(now - last_rx_ms) < 4000u;
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

static void send_perf(void) {
    ui_render_stats_t stats;
    uint32_t values[8];
    uint32_t ram_used = 0;
    uint32_t ram_total = 0;
    uint16_t cpu;
    uint8_t fps;
    uint8_t frame[36];
    uint8_t check;
    unsigned i, j;
    ui_render_get_stats(&stats);
    values[0] = stats.prepare_us;
    values[1] = stats.raster_us;
    values[2] = stats.wait_us;
    values[3] = stats.frame_us;
    values[4] = stats.pixels;
    values[5] = stats.bands | ((uint32_t)stats.dropped << 16);

    hal_ram_get(&ram_used, &ram_total);
    cpu = hal_cpu_tenths();
    if (cpu > 1000u) {
        cpu = 1000u;
    }
    fps = ui_app_get_fps();
    values[6] = (ram_used & 0xFFFFu) | ((uint32_t)cpu << 16);
    values[7] = (ram_total & 0xFFFFu) | ((uint32_t)fps << 16);

    frame[0] = LINK_MARK;
    frame[1] = LINK_STATS;
    frame[2] = 32;
    check = (uint8_t)(frame[0] ^ frame[1] ^ frame[2]);
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 4; j++) {
            uint8_t b = (uint8_t)(values[i] >> (j * 8));
            frame[3 + i * 4 + j] = b;
            check ^= b;
        }
    }
    frame[35] = check;
    hal_link_write(frame, sizeof frame);
}

void link_ack_flush(void) {
    if (perf_requested) {
        perf_requested = 0;
        send_perf();
    }
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

void link_media_next(void) {
    send_ctrl(ACT_NEXT);
}

void link_media_prev(void) {
    send_ctrl(ACT_PREV);
}
