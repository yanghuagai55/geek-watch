#include "ui.h"
#include "ui_menu.h"
#include "settings.h"
#include "sport.h"
#include "compass3d.h"
#include "pic_mod.h"

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
    LINK_EXT_IMG_META = 0x28,
    LINK_EXT_IMG_DATA = 0x29,
    LINK_SET_CFG = 0x2A,
    LINK_GET_CFG = 0x2B,
    LINK_CFG_STATE = 0x2C,
    LINK_EXT_OP = 0x2D,
    LINK_EXT_POOL_STATE = 0x2E,
    LINK_EXT_ASSET = 0x2F,
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
    LINK_MSG_DETAIL_H = 160,
    LINK_MSG_DETAIL_CHUNKS = 20,
    LINK_ROW_BYTES = LINK_BMP_W * 22 / 8,
    LINK_LIST_WIN_ROWS = 5,
    LINK_MEDIA_BYTES = LINK_BMP_W * LINK_BMP_H / 2,
    LINK_LIST_BYTES = LINK_ROW_BYTES * LINK_LIST_WIN_ROWS,
    LINK_MASK_BYTES = LINK_BMP_W * LINK_MSG_DETAIL_H / 8,
    LINK_ASSET_COMPASS_W = 64,
    LINK_ASSET_COMPASS_H = 64,
    LINK_ASSET_COMPASS_BYTES = 4 + LINK_ASSET_COMPASS_W * LINK_ASSET_COMPASS_H / 8,
    DYN_NONE = 0,
    DYN_MEDIA = 1,
    DYN_MSG_LIST = 2,
    DYN_MSG_DETAIL = 3,
    DYN_COMPASS = 4,
    DYN_COMPASS3D = 5,
    DYN_ABOUT = 6,
    DYN_SPORT = 7,
    DYN_ANALOG = 8,
    DYN_SYS = 9,
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

#define EXT_DIR_MAGIC     0x41544431u /* 'ATD1' */
#define EXT_DIR_VER       4u
#define EXT_DIR_RECLEN    220u
#define EXT_DIR_SLOT0     (LINK_EXT_OFF_DIR + 0x000u)
#define EXT_DIR_SLOT1     (LINK_EXT_OFF_DIR + 0x100u)

typedef struct {
    uint16_t off;
    uint16_t len;
} ext_blk_t;

enum {
    EXT_BLK_IMG = 0,
    EXT_BLK_MEDIA = 1,
    EXT_BLK_MSG0 = 2,
    EXT_BLK_ASSET0 = 2 + LINK_EXT_MSG_MAX * 2,
    EXT_BLK_ASSET_COMPASS = EXT_BLK_ASSET0,
    EXT_BLK_ASSET_MSG_EMPTY = EXT_BLK_ASSET0 + 1,
    EXT_BLK_ASSET_COMPASS3D = EXT_BLK_ASSET0 + 2,
    EXT_BLK_ASSET_ABOUT = EXT_BLK_ASSET0 + 3,
    EXT_BLK_ASSET_HEADERS = EXT_BLK_ASSET0 + 4,
    EXT_BLK_ASSET_SPORT = EXT_BLK_ASSET0 + 5,
    EXT_BLK_ASSET_ANALOG = EXT_BLK_ASSET0 + 6,
    EXT_BLK_ASSET_SYS = EXT_BLK_ASSET0 + 7,
    EXT_BLK_TOTAL = EXT_BLK_ASSET0 + 8
};

typedef struct {
    uint32_t seq;
    ext_blk_t blks[EXT_BLK_TOTAL];
    uint8_t msg_count;
    uint8_t active_slot;
} ext_dir_t;

enum {
    HDR_IDX_BYTES = 280u,
    HDR_RING_BYTES = 1360u,
    HDR_BUF_BYTES = HDR_IDX_BYTES + HDR_RING_BYTES,
    HDR_SLOTS = 10
};

#if !defined(AT32_SLIM_ROM)
static char msgs[LINK_MSG_N][LINK_MSG_LEN];
#endif
static char lyric[LINK_LYRIC_LEN];
/* 动态位图缓冲 + 统一片外 32KB I2C Flash 动态块分配与掉电恢复。 */
static struct {
    ext_dir_t ext_dir;
    uint8_t win_row_id[LINK_LIST_WIN_ROWS];
    uint8_t *dyn_buf;
    uint16_t dyn_cap;
    uint8_t dyn_kind;
    uint8_t *rx_row;
    uint8_t *rx_det;
    uint8_t *rx_asset;
    uint16_t rx_asset_need;
    uint16_t rx_asset_crc;
    uint16_t rx_asset_off;
    uint8_t rx_asset_id;
    uint8_t *c3d_buf;
    uint16_t c3d_entry;
    uint16_t c3d_total;
    uint8_t pic_ready_kind;
    uint16_t pic_entry;
    uint8_t about_ready;
    uint8_t about_want;
    uint16_t about_total;
    uint16_t about_crc;
    uint8_t hdr_ready;
    uint8_t hdr_active;
    uint8_t hdr_loaded;
    uint8_t hdr_count;
    uint16_t hdr_cover_off;
    uint16_t hdr_total;
    uint8_t *hdr_buf;
    uint16_t hdr_wr;
    uint16_t hdr_slot_cp[HDR_SLOTS];
    uint16_t hdr_slot_off[HDR_SLOTS];
    uint16_t hdr_slot_end[HDR_SLOTS];
    uint8_t hdr_slot_next;
    uint32_t load_ext_off;
    uint16_t load_pos;
    uint16_t load_need;
    uint16_t load_crc;
    uint16_t load_entry;
    uint8_t load_kind;
    uint8_t rx_det_slot;
    uint8_t list_target_n;
    uint16_t media_staging_off;
    uint8_t media_staging_ok;
    uint8_t detail_slot;
    uint8_t img_ready;
    uint8_t img_to_chip;
    uint16_t img_gen;
    uint8_t img_fmt;
    int16_t img_w;
    int16_t img_h;
    uint16_t img_total_bytes;
    uint16_t img_staging_off;
    uint8_t img_apply_dial;
    uint8_t img_msg_preview;
    int media_mask_ready;
    int msg_list_ready;
    int msg_list_loading;
    int msg_list_empty;
    int msg_detail_ready;
    int msg_detail_loading;
    uint32_t last_rx_ms;
    int playing;
    int bright;
    int snap_arm;
    int snap_on;
    int perf_requested;
    uint8_t link_keys;
    uint8_t link_down;
    uint8_t link_up;
    int ack_ok;
    int ack_bad;
    int no_ack;
    uint8_t snap_xor;
    uint32_t snap_left;
    int st;
    uint8_t cmd;
    uint8_t len;
    uint8_t got;
    uint8_t pay[LINK_PAY];
    uint8_t sum;
} lk;

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

#if !defined(AT32_SLIM_ROM)
static void copy_cstr(char *dst, int cap, const char *src) {
    int n = 0;
    while (src[n]) {
        n++;
    }
    copy_text(dst, cap, (const uint8_t *)src, n);
}
#endif

static void reply(const char *s) {
    int n = 0;
    while (s[n]) {
        n++;
    }
    hal_link_write((const uint8_t *)s, n);
}

static uint16_t u16_get(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void u16_put(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static uint32_t u32_get(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void u32_put(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static void send_frame_buf(uint8_t *frame, uint8_t plen) {
    uint8_t check = 0u;
    int i;
    frame[0] = LINK_MARK;
    frame[2] = plen;
    for (i = 0; i < (int)plen + 3; i++) {
        check = (uint8_t)(check ^ frame[i]);
    }
    frame[plen + 3] = check;
    hal_link_write(frame, (int)plen + 4);
}

static void send_ctrl(uint8_t action) {
    uint8_t frame[5];
    frame[1] = LINK_CTRL;
    frame[3] = action;
    send_frame_buf(frame, 1u);
}

static void send_empty(uint8_t code) {
    uint8_t frame[4];
    frame[1] = code;
    send_frame_buf(frame, 0u);
}

/* 1A：当前手表设置，供 App 回显。 */
static void send_settings_state(void) {
    settings_t *s = settings_get();
    uint8_t frame[10];

    frame[1] = LINK_SETTINGS_STATE;
    frame[3] = (uint8_t)(s->raise_wrist ? 1 : 0);
    frame[4] = (uint8_t)(s->key_beep ? 1 : 0);
    frame[5] = (uint8_t)(s->brightness <= 2 ? s->brightness : 1);
    frame[6] = (uint8_t)(s->bluetooth ? 1 : 0);
    frame[7] = (uint8_t)(s->led_mode <= 2 ? s->led_mode : 1);
    frame[8] = (uint8_t)(s->screen_off <= 4 ? s->screen_off : 0);
    send_frame_buf(frame, 6u);
}

void link_notify_settings(void) {
    send_settings_state();
}

/* 2C：完整设置 + 表盘自定义 + AT24C256 存储/优先级状态（共 26 字节）。 */
static void send_cfg_state(void) {
    settings_t *s = settings_get();
    uint8_t frame[30];

    frame[1] = LINK_CFG_STATE;
    settings_pack_wire(frame + 3);
    frame[23] = (uint8_t)((hal_ext_flash_present() ? 0x01u : 0u) |
                          (s->cfg_loaded ? 0x02u : 0u) |
                          (s->img_ready ? 0x04u : 0u) |
                          (s->mag_cal_saved ? 0x08u : 0u) |
                          ((s->img_ready && s->img_on_chip) ? 0x10u : 0u));
    frame[24] = s->cfg_source;
    u16_put(frame + 25, s->img_bytes);
    frame[27] = s->img_fmt;
    frame[28] = hal_ext_flash_addr();
    send_frame_buf(frame, 26u);
}

void link_notify_config(void) {
    send_cfg_state();
}

/* 27：运动数据。5 个 u32 小端（步数/距离/卡路里/活跃分钟/目标）
   + 12 个 u16 桶（每 2 小时一段），共 44 字节。 */
static void send_sport_state(void) {
    sport_stats_t s;
    uint32_t bars[SPORT_BARS];
    uint8_t frame[48];
    int i;

    sport_get(&s);
    sport_get_bars(bars, SPORT_BARS);
    frame[1] = LINK_SPORT;
    u32_put(frame + 3, s.steps);
    u32_put(frame + 7, s.distance_m);
    u32_put(frame + 11, s.calories);
    u32_put(frame + 15, s.active_min);
    u32_put(frame + 19, s.goal);
    for (i = 0; i < SPORT_BARS; i++) {
        uint32_t v = bars[i] > 0xFFFFu ? 0xFFFFu : bars[i];
        u16_put(frame + 23 + i * 2, (uint16_t)v);
    }
    send_frame_buf(frame, 44u);
}

static int ext_collect_blks(ext_blk_t *blks, int cap) {
    int n = 0, i, j;
    int active_end = EXT_BLK_MSG0 + (int)lk.ext_dir.msg_count * 2;
    if (active_end > EXT_BLK_ASSET0) active_end = EXT_BLK_ASSET0;
    for (i = 0; i < active_end && n < cap; i++) {
        if (lk.ext_dir.blks[i].len > 0u) {
            blks[n++] = lk.ext_dir.blks[i];
        }
    }
    for (i = EXT_BLK_ASSET0; i < EXT_BLK_TOTAL && n < cap; i++) {
        if (lk.ext_dir.blks[i].len > 0u) {
            blks[n++] = lk.ext_dir.blks[i];
        }
    }
    for (i = 1; i < n; i++) {
        ext_blk_t key = blks[i];
        j = i - 1;
        while (j >= 0 && blks[j].off > key.off) {
            blks[j + 1] = blks[j];
            j--;
        }
        blks[j + 1] = key;
    }
    return n;
}

static void ext_dir_save(void) {
    uint8_t rec[EXT_DIR_RECLEN];
    uint32_t next;
    int i;

    if (!hal_ext_flash_present()) {
        return;
    }
    memset(rec, 0, sizeof rec);
    lk.ext_dir.seq++;
    u32_put(rec + 0, EXT_DIR_MAGIC);
    u32_put(rec + 4, lk.ext_dir.seq);
    rec[8] = EXT_DIR_VER;
    rec[9] = lk.ext_dir.msg_count <= LINK_EXT_MSG_MAX ? lk.ext_dir.msg_count : LINK_EXT_MSG_MAX;
    for (i = 0; i < EXT_BLK_TOTAL; i++) {
        u16_put(rec + 10 + i * 4, lk.ext_dir.blks[i].off);
        u16_put(rec + 12 + i * 4, lk.ext_dir.blks[i].len);
    }
    u16_put(rec + EXT_DIR_RECLEN - 2u, settings_crc16(rec, EXT_DIR_RECLEN - 2u));
    next = (lk.ext_dir.active_slot ^ 1u) ? EXT_DIR_SLOT1 : EXT_DIR_SLOT0;
    if (hal_ext_flash_write(next, rec, EXT_DIR_RECLEN)) {
        lk.ext_dir.active_slot ^= 1u;
    }
}

static int ext_dir_read_slot(uint32_t off, uint8_t *rec) {
    if (!hal_ext_flash_read(off, rec, EXT_DIR_RECLEN)) {
        return 0;
    }
    if (u32_get(rec + 0) != EXT_DIR_MAGIC || rec[8] != EXT_DIR_VER) {
        return 0;
    }
    return u16_get(rec + EXT_DIR_RECLEN - 2u) == settings_crc16(rec, EXT_DIR_RECLEN - 2u);
}

static int blk_ok(uint16_t off, uint16_t blen) {
    return blen > 0u && (uint32_t)off + (uint32_t)blen <= LINK_EXT_POOL_SIZE;
}

/* 双端防碎片动态池分配：top_down=0 从低端向上分配（表盘/静态ROM资产）；top_down=1 从高端向下分配（消息/媒体）。 */
static int ext_pool_alloc_ex(uint16_t need, int top_down,
                             uint16_t *out_off, uint16_t *free_max_out) {
    for (;;) {
        ext_blk_t blks[EXT_BLK_TOTAL];
        int n = ext_collect_blks(blks, (int)(sizeof blks / sizeof blks[0]));
        uint16_t cursor = 0u;
        uint16_t best = 0u;
        uint16_t picked = 0u;
        int have = 0;
        int i;

        for (i = 0; i <= n; i++) {
            uint16_t end = (i < n) ? blks[i].off : (uint16_t)LINK_EXT_POOL_SIZE;
            if (end > cursor) {
                uint16_t gap = (uint16_t)(end - cursor);
                if (gap > best) best = gap;
                if (need > 0u && gap >= need) {
                    if (!top_down) {
                        if (out_off) *out_off = cursor;
                        return 1;
                    }
                    picked = (uint16_t)(end - need);
                    have = 1;
                }
            }
            if (i < n) {
                uint32_t nxt = (uint32_t)blks[i].off + (uint32_t)blks[i].len;
                if (nxt > cursor) {
                    cursor = nxt > LINK_EXT_POOL_SIZE ? (uint16_t)LINK_EXT_POOL_SIZE : (uint16_t)nxt;
                }
            }
        }
        if (have) {
            if (out_off) *out_off = picked;
            return 1;
        }
        if (need == 0u) {
            if (free_max_out) *free_max_out = best;
            return 1;
        }
        if (need > LINK_EXT_POOL_SIZE) {
            return 0;
        }
        if (lk.ext_dir.blks[EXT_BLK_MEDIA].len > 0u) {
            lk.ext_dir.blks[EXT_BLK_MEDIA].off = 0u;
            lk.ext_dir.blks[EXT_BLK_MEDIA].len = 0u;
            continue;
        }
        if (lk.ext_dir.msg_count == 0u) {
            return 0;
        }
        lk.ext_dir.msg_count--;
        memset(&lk.ext_dir.blks[EXT_BLK_MSG0 + (int)lk.ext_dir.msg_count * 2], 0, 2 * sizeof(ext_blk_t));
        ui_app_set_message_count(lk.ext_dir.msg_count);
    }
}

static int ext_pool_alloc(uint16_t need, int top_down, uint16_t *out_off) {
    return ext_pool_alloc_ex(need, top_down, out_off, 0);
}

void link_ext_pool_stats(uint16_t *total, uint16_t *used, uint16_t *free_max,
                         uint16_t *msg_bytes, uint16_t *img_bytes, uint8_t *msg_n) {
    uint16_t mb = 0u, fm = 0u, ab = 0u;
    int i;
    int msg_end = EXT_BLK_MSG0 + (int)lk.ext_dir.msg_count * 2;
    if (msg_end > EXT_BLK_ASSET0) msg_end = EXT_BLK_ASSET0;
    for (i = EXT_BLK_MSG0; i < msg_end; i++) {
        mb = (uint16_t)(mb + lk.ext_dir.blks[i].len);
    }
    for (i = EXT_BLK_ASSET0; i < EXT_BLK_TOTAL; i++) {
        ab = (uint16_t)(ab + lk.ext_dir.blks[i].len);
    }
    (void)ext_pool_alloc_ex(0u, 0, 0, &fm);
    if (total) *total = LINK_EXT_POOL_SIZE;
    if (used) *used = (uint16_t)(lk.ext_dir.blks[EXT_BLK_IMG].len + lk.ext_dir.blks[EXT_BLK_MEDIA].len + mb + ab);
    if (free_max) *free_max = fm;
    if (msg_bytes) *msg_bytes = mb;
    if (img_bytes) *img_bytes = lk.ext_dir.blks[EXT_BLK_IMG].len;
    if (msg_n) *msg_n = lk.ext_dir.msg_count;
}

static void send_ext_pool_state(void) {
    uint16_t tot = 0u, used = 0u, fm = 0u, mb = 0u, ib = 0u;
    uint8_t mn = 0u;
    uint8_t frame[17];

    link_ext_pool_stats(&tot, &used, &fm, &mb, &ib, &mn);
    frame[1] = LINK_EXT_POOL_STATE;
    u16_put(frame + 3, tot);
    u16_put(frame + 5, used);
    u16_put(frame + 7, fm);
    u16_put(frame + 9, mb);
    u16_put(frame + 11, ib);
    frame[13] = mn;
    frame[14] = LINK_EXT_MSG_MAX;
    frame[15] = (uint8_t)((lk.ext_dir.blks[EXT_BLK_ASSET_COMPASS].len > 0u ? 0x01u : 0u) |
                          (lk.ext_dir.blks[EXT_BLK_ASSET_SYS].len > 12u ? 0x02u : 0u) |
                          (lk.ext_dir.blks[EXT_BLK_ASSET_COMPASS3D].len > 12u ? 0x04u : 0u) |
                          (lk.about_ready ? 0x08u : 0u) |
                          (ui_app_music_active() ? 0x10u : 0u) |
                          (lk.hdr_ready ? 0x20u : 0u) |
                          (lk.ext_dir.blks[EXT_BLK_ASSET_SPORT].len > 12u ? 0x40u : 0u) |
                          (lk.ext_dir.blks[EXT_BLK_ASSET_ANALOG].len > 12u ? 0x80u : 0u));
    send_frame_buf(frame, 13u);
}

typedef struct {
    uint8_t page[64];
    uint16_t ppos;
    uint16_t woff;
    uint16_t dst_off;
    uint16_t out_n;
    uint8_t do_write;
    uint8_t ok;
} pb_writer_t;

static void pb_emit(pb_writer_t *w, uint8_t b) {
    w->out_n++;
    if (!w->do_write || !w->ok) return;
    w->page[w->ppos++] = b;
    if (w->ppos == sizeof w->page) {
        if (!hal_ext_flash_write(LINK_EXT_OFF_POOL + w->dst_off + w->woff,
                                 w->page, w->ppos)) {
            w->ok = 0u;
        }
        w->woff = (uint16_t)(w->woff + w->ppos);
        w->ppos = 0u;
    }
}

/* PackBits RLE 流式编码：do_write=0 统计压缩体积；do_write=1 通过 64B 页缓冲直写 AT24C256，零额外 RAM 缓冲。 */
static uint16_t packbits_stream(const uint8_t *src, uint16_t raw_len,
                                int do_write, uint16_t dst_off, int *ok_out) {
    pb_writer_t w;
    uint16_t i = 0u;

    w.ppos = 0u; w.woff = 0u; w.dst_off = dst_off; w.out_n = 0u;
    w.do_write = (uint8_t)(do_write ? 1u : 0u);
    w.ok = 1u;

    while (i < raw_len) {
        uint16_t r = 1u;
        while (i + r < raw_len && r < 129u && src[i + r] == src[i]) r++;
        if (r >= 3u) {
            pb_emit(&w, (uint8_t)(0x80u | (r - 2u)));
            pb_emit(&w, src[i]);
            i = (uint16_t)(i + r);
        } else {
            uint16_t l = 0u, j;
            while (i + l < raw_len && l < 128u) {
                if (l > 0u && i + l + 2u < raw_len &&
                    src[i + l] == src[i + l + 1] &&
                    src[i + l] == src[i + l + 2]) break;
                l++;
            }
            pb_emit(&w, (uint8_t)(l - 1u));
            for (j = 0u; j < l; j++) pb_emit(&w, src[i + j]);
            i = (uint16_t)(i + l);
        }
    }
    if (w.do_write && w.ok && w.ppos > 0u) {
        if (!hal_ext_flash_write(LINK_EXT_OFF_POOL + w.dst_off + w.woff,
                                 w.page, w.ppos)) {
            w.ok = 0u;
        }
    }
    if (ok_out) *ok_out = w.ok;
    return w.out_n;
}

static int ext_store_blob(const uint8_t *raw, uint16_t raw_len, int top_down,
                          uint16_t *off_slot, uint16_t *len_slot) {
    uint16_t comp_n, store_n, off = 0u;
    int ok = 1;

    *off_slot = 0u;
    *len_slot = 0u;
    if (!hal_ext_flash_present() || !raw || raw_len == 0u) {
        return 0;
    }
    comp_n = packbits_stream(raw, raw_len, 0, 0u, 0);
    store_n = (comp_n > 0u && comp_n < raw_len) ? comp_n : raw_len;
    if (!ext_pool_alloc(store_n, top_down, &off)) {
        return 0;
    }
    if (store_n < raw_len) {
        (void)packbits_stream(raw, raw_len, 1, off, &ok);
    } else {
        ok = hal_ext_flash_write(LINK_EXT_OFF_POOL + off, raw, raw_len);
    }
    if (!ok) {
        return 0;
    }
    *off_slot = off;
    *len_slot = store_n;
    return 1;
}

typedef struct {
    uint8_t rb[32];
    uint16_t base;
    uint16_t blen;
    uint16_t rpos;
    uint16_t roff;
    uint16_t rlen;
} rb_reader_t;

static int rb_getc(rb_reader_t *r, uint8_t *out) {
    if (r->roff >= r->rlen) {
        uint16_t rem = (uint16_t)(r->blen - r->rpos);
        uint16_t chunk = rem > (uint16_t)sizeof r->rb ? (uint16_t)sizeof r->rb : rem;
        if (chunk == 0u || !hal_ext_flash_read(LINK_EXT_OFF_POOL + r->base + r->rpos,
                                               r->rb, chunk)) {
            return 0;
        }
        r->rpos = (uint16_t)(r->rpos + chunk);
        r->roff = 0u;
        r->rlen = chunk;
    }
    *out = r->rb[r->roff++];
    return 1;
}

static int ext_load_blob(uint16_t off, uint16_t blen, uint8_t *dst, uint16_t raw_len) {
    rb_reader_t r;
    uint16_t out = 0u;

    if (!hal_ext_flash_present() || !dst || !blk_ok(off, blen)) {
        return 0;
    }
    if (blen == raw_len) {
        return hal_ext_flash_read(LINK_EXT_OFF_POOL + off, dst, raw_len);
    }
    r.base = off; r.blen = blen; r.rpos = 0u; r.roff = 0u; r.rlen = 0u;
    while (out < raw_len) {
        uint8_t hdr, v;
        int n, i;
        if (!rb_getc(&r, &hdr)) return 0;
        if (hdr & 0x80u) {
            n = (hdr & 0x7Fu) + 2;
            if (!rb_getc(&r, &v)) return 0;
            for (i = 0; i < n && out < raw_len; i++) dst[out++] = v;
        } else {
            n = (hdr & 0x7Fu) + 1;
            for (i = 0; i < n && out < raw_len; i++) {
                if (!rb_getc(&r, &v)) return 0;
                dst[out++] = v;
            }
        }
    }
    return out == raw_len;
}

static void ext_save_msg_blob(int slot, int is_det, const uint8_t *raw, uint16_t raw_len) {
    ext_blk_t *blk;

    if (slot < 0 || slot >= LINK_EXT_MSG_MAX) {
        return;
    }
    if (slot >= (int)lk.ext_dir.msg_count) {
        lk.ext_dir.msg_count = (uint8_t)(slot + 1);
    }
    blk = &lk.ext_dir.blks[EXT_BLK_MSG0 + slot * 2 + (is_det ? 1 : 0)];
    blk->off = 0u;
    blk->len = 0u;
    (void)ext_store_blob(raw, raw_len, 1, &blk->off, &blk->len);
}

static void ext_save_msg_row(int slot, const uint8_t *row352) {
    ext_save_msg_blob(slot, 0, row352, LINK_ROW_BYTES);
}

static void ext_save_msg_det(int slot, const uint8_t *det2560) {
    ext_save_msg_blob(slot, 1, det2560, LINK_MASK_BYTES);
}

static void free_rx_scratch(void) {
    if (lk.rx_row) {
        ui_mem_free(lk.rx_row);
        lk.rx_row = 0;
    }
    if (lk.rx_det) {
        ui_mem_free(lk.rx_det);
        lk.rx_det = 0;
    }
    if (lk.rx_asset) {
        ui_mem_free(lk.rx_asset);
        lk.rx_asset = 0;
    }
    lk.rx_asset_id = 0u;
    lk.rx_asset_need = 0u;
}

static void free_c3d_buf(void) {
    if (lk.load_kind == DYN_COMPASS3D) {
        lk.load_need = 0u;
        lk.load_pos = 0u;
        lk.load_kind = DYN_NONE;
        lk.load_crc = 0u;
        lk.load_entry = 0u;
    }
    if (lk.c3d_buf) {
        ui_mem_free(lk.c3d_buf);
        lk.c3d_buf = 0;
    }
    lk.c3d_entry = 0u;
    lk.c3d_total = 0u;
}

static void free_dyn_buf(void) {
    if (lk.dyn_buf) {
        ui_mem_free(lk.dyn_buf);
        lk.dyn_buf = 0;
    }
    lk.dyn_cap = 0;
    lk.dyn_kind = DYN_NONE;
    if (lk.load_kind != DYN_COMPASS3D) {
        lk.load_need = 0u;
        lk.load_pos = 0u;
        lk.load_kind = DYN_NONE;
        lk.load_crc = 0u;
        lk.load_entry = 0u;
    }
    lk.pic_ready_kind = DYN_NONE;
    lk.pic_entry = 0u;
    memset(lk.win_row_id, 0xFF, sizeof lk.win_row_id);
}

static void free_hdr_buf(void) {
    if (lk.hdr_buf) {
        ui_mem_free(lk.hdr_buf);
        lk.hdr_buf = 0;
    }
    lk.hdr_loaded = 0u;
    lk.hdr_wr = 0u;
    lk.hdr_slot_next = 0u;
    memset(lk.hdr_slot_cp, 0, sizeof lk.hdr_slot_cp);
    memset(lk.hdr_slot_off, 0, sizeof lk.hdr_slot_off);
}

static uint16_t ext_flash_crc16(uint32_t abs_off, uint32_t len) {
    uint8_t buf[32];
    uint16_t crc = 0xFFFFu;
    uint32_t pos = 0u;

    while (pos < len) {
        uint32_t n = (len - pos > (uint32_t)sizeof buf) ? (uint32_t)sizeof buf : (len - pos);
        if (!hal_ext_flash_read(abs_off + pos, buf, n)) {
            return 0u;
        }
        crc = settings_crc16_step(crc, buf, n);
        pos += n;
    }
    return crc;
}

static void load_about_meta(void) {
    ext_blk_t *b = &lk.ext_dir.blks[EXT_BLK_ASSET_ABOUT];
    uint8_t hdr[16];
    uint16_t off0, total, crc;

    lk.about_ready = 0u;
    lk.about_total = 0u;
    lk.about_crc = 0u;
    if (!hal_ext_flash_present() || b->len < 40u) {
        return;
    }
    if (!hal_ext_flash_read(LINK_EXT_OFF_POOL + (uint32_t)b->off, hdr, sizeof hdr)) {
        return;
    }
    if (u32_get(hdr + 0) != 0x32544241u) { /* 'ABT2' */
        return;
    }
    off0 = u16_get(hdr + 8);
    total = u16_get(hdr + 10);
    crc = u16_get(hdr + 12);
    if (!hdr[4] || !hdr[5] || hdr[6] != 3u ||
        off0 != 40u || total <= off0 || total > 8192u || total > b->len) {
        return;
    }
    lk.about_total = total;
    lk.about_crc = crc;
    lk.about_ready = 1u;
}

static void load_hdr_meta(void) {
    ext_blk_t *b = &lk.ext_dir.blks[EXT_BLK_ASSET_HEADERS];
    uint8_t hdr[12];
    uint16_t cnt, cov, total;

    lk.hdr_ready = 0u;
    lk.hdr_count = 0u;
    lk.hdr_cover_off = 0u;
    lk.hdr_total = 0u;
    if (!hal_ext_flash_present() || b->len < 24u) {
        return;
    }
    if (!hal_ext_flash_read(LINK_EXT_OFF_POOL + (uint32_t)b->off, hdr, sizeof hdr)) {
        return;
    }
    if (u32_get(hdr + 0) != 0x31444853u) { /* 'SHD1' */
        return;
    }
    cnt = u16_get(hdr + 4);
    cov = u16_get(hdr + 6);
    total = u16_get(hdr + 8);
    if (cnt == 0u || cnt > 32u || cov < 12u + cnt * 10u ||
        cov - 12u > HDR_IDX_BYTES || total <= cov || total > b->len) {
        return;
    }
    lk.hdr_count = (uint8_t)cnt;
    lk.hdr_cover_off = cov;
    lk.hdr_total = total;
    lk.hdr_ready = 1u;
}

void link_clear_ext_messages(void) {
    memset(&lk.ext_dir.blks[EXT_BLK_MSG0], 0, LINK_EXT_MSG_MAX * 2 * sizeof(ext_blk_t));
    lk.ext_dir.msg_count = 0u;
    lk.list_target_n = 0u;
    memset(lk.win_row_id, 0xFF, sizeof lk.win_row_id);
    lk.msg_list_ready = 0;
    lk.msg_list_loading = 0;
    lk.msg_list_empty = 1;
    lk.msg_detail_ready = 0;
    lk.msg_detail_loading = 0;
    free_rx_scratch();
    if (lk.dyn_kind == DYN_MSG_LIST || lk.dyn_kind == DYN_MSG_DETAIL) {
        free_dyn_buf();
    }
    ext_dir_save();
    ui_app_set_message_count(0);
    send_ext_pool_state();
    ui_draw_invalidate();
}

static void validate_blk(uint16_t *off, uint16_t *blen) {
    if (!blk_ok(*off, *blen)) {
        *off = 0u;
        *blen = 0u;
    }
}

void link_load_ext(void) {
    uint8_t rec[EXT_DIR_RECLEN];
    uint32_t seq0 = 0u, seq1 = 0u;
    int ok0, ok1, i;
    settings_t *cfg = settings_get();

    memset(&lk.ext_dir, 0, sizeof lk.ext_dir);
    memset(lk.win_row_id, 0xFF, sizeof lk.win_row_id);
    lk.about_ready = 0u;
    lk.hdr_ready = 0u;
    free_hdr_buf();
    if (!hal_ext_flash_present()) {
        ui_app_set_message_count(0);
        return;
    }
    ok0 = ext_dir_read_slot(EXT_DIR_SLOT0, rec);
    if (ok0) seq0 = u32_get(rec + 4);
    ok1 = ext_dir_read_slot(EXT_DIR_SLOT1, rec);
    if (ok1) seq1 = u32_get(rec + 4);
    if (!ok0 && !ok1) {
        ui_app_set_message_count(0);
        return;
    }
    if (ok0 && (!ok1 || (int32_t)(seq0 - seq1) >= 0)) {
        if (!ext_dir_read_slot(EXT_DIR_SLOT0, rec)) {
            ui_app_set_message_count(0);
            return;
        }
        lk.ext_dir.seq = seq0;
        lk.ext_dir.active_slot = 0u;
    } else {
        lk.ext_dir.seq = seq1;
        lk.ext_dir.active_slot = 1u;
    }
    lk.ext_dir.msg_count = rec[9] <= LINK_EXT_MSG_MAX ? rec[9] : LINK_EXT_MSG_MAX;
    {
        int used_blks = EXT_BLK_MSG0 + (int)lk.ext_dir.msg_count * 2;
        if (used_blks > EXT_BLK_ASSET0) used_blks = EXT_BLK_ASSET0;
        for (i = 0; i < used_blks; i++) {
            lk.ext_dir.blks[i].off = u16_get(rec + 10 + i * 4);
            lk.ext_dir.blks[i].len = u16_get(rec + 12 + i * 4);
            validate_blk(&lk.ext_dir.blks[i].off, &lk.ext_dir.blks[i].len);
        }
        for (i = EXT_BLK_ASSET0; i < EXT_BLK_TOTAL; i++) {
            lk.ext_dir.blks[i].off = u16_get(rec + 10 + i * 4);
            lk.ext_dir.blks[i].len = u16_get(rec + 12 + i * 4);
            validate_blk(&lk.ext_dir.blks[i].off, &lk.ext_dir.blks[i].len);
        }
    }
    load_about_meta();
    load_hdr_meta();
    if (!cfg->img_ready || (!cfg->img_on_chip && lk.ext_dir.blks[EXT_BLK_IMG].len == 0u)) {
        lk.ext_dir.blks[EXT_BLK_IMG].off = 0u;
        lk.ext_dir.blks[EXT_BLK_IMG].len = 0u;
        cfg->img_ready = 0u;
        cfg->img_on_chip = 0u;
    } else {
        lk.img_ready = 1u;
    }
    if (cfg->msg_auto_clear && lk.ext_dir.msg_count > 0u) {
        link_clear_ext_messages();
        return;
    }
    ui_app_set_message_count(lk.ext_dir.msg_count);
}

uint32_t link_ext_img_offset(void) {
    return (lk.ext_dir.blks[EXT_BLK_IMG].len > 0u)
               ? (LINK_EXT_OFF_POOL + (uint32_t)lk.ext_dir.blks[EXT_BLK_IMG].off)
               : LINK_EXT_OFF_IMG;
}

static uint8_t *ensure_dyn_buf(uint8_t kind, uint16_t size) {
    if (kind != DYN_SPORT && lk.c3d_buf) {
        free_c3d_buf();
    }
    if (lk.dyn_buf && lk.dyn_kind == kind && lk.dyn_cap == size) {
        return lk.dyn_buf;
    }
    free_dyn_buf();
    lk.dyn_buf = (uint8_t *)ui_mem_alloc(size, 4);
    if (!lk.dyn_buf && lk.c3d_buf) {
        free_c3d_buf();
        lk.dyn_buf = (uint8_t *)ui_mem_alloc(size, 4);
    }
    if (lk.dyn_buf) {
        lk.dyn_cap = size;
        lk.dyn_kind = kind;
    }
    return lk.dyn_buf;
}

static void expand_media_row_chunk(uint8_t *media_dst, uint8_t chunk, const uint8_t *src) {
    const int rows = LINK_BMP_H / LINK_BMP_CHUNKS;
    int r, c;

    for (r = 0; r < rows; r++) {
        uint8_t *dst = media_dst + (chunk * rows + r) * (LINK_BMP_W / 2);
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
}


#if !defined(AT32_SLIM_ROM)
static void synth_compass_asset(uint8_t *dst) {
    uint8_t *pix = dst + 4;
    int x, y;

    u16_put(dst + 0, LINK_ASSET_COMPASS_W);
    u16_put(dst + 2, LINK_ASSET_COMPASS_H);
    memset(pix, 0, LINK_ASSET_COMPASS_BYTES - 4u);
    for (y = 2; y < 62; y++) {
        int dy = y - 32;
        for (x = 2; x < 62; x++) {
            int dx = x - 32;
            int r2 = dx * dx + dy * dy;
            if (r2 >= 784 && r2 <= 890) {
                pix[y * 8 + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
            }
        }
    }
    for (x = 0; x < 5; x++) {
        int rx = 29 + x;
        pix[(7 + x) * 8 + 3] |= 0x04u;
        pix[(7 + x) * 8 + 4] |= 0x20u;
        pix[(7 + x) * 8 + (rx >> 3)] |= (uint8_t)(0x80u >> (rx & 7));
    }
}
#endif

static int begin_pic_load(int blk_idx, uint32_t magic, uint8_t dyn_kind, uint16_t max_sz) {
    ext_blk_t *b = &lk.ext_dir.blks[blk_idx];
    uint8_t hdr[12];
    uint16_t flags, entry_off, total, crc;
    uint8_t *buf;

    if (dyn_kind != DYN_COMPASS3D) {
        lk.pic_ready_kind = DYN_NONE;
        lk.pic_entry = 0u;
    }
    if (!hal_ext_flash_present() || b->len < 16u || b->len > max_sz) {
        return 0;
    }
    if (!hal_ext_flash_read(LINK_EXT_OFF_POOL + (uint32_t)b->off, hdr, sizeof hdr)) {
        return 0;
    }
    if (u32_get(hdr + 0) != magic) {
        return 0;
    }
    flags = u16_get(hdr + 4);
    entry_off = u16_get(hdr + 6);
    total = u16_get(hdr + 8);
    crc = u16_get(hdr + 10);
    if ((flags >> 8) != COMPASS3D_ABI_VER || (flags & 1u) == 0u ||
        entry_off < 12u || total <= entry_off || total > b->len) {
        return 0;
    }
    if (dyn_kind == DYN_COMPASS3D) {
        free_c3d_buf();
        buf = (uint8_t *)ui_mem_alloc(total, 4);
        if (!buf) {
            free_dyn_buf();
            buf = (uint8_t *)ui_mem_alloc(total, 4);
        }
        lk.c3d_buf = buf;
    } else {
        buf = ensure_dyn_buf(dyn_kind, total);
    }
    if (!buf) {
        return 0;
    }
    memcpy(buf, hdr, sizeof hdr);
    lk.load_kind = dyn_kind;
    lk.load_entry = entry_off;
    lk.load_crc = crc;
    lk.load_ext_off = LINK_EXT_OFF_POOL + (uint32_t)b->off;
    lk.load_pos = (uint16_t)sizeof hdr;
    lk.load_need = total;
    return 1;
}

static int begin_compass3d_load(void) {
    return begin_pic_load(EXT_BLK_ASSET_COMPASS3D, COMPASS3D_MAGIC, DYN_COMPASS3D, 4096u);
}

static int begin_about_load(void) {
    ext_blk_t *b = &lk.ext_dir.blks[EXT_BLK_ASSET_ABOUT];
    uint8_t *buf;

    if (!lk.about_ready || !hal_ext_flash_present() ||
        lk.about_total < 40u || lk.about_total > b->len) {
        return 0;
    }
    buf = ensure_dyn_buf(DYN_ABOUT, lk.about_total);
    if (!buf) {
        return 0;
    }
    lk.load_kind = DYN_ABOUT;
    lk.load_crc = lk.about_crc;
    lk.load_ext_off = LINK_EXT_OFF_POOL + (uint32_t)b->off;
    lk.load_pos = 0u;
    lk.load_need = lk.about_total;
    return 1;
}

#if !defined(AT32_SLIM_ROM)
static int load_asset_blk(int blk_idx, uint16_t raw_len, uint8_t dyn_kind,
                          void (*synth)(uint8_t *)) {
    uint8_t *buf;
    ext_blk_t *b = &lk.ext_dir.blks[blk_idx];

    if (lk.dyn_kind == dyn_kind && lk.dyn_buf && lk.dyn_cap == raw_len) {
        return 1;
    }
    buf = ensure_dyn_buf(dyn_kind, raw_len);
    if (!buf) {
        return 0;
    }
    if (b->len > 0u && hal_ext_flash_present() &&
        ext_load_blob(b->off, b->len, buf, raw_len) &&
        u16_get(buf + 0) > 0u && u16_get(buf + 2) > 0u) {
        return 1;
    }
    if (!synth) {
        free_dyn_buf();
        return 0;
    }
    synth(buf);
    if (hal_ext_flash_present()) {
        if (ext_store_blob(buf, raw_len, 0, &b->off, &b->len)) {
            ext_dir_save();
        }
    }
    return 1;
}
#endif

static void compass_fallback_load(void) {
    free_c3d_buf();
#if defined(AT32_SLIM_ROM)
    (void)begin_pic_load(EXT_BLK_ASSET_SPORT, PIC_MAGIC_SPORT, DYN_SPORT, 4608u);
#else
    (void)load_asset_blk(EXT_BLK_ASSET_COMPASS, LINK_ASSET_COMPASS_BYTES,
                         DYN_COMPASS, synth_compass_asset);
#endif
}

static void finish_dyn_load(void) {
    uint8_t kind = lk.load_kind;
    uint16_t total = lk.load_need;
    uint16_t crc = lk.load_crc;
    uint16_t entry = lk.load_entry;
    uint8_t *buf = (kind == DYN_COMPASS3D) ? lk.c3d_buf : lk.dyn_buf;

    if ((kind == DYN_COMPASS3D || kind == DYN_SPORT || kind == DYN_ANALOG || kind == DYN_SYS) &&
        buf && total >= 16u &&
        settings_crc16(buf + 12u, (uint32_t)(total - 12u)) == crc) {
        lk.load_need = 0u;
        lk.load_pos = 0u;
        lk.load_kind = DYN_NONE;
        if (kind == DYN_COMPASS3D) {
            lk.c3d_entry = entry;
            lk.c3d_total = total;
        } else {
            lk.pic_ready_kind = kind;
            lk.pic_entry = entry;
        }
        ui_draw_invalidate();
        return;
    }
    if (kind == DYN_ABOUT && buf && total >= 40u) {
        uint16_t off0 = u16_get(buf + 8);
        uint16_t hdr_tot = u16_get(buf + 10);
        uint16_t hdr_crc = u16_get(buf + 12);
        int i, ok = (u32_get(buf + 0) == 0x32544241u && off0 == 40u &&
                     hdr_tot == total && hdr_crc == crc && buf[6] == 3u &&
                     settings_crc16(buf + 40u, (uint32_t)(total - 40u)) == crc);
        for (i = 0; ok && i < 3; i++) {
            const uint8_t *d = buf + 16 + i * 8;
            uint16_t ol = u16_get(d + 0);
            uint16_t orr = u16_get(d + 2);
            uint8_t gw = d[4], gh = d[5], wl = d[6];
            uint8_t wr = (uint8_t)(gw - wl);
            if (!gw || !gh || wl == 0u || wl > 64u || wr > 64u ||
                ol < off0 || ol >= total || orr < ol || (wr > 0u ? (orr >= total) : (orr > total))) {
                ok = 0;
            }
        }
        if (ok) {
            lk.load_need = 0u;
            lk.load_pos = 0u;
            lk.load_kind = DYN_NONE;
            ui_draw_invalidate();
            return;
        }
    }
    if (kind == DYN_COMPASS3D) {
        compass_fallback_load();
    } else {
        free_dyn_buf();
    }
    ui_draw_invalidate();
}

void link_asset_pump(uint16_t max_bytes) {
    uint16_t rem, take;
    uint8_t kind = lk.load_kind;
    uint8_t *dst;

    if (lk.load_need == 0u) {
        return;
    }
    dst = (kind == DYN_COMPASS3D) ? lk.c3d_buf : lk.dyn_buf;
    if (!dst || (kind != DYN_COMPASS3D &&
                 (lk.dyn_kind != kind || lk.dyn_cap < lk.load_need)) ||
        !hal_ext_flash_present()) {
        if (kind == DYN_COMPASS3D) free_c3d_buf();
        else free_dyn_buf();
        return;
    }
    rem = (uint16_t)(lk.load_need - lk.load_pos);
    /* 由调用方按帧预算决定单次载入量，避免 1KB 阻塞把动画帧拉长。 */
    take = (max_bytes > 0u && rem > max_bytes) ? max_bytes : rem;
    if (take > 0u &&
        !hal_ext_flash_read(lk.load_ext_off + lk.load_pos, dst + lk.load_pos, take)) {
        if (kind == DYN_COMPASS3D) {
            compass_fallback_load();
        } else {
            free_dyn_buf();
        }
        ui_draw_invalidate();
        return;
    }
    lk.load_pos = (uint16_t)(lk.load_pos + take);
    if (lk.load_pos >= lk.load_need) {
        finish_dyn_load();
    }
}

void link_asset_finish(void) {
    while (lk.load_need > 0u) {
        link_asset_pump(lk.load_need);
    }
}

void link_begin_compass(void) {
    lk.media_mask_ready = 0;
    if (lk.c3d_buf && (lk.load_kind == DYN_COMPASS3D || lk.c3d_total >= 16u)) {
        return;
    }
    if (begin_compass3d_load()) {
        return;
    }
#if defined(AT32_SLIM_ROM)
    if (lk.dyn_kind == DYN_SPORT && lk.dyn_buf &&
        (lk.load_need > 0u || lk.pic_ready_kind == DYN_SPORT)) {
        return;
    }
    (void)begin_pic_load(EXT_BLK_ASSET_SPORT, PIC_MAGIC_SPORT, DYN_SPORT, 4608u);
#else
    (void)load_asset_blk(EXT_BLK_ASSET_COMPASS, LINK_ASSET_COMPASS_BYTES,
                         DYN_COMPASS, synth_compass_asset);
#endif
}

void link_enter_compass(void) {
    link_begin_compass();
    link_asset_finish();
}

void link_leave_compass(void) {
    free_c3d_buf();
    if (lk.dyn_kind == DYN_COMPASS || lk.dyn_kind == DYN_COMPASS3D) {
        free_dyn_buf();
    }
}

void link_enter_music(void) {
    if (lk.media_mask_ready && lk.dyn_buf && lk.dyn_kind == DYN_MEDIA) {
        send_ext_pool_state();
        return;
    }
    if (lk.ext_dir.blks[EXT_BLK_MEDIA].len == LINK_BMP_W * LINK_BMP_H / 8 && hal_ext_flash_present()) {
        uint8_t *buf = ensure_dyn_buf(DYN_MEDIA, LINK_MEDIA_BYTES);
        uint8_t raw[LINK_BMP_CHUNK];
        uint8_t ch;
        if (buf) {
            int ok = 1;
            for (ch = 0; ch < LINK_BMP_CHUNKS; ch++) {
                if (!hal_ext_flash_read(LINK_EXT_OFF_POOL +
                                            (uint32_t)lk.ext_dir.blks[EXT_BLK_MEDIA].off +
                                            (uint32_t)ch * LINK_BMP_CHUNK,
                                        raw, LINK_BMP_CHUNK)) {
                    ok = 0;
                    break;
                }
                expand_media_row_chunk(buf, ch, raw);
            }
            if (ok) {
                lk.media_mask_ready = 1;
                send_ext_pool_state();
                return;
            }
            free_dyn_buf();
        }
    }
    send_ext_pool_state();
}

void link_leave_music(void) {
    lk.media_mask_ready = 0;
    if (lk.dyn_kind == DYN_MEDIA) {
        free_dyn_buf();
    }
    send_ext_pool_state();
}

void link_request_messages(void) {
    lk.msg_detail_ready = 0;
    lk.msg_detail_loading = 0;
    lk.media_mask_ready = 0;
    if (lk.ext_dir.msg_count > 0u && !lk.msg_list_empty && hal_ext_flash_present()) {
        uint8_t *buf = ensure_dyn_buf(DYN_MSG_LIST, LINK_LIST_BYTES);
        if (buf) {
            memset(lk.win_row_id, 0xFF, sizeof lk.win_row_id);
            ui_app_set_message_count(lk.ext_dir.msg_count);
            lk.msg_list_ready = 1;
            lk.msg_list_loading = 0;
            send_empty(LINK_MSG_LIST_REQ);
            return;
        }
    }
    if (lk.dyn_kind != DYN_MSG_LIST) {
        free_dyn_buf();
    }
    lk.msg_list_ready = 0;
    lk.msg_list_loading = 1;
    lk.msg_list_empty = 0;
    send_empty(LINK_MSG_LIST_REQ);
}

void link_request_message_detail(int index) {
    uint8_t frame[5];
    if (index < 0) {
        index = 0;
    }
    if (index >= LINK_EXT_MSG_MAX) {
        index = LINK_EXT_MSG_MAX - 1;
    }
    lk.detail_slot = (uint8_t)index;
    lk.msg_list_ready = 0;
    lk.msg_list_loading = 0;
    lk.media_mask_ready = 0;
    if (index < (int)lk.ext_dir.msg_count &&
        lk.ext_dir.blks[EXT_BLK_MSG0 + index * 2 + 1].len > 0u &&
        hal_ext_flash_present()) {
        uint8_t *buf = ensure_dyn_buf(DYN_MSG_DETAIL, LINK_MASK_BYTES);
        if (buf && ext_load_blob(lk.ext_dir.blks[EXT_BLK_MSG0 + index * 2 + 1].off,
                                 lk.ext_dir.blks[EXT_BLK_MSG0 + index * 2 + 1].len,
                                 buf, LINK_MASK_BYTES)) {
            lk.msg_detail_ready = 1;
            lk.msg_detail_loading = 0;
            goto send_req;
        }
    }
    if (lk.dyn_kind != DYN_MSG_DETAIL) {
        free_dyn_buf();
    }
    lk.msg_detail_ready = 0;
    lk.msg_detail_loading = 1;
send_req:
    frame[1] = LINK_MSG_DETAIL_REQ;
    frame[3] = (uint8_t)index;
    send_frame_buf(frame, 1u);
}

void link_leave_messages(void) {
    lk.msg_list_ready = 0;
    lk.msg_list_loading = 0;
    lk.msg_list_empty = 0;
    lk.msg_detail_ready = 0;
    lk.msg_detail_loading = 0;
    free_rx_scratch();
    if (lk.dyn_kind == DYN_MSG_LIST || lk.dyn_kind == DYN_MSG_DETAIL) {
        free_dyn_buf();
    }
    send_empty(LINK_MSG_LEAVE);
}

/* 块号 0..3，每块 128 字节 = 8 行 1bpp；动态存入片外池并按需展开成 4bpp 覆盖位图。 */
static void mask_chunk(uint8_t chunk, const uint8_t *src, int n) {
    uint8_t *buf;

    if (ui_app_message_active() || !ui_app_music_active()) {
        lk.media_staging_ok = 0u;
        return;
    }
    if (chunk >= LINK_BMP_CHUNKS || n < LINK_BMP_CHUNK) {
        return;
    }
    if (chunk == 0) {
        lk.media_mask_ready = 0;
        lk.ext_dir.blks[EXT_BLK_MEDIA].off = 0u;
        lk.ext_dir.blks[EXT_BLK_MEDIA].len = 0u;
        lk.media_staging_ok = (uint8_t)ext_pool_alloc(LINK_BMP_CHUNKS * LINK_BMP_CHUNK, 1, &lk.media_staging_off);
        lk.msg_list_ready = 0;
        lk.msg_list_loading = 0;
        lk.msg_list_empty = 0;
        lk.msg_detail_ready = 0;
        lk.msg_detail_loading = 0;
    }
    if (lk.media_staging_ok) {
        (void)hal_ext_flash_write(LINK_EXT_OFF_POOL + (uint32_t)lk.media_staging_off +
                                  (uint32_t)chunk * LINK_BMP_CHUNK,
                                  src, LINK_BMP_CHUNK);
        if (chunk == LINK_BMP_CHUNKS - 1) {
            lk.ext_dir.blks[EXT_BLK_MEDIA].off = lk.media_staging_off;
            lk.ext_dir.blks[EXT_BLK_MEDIA].len = LINK_BMP_CHUNKS * LINK_BMP_CHUNK;
            ext_dir_save();
        }
    }
    buf = ensure_dyn_buf(DYN_MEDIA, LINK_MEDIA_BYTES);
    if (!buf) {
        return;
    }
    expand_media_row_chunk(buf, chunk, src);
    if (chunk == LINK_BMP_CHUNKS - 1) {
        lk.media_mask_ready = 1;
    }
}

/* 128×(N*22) 可变长消息列表，1bpp，逐行 PackBits 压缩后动态写入 AT24C256 数据池。 */
static void rx_row_finish(uint8_t row_idx) {
    if (row_idx < LINK_EXT_MSG_MAX) {
        ext_save_msg_row((int)row_idx, lk.rx_row);
        if (lk.dyn_kind == DYN_MSG_LIST && lk.dyn_buf) {
            uint8_t ws = (uint8_t)(row_idx % LINK_LIST_WIN_ROWS);
            memcpy(lk.dyn_buf + ws * LINK_ROW_BYTES, lk.rx_row, LINK_ROW_BYTES);
            lk.win_row_id[ws] = row_idx;
        }
    }
}

static void msg_list_chunk(uint8_t chunk, const uint8_t *src, int n) {
    uint8_t count;
    uint16_t total, pos, rem;
    uint8_t r;
    int k;

    if (n < LINK_BMP_CHUNK) {
        return;
    }
    if (lk.dyn_kind == DYN_MSG_DETAIL && ui_app_message_active()) {
        return;
    }
    if (chunk == 0) {
        if (lk.list_target_n == 0u) {
            lk.list_target_n = 5u;
        }
        lk.msg_list_empty = 0;
        if (ui_app_message_active() == UI_MENU_LIST) {
            lk.msg_list_ready = 0;
            lk.msg_list_loading = 1;
            lk.msg_detail_ready = 0;
            lk.msg_detail_loading = 0;
            lk.media_mask_ready = 0;
            (void)ensure_dyn_buf(DYN_MSG_LIST, LINK_LIST_BYTES);
        }
        if (!lk.rx_row) {
            lk.rx_row = (uint8_t *)ui_mem_alloc(LINK_ROW_BYTES, 4);
        }
    }
    count = lk.list_target_n ? lk.list_target_n : 5u;
    if (count > LINK_EXT_MSG_MAX) count = LINK_EXT_MSG_MAX;
    total = (uint16_t)count * LINK_ROW_BYTES;
    pos = (uint16_t)((uint16_t)chunk * LINK_BMP_CHUNK);
    if (pos >= total || !lk.rx_row) {
        return;
    }
    r = 0u;
    rem = pos;
    while (rem >= LINK_ROW_BYTES) {
        rem -= LINK_ROW_BYTES;
        r++;
    }
    k = 0;
    while (k < LINK_BMP_CHUNK && pos < total) {
        int rem_r = (int)(LINK_ROW_BYTES - rem);
        int rem_c = LINK_BMP_CHUNK - k;
        int take = rem_r < rem_c ? rem_r : rem_c;
        if ((uint16_t)(pos + take) > total) take = (int)(total - pos);
        memcpy(lk.rx_row + rem, src + k, (size_t)take);
        rem = (uint16_t)(rem + take);
        if (rem == LINK_ROW_BYTES) {
            if (r < count) {
                rx_row_finish(r);
            }
            rem = 0u;
            r++;
        }
        pos = (uint16_t)(pos + take);
        k += take;
    }
    if ((uint16_t)((uint16_t)(chunk + 1u) * LINK_BMP_CHUNK) >= total) {
        ui_mem_free(lk.rx_row);
        lk.rx_row = 0;
        lk.msg_list_empty = 0;
        lk.ext_dir.msg_count = count;
        ext_dir_save();
        ui_app_set_message_count(count);
        if (ui_app_message_active() == UI_MENU_LIST) {
            if (!lk.dyn_buf || lk.dyn_kind != DYN_MSG_LIST) {
                (void)ensure_dyn_buf(DYN_MSG_LIST, LINK_LIST_BYTES);
            }
            if (lk.dyn_buf && lk.dyn_kind == DYN_MSG_LIST) {
                lk.msg_list_ready = 1;
            }
            lk.msg_list_loading = 0;
        }
        ui_draw_invalidate();
    }
}

/* 128x160 详情图，1bpp，20 块；压缩后动态存入 AT24C256（支持 0..19 槽位）。 */
static void msg_detail_slot_chunk(uint8_t slot, uint8_t chunk, const uint8_t *src, int n) {
    uint8_t *buf = 0;
    int active_view = (ui_app_message_active() == UI_MENU_CHILD) && slot == lk.detail_slot;

    if (slot >= LINK_EXT_MSG_MAX || chunk >= LINK_MSG_DETAIL_CHUNKS || n < LINK_BMP_CHUNK) {
        return;
    }
    if (active_view) {
        if (chunk == 0) {
            lk.msg_detail_ready = 0;
            lk.msg_detail_loading = 1;
            lk.msg_list_ready = 0;
            lk.msg_list_loading = 0;
            lk.msg_list_empty = 0;
            lk.media_mask_ready = 0;
        }
        buf = ensure_dyn_buf(DYN_MSG_DETAIL, LINK_MASK_BYTES);
    } else {
        if (chunk == 0) {
            if (lk.rx_det) {
                ui_mem_free(lk.rx_det);
                lk.rx_det = 0;
            }
            lk.rx_det = (uint8_t *)ui_mem_alloc(LINK_MASK_BYTES, 4);
            lk.rx_det_slot = slot;
        }
        if (lk.rx_det_slot == slot) {
            buf = lk.rx_det;
        }
    }
    if (!buf) {
        return;
    }
    memcpy(buf + chunk * LINK_BMP_CHUNK, src, LINK_BMP_CHUNK);
    if (chunk == LINK_MSG_DETAIL_CHUNKS - 1) {
        ext_save_msg_det((int)slot, buf);
        ext_dir_save();
        if (active_view) {
            lk.msg_detail_ready = 1;
            lk.msg_detail_loading = 0;
        } else {
            ui_mem_free(lk.rx_det);
            lk.rx_det = 0;
        }
    }
}

static void msg_detail_chunk(uint8_t chunk, const uint8_t *src, int n) {
    msg_detail_slot_chunk(lk.detail_slot, chunk, src, n);
}

/* 0x29：图片分块写入（适合热槽时直写 MCU 片内 Flash，大容量回退 AT24C256 动态池）。 */
static void ext_img_chunk(uint8_t chunk, const uint8_t *src, int n) {
    uint32_t off;
    uint32_t write_n;

    if (lk.img_total_bytes == 0 || n <= 0) {
        return;
    }
    off = (uint32_t)chunk * LINK_BMP_CHUNK;
    if (off >= lk.img_total_bytes) {
        return;
    }
    write_n = (uint32_t)n;
    if (off + write_n > lk.img_total_bytes) {
        write_n = lk.img_total_bytes - off;
    }
    if (lk.img_to_chip) {
        if (!hal_int_img_write(off, src, write_n)) {
            return;
        }
    } else if (!hal_ext_flash_write(LINK_EXT_OFF_POOL + (uint32_t)lk.img_staging_off + off,
                                    src, write_n)) {
        return;
    }
    if (off + write_n >= lk.img_total_bytes) {
        if (lk.img_to_chip) {
            lk.ext_dir.blks[EXT_BLK_IMG].off = 0u;
            lk.ext_dir.blks[EXT_BLK_IMG].len = 0u;
        } else {
            lk.ext_dir.blks[EXT_BLK_IMG].off = lk.img_staging_off;
            lk.ext_dir.blks[EXT_BLK_IMG].len = lk.img_total_bytes;
        }
        ext_dir_save();
        lk.img_ready = 1;
        lk.img_gen++;
        if (lk.img_apply_dial) {
            settings_t *s = settings_get();
            s->img_w = lk.img_w;
            s->img_h = lk.img_h;
            s->img_fmt = lk.img_fmt;
            s->img_bytes = lk.img_total_bytes;
            s->img_ready = 1u;
            s->img_on_chip = lk.img_to_chip ? 1u : 0u;
            s->dial_id = UI_DIAL_PHOTO;
            lk.img_msg_preview = 0;
            (void)settings_save_ext(UI_CFG_PRIO_APP);
            send_cfg_state();
        } else {
            lk.img_msg_preview = 1;
        }
        send_ext_pool_state();
        ui_draw_invalidate();
    }
}

/* 0x2F：按需功能页片外 ROM 静态资源下发。 */
static void ext_asset_chunk(const uint8_t *p, int l) {
    uint8_t id, chunk;
    uint16_t a0, a1, off, take;

    if (l < 7) {
        return;
    }
    id = p[0];
    chunk = p[1];
    a0 = u16_get(p + 2);
    a1 = u16_get(p + 4);

    if (id >= LINK_ASSET_COMPASS3D && id <= LINK_ASSET_SYS) {
        static const uint8_t blk_map[6] = {
            EXT_BLK_ASSET_COMPASS3D,
            EXT_BLK_ASSET_ABOUT,
            EXT_BLK_ASSET_HEADERS,
            EXT_BLK_ASSET_SPORT,
            EXT_BLK_ASSET_ANALOG,
            EXT_BLK_ASSET_SYS
        };
        static const uint16_t max_map[6] = { 4096u, 8192u, 6144u, 4608u, 3072u, 4096u };
        int idx = (int)(id - LINK_ASSET_COMPASS3D);
        int blk_idx = blk_map[idx];
        uint16_t max_sz = max_map[idx];
        if (a0 < 16u || a0 > max_sz || !hal_ext_flash_present()) {
            return;
        }
        if (chunk == 0u) {
            lk.ext_dir.blks[blk_idx].off = 0u;
            lk.ext_dir.blks[blk_idx].len = 0u;
            if (id == LINK_ASSET_ABOUT) {
                lk.about_ready = 0u;
                lk.about_total = 0u;
                lk.about_crc = 0u;
                if (lk.dyn_kind == DYN_ABOUT) {
                    free_dyn_buf();
                }
            } else if (id == LINK_ASSET_HEADERS) {
                lk.hdr_ready = 0u;
                free_hdr_buf();
            } else if ((id == LINK_ASSET_SPORT && lk.dyn_kind == DYN_SPORT) ||
                       (id == LINK_ASSET_ANALOG && lk.dyn_kind == DYN_ANALOG) ||
                       (id == LINK_ASSET_SYS && lk.dyn_kind == DYN_SYS)) {
                free_dyn_buf();
            }
            if (!ext_pool_alloc(a0, 0, &lk.rx_asset_off)) {
                lk.rx_asset_id = 0u;
                lk.rx_asset_need = 0u;
                return;
            }
            lk.rx_asset_id = id;
            lk.rx_asset_need = a0;
            lk.rx_asset_crc = a1;
        }
        if (lk.rx_asset_id != id || lk.rx_asset_need != a0 || lk.rx_asset_crc != a1) {
            return;
        }
        off = (uint16_t)((uint16_t)chunk * LINK_BMP_CHUNK);
        if (off >= lk.rx_asset_need) {
            return;
        }
        take = (uint16_t)(l - 6);
        if (off + take > lk.rx_asset_need) {
            take = (uint16_t)(lk.rx_asset_need - off);
        }
        if (!hal_ext_flash_write(LINK_EXT_OFF_POOL + (uint32_t)lk.rx_asset_off + off, p + 6, take)) {
            return;
        }
        if (off + take >= lk.rx_asset_need) {
            uint16_t got_crc = ext_flash_crc16(LINK_EXT_OFF_POOL + (uint32_t)lk.rx_asset_off,
                                               lk.rx_asset_need);
            if (got_crc == lk.rx_asset_crc) {
                lk.ext_dir.blks[blk_idx].off = lk.rx_asset_off;
                lk.ext_dir.blks[blk_idx].len = lk.rx_asset_need;
                ext_dir_save();
                if (id == LINK_ASSET_COMPASS3D &&
                    (lk.c3d_buf || lk.dyn_kind == DYN_COMPASS)) {
                    free_c3d_buf();
                    if (lk.dyn_kind == DYN_COMPASS) {
                        free_dyn_buf();
                    }
                    link_begin_compass();
                } else if (id == LINK_ASSET_ABOUT) {
                    load_about_meta();
                    if (lk.about_want) {
                        link_begin_about();
                    }
                } else if (id == LINK_ASSET_HEADERS) {
                    load_hdr_meta();
                    free_hdr_buf();
                }
            } else {
                lk.ack_bad++;
            }
            lk.rx_asset_id = 0u;
            lk.rx_asset_need = 0u;
            send_ext_pool_state();
            ui_draw_invalidate();
        }
        return;
    }

    if (id != LINK_ASSET_COMPASS || a0 != LINK_ASSET_COMPASS_W || a1 != LINK_ASSET_COMPASS_H) {
        return;
    }
    if (chunk == 0u) {
        if (lk.rx_asset) {
            ui_mem_free(lk.rx_asset);
            lk.rx_asset = 0;
        }
        lk.rx_asset = (uint8_t *)ui_mem_alloc(LINK_ASSET_COMPASS_BYTES, 4);
        if (!lk.rx_asset) {
            lk.rx_asset_id = 0u;
            lk.rx_asset_need = 0u;
            return;
        }
        u16_put(lk.rx_asset + 0, a0);
        u16_put(lk.rx_asset + 2, a1);
        lk.rx_asset_id = id;
        lk.rx_asset_need = LINK_ASSET_COMPASS_BYTES;
    }
    if (!lk.rx_asset || lk.rx_asset_id != id || lk.rx_asset_need != LINK_ASSET_COMPASS_BYTES) {
        return;
    }
    off = (uint16_t)(4u + (uint16_t)chunk * LINK_BMP_CHUNK);
    if (off >= LINK_ASSET_COMPASS_BYTES) {
        return;
    }
    take = (uint16_t)(l - 6);
    if (off + take > LINK_ASSET_COMPASS_BYTES) {
        take = (uint16_t)(LINK_ASSET_COMPASS_BYTES - off);
    }
    memcpy(lk.rx_asset + off, p + 6, take);
    if (off + take >= LINK_ASSET_COMPASS_BYTES) {
        ext_blk_t *b = &lk.ext_dir.blks[EXT_BLK_ASSET_COMPASS];
        if (ext_store_blob(lk.rx_asset, LINK_ASSET_COMPASS_BYTES, 0, &b->off, &b->len)) {
            ext_dir_save();
        }
        ui_mem_free(lk.rx_asset);
        lk.rx_asset = 0;
        lk.rx_asset_id = 0u;
        lk.rx_asset_need = 0u;
        if (lk.dyn_kind == DYN_COMPASS) {
            free_dyn_buf();
            link_begin_compass();
        }
        send_ext_pool_state();
        ui_draw_invalidate();
    }
}

/* 0x26：RLE 压缩的 128 字节点阵块，解压后复用原来的分块函数。 */
static void mask_rle_slot(uint8_t type, uint8_t slot, uint8_t chunk, const uint8_t *src, int n) {
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
    } else if (type == 4) {
        ext_img_chunk(chunk, block, LINK_BMP_CHUNK);
    } else if (type == 5) {
        msg_detail_slot_chunk(slot, chunk, block, LINK_BMP_CHUNK);
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
    const uint8_t c = lk.cmd;
    const uint8_t l = lk.len;
    const uint8_t *const p = lk.pay;

    switch (c) {
    case LINK_TIME:
        if (l == 7 && time_ok(p)) {
            hal_clock_t clock;
            clock.year = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
            clock.month = p[2];
            clock.day = p[3];
            clock.hour = p[4];
            clock.minute = p[5];
            clock.second = p[6];
            hal_clock_set(&clock);
            settings_record_clock(&clock);
            if (!settings_get()->cfg_loaded) {
                (void)settings_save_ext(UI_CFG_PRIO_APP);
            }
        }
        break;
    case LINK_MSG:
#if !defined(AT32_SLIM_ROM)
        if (l >= 1 && p[0] < LINK_MSG_N) {
            copy_text(msgs[p[0]], LINK_MSG_LEN, p + 1, l - 1);
            if (settings_get()->key_beep) {
                hal_beep(HAL_VIB_SMS, 70u);
            }
        }
#endif
        break;
    case LINK_BMP:
        if (l == 1 + LINK_BMP_CHUNK) {
            mask_chunk(p[0], p + 1, l - 1);
        }
        break;
    case LINK_MSG_LIST_BMP:
        if (l == 1 + LINK_BMP_CHUNK) {
            msg_list_chunk(p[0], p + 1, l - 1);
        }
        break;
    case LINK_MSG_DETAIL_BMP:
        if (l == 1 + LINK_BMP_CHUNK) {
            msg_detail_chunk(p[0], p + 1, l - 1);
        } else if (l == 2 + LINK_BMP_CHUNK) {
            msg_detail_slot_chunk(p[0], p[1], p + 2, l - 2);
        }
        break;
    case LINK_MASK_RLE:
        if (l >= 3) {
            if (p[0] == 5 && l >= 4) {
                mask_rle_slot(p[0], p[1], p[2], p + 3, l - 3);
            } else {
                mask_rle_slot(p[0], 0u, p[1], p + 2, l - 2);
            }
        }
        break;
    case LINK_EXT_IMG_META:
        if (l == 5 || l == 8) {
            uint16_t w = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
            uint16_t h = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
            uint8_t fmt = p[4];
            uint32_t total = 0;
            uint8_t apply = 0;
            if (l == 8) {
                total = (uint16_t)(p[5] | ((uint16_t)p[6] << 8));
                apply = (uint8_t)(p[7] & 1u);
            } else {
                uint32_t row_bytes = 0;
                if (fmt == UI_EXT_FMT_1BPP) row_bytes = ((uint32_t)w + 7u) >> 3;
#if !defined(AT32_SLIM_ROM)
                else if (fmt == UI_EXT_FMT_4BPP) row_bytes = ((uint32_t)w + 1u) >> 1;
                else if (fmt == UI_EXT_FMT_RGB565) row_bytes = (uint32_t)w * 2u;
#endif
                total = row_bytes * (uint32_t)h;
            }
            if (w > 0 && w <= 240 && h > 0 && h <= 240 &&
                fmt >= UI_EXT_FMT_1BPP && fmt <= UI_EXT_FMT_RGB565_RLE &&
                total > 0u && total <= LINK_EXT_IMG_MAX) {
                lk.ext_dir.blks[EXT_BLK_IMG].off = 0u;
                lk.ext_dir.blks[EXT_BLK_IMG].len = 0u;
                if (apply && total <= hal_int_img_cap()) {
                    lk.img_to_chip = 1u;
                    lk.img_w = (int16_t)w;
                    lk.img_h = (int16_t)h;
                    lk.img_fmt = fmt;
                    lk.img_total_bytes = (uint16_t)total;
                    lk.img_apply_dial = apply;
                    lk.img_ready = 0;
                    lk.img_msg_preview = 0;
                } else if (ext_pool_alloc((uint16_t)total, 0, &lk.img_staging_off)) {
                    lk.img_to_chip = 0u;
                    lk.img_w = (int16_t)w;
                    lk.img_h = (int16_t)h;
                    lk.img_fmt = fmt;
                    lk.img_total_bytes = (uint16_t)total;
                    lk.img_apply_dial = apply;
                    lk.img_ready = 0;
                    if (!apply) {
                        lk.img_msg_preview = 0;
                    }
                } else {
                    lk.img_total_bytes = 0u;
                    lk.img_to_chip = 0u;
                }
            }
        }
        break;
    case LINK_EXT_IMG_DATA:
        if (l >= 2) {
            ext_img_chunk(p[0], p + 1, l - 1);
        }
        break;
    case LINK_EXT_OP:
        if (l >= 1) {
            if (p[0] == 0u) {
                send_ext_pool_state();
            } else if (p[0] == 1u) {
                int n = lk.ext_dir.msg_count >= LINK_EXT_MSG_MAX ? LINK_EXT_MSG_MAX - 1 : (int)lk.ext_dir.msg_count;
                if (n > 0) {
                    memmove(&lk.ext_dir.blks[EXT_BLK_MSG0 + 2],
                            &lk.ext_dir.blks[EXT_BLK_MSG0],
                            (size_t)n * 2u * sizeof(ext_blk_t));
                }
                memset(&lk.ext_dir.blks[EXT_BLK_MSG0], 0, 2u * sizeof(ext_blk_t));
                lk.ext_dir.msg_count = (uint8_t)(n + 1);
                if (l >= 2 && p[1] > 0u && p[1] < lk.ext_dir.msg_count) {
                    lk.ext_dir.msg_count = p[1];
                }
                lk.msg_list_empty = 0;
                memset(lk.win_row_id, 0xFF, sizeof lk.win_row_id);
                ui_app_set_message_count(lk.ext_dir.msg_count);
                ext_dir_save();
                if (settings_get()->key_beep) {
                    hal_beep(HAL_VIB_SMS, 70u);
                }
            } else if (p[0] == 2u && l >= 3) {
                uint16_t off = (uint16_t)p[1] * LINK_BMP_CHUNK;
                if (p[1] == 0u && !lk.rx_row) {
                    lk.rx_row = (uint8_t *)ui_mem_alloc(LINK_ROW_BYTES, 4);
                }
                if (lk.rx_row && off < LINK_ROW_BYTES) {
                    uint16_t cp = (uint16_t)(l - 2);
                    if (off + cp > LINK_ROW_BYTES) cp = (uint16_t)(LINK_ROW_BYTES - off);
                    memcpy(lk.rx_row + off, p + 2, cp);
                    if (off + cp >= LINK_ROW_BYTES) {
                        int list_open = (ui_app_message_active() == UI_MENU_LIST);
                        lk.msg_list_empty = 0;
                        if (list_open) {
                            (void)ensure_dyn_buf(DYN_MSG_LIST, LINK_LIST_BYTES);
                        }
                        rx_row_finish(0u);
                        if (list_open && lk.dyn_kind == DYN_MSG_LIST && lk.dyn_buf) {
                            lk.msg_list_ready = 1;
                            lk.msg_list_loading = 0;
                        }
                        ui_mem_free(lk.rx_row);
                        lk.rx_row = 0;
                        ext_dir_save();
                        ui_app_set_message_count(lk.ext_dir.msg_count);
                        ui_draw_invalidate();
                    }
                }
            }
        }
        break;
    case LINK_EXT_ASSET:
        ext_asset_chunk(p, l);
        break;
    case LINK_MSG_CLEAR:
        if (l == 0) {
            link_clear_ext_messages();
            send_ext_pool_state();
        }
        break;
    case LINK_MSG_COUNT:
        if (l == 1) {
            int count = p[0];
            if (count > LINK_EXT_MSG_MAX) count = LINK_EXT_MSG_MAX;
            lk.list_target_n = (uint8_t)count;
            if (count == 0) {
                link_clear_ext_messages();
            } else {
                if (lk.ext_dir.msg_count > (uint8_t)count) {
                    lk.ext_dir.msg_count = (uint8_t)count;
                    ext_dir_save();
                }
                ui_app_set_message_count(count);
                lk.msg_list_loading = 1;
                lk.msg_list_empty = 0;
            }
        }
        break;
    case LINK_LYRIC:
        copy_text(lyric, LINK_LYRIC_LEN, p, l);
        lk.media_mask_ready = 0;
        lk.ext_dir.blks[EXT_BLK_MEDIA].off = 0u;
        lk.ext_dir.blks[EXT_BLK_MEDIA].len = 0u;
        if (lk.dyn_kind == DYN_MEDIA) {
            free_dyn_buf();
        }
        break;
    case LINK_STATE:
        if (l == 1) {
            lk.playing = p[0] ? 1 : 0;
        }
        break;
    case LINK_BRIGHT:
        if (l == 1 && p[0] <= 2) {
            lk.bright = p[0];
            ui_settings_apply(3, p[0]);
            (void)settings_save_ext(UI_CFG_PRIO_APP);
        }
        break;
    case LINK_SETTING:
        if (l == 2) {
            ui_settings_apply(p[0], p[1]);
            (void)settings_save_ext(UI_CFG_PRIO_APP);
            send_settings_state();
            send_cfg_state();
        }
        break;
    case LINK_GET_SETTINGS:
        if (l == 0) {
            send_settings_state();
        }
        break;
    case LINK_SET_CFG:
        if (l == 20) {
            settings_t *s = settings_get();
            settings_unpack_wire(p);
            ui_settings_set_debug(s->debug);
            ui_settings_apply(3, s->brightness);
            ui_settings_apply(5, s->led_mode);
            ui_settings_apply(6, s->screen_off);
            if (s->msg_auto_clear && lk.ext_dir.msg_count > 0u) {
                link_clear_ext_messages();
            }
            if (p[0] & 0x01u) {
                (void)settings_save_ext(s->cfg_source);
            }
            ui_draw_invalidate();
            send_settings_state();
            send_cfg_state();
            send_ext_pool_state();
        }
        break;
    case LINK_GET_CFG:
        if (l == 0) {
            send_cfg_state();
        }
        break;
    case LINK_SPORT:
        if (l == 0) {
            send_sport_state();
        }
        break;
    case LINK_SNAP:
        if (l == 0) {
            lk.snap_arm = 1;
        }
        break;
    case LINK_KEY:
        if (l == 1) {
            uint8_t next = (uint8_t)(p[0] & 0x07u);
            lk.link_down = (uint8_t)(lk.link_down | (next & (uint8_t)~lk.link_keys));
            lk.link_up = (uint8_t)(lk.link_up | (lk.link_keys & (uint8_t)~next));
            lk.link_keys = next;
        }
        break;
    case LINK_PERF:
        if (l == 0) {
            lk.perf_requested = 1;
        }
        break;
    case LINK_SENSOR:
        if (l == 0) {
            char buf[160];
            int n = hal_debug_snapshot(buf, (int)sizeof buf);
            if (n > 0) {
                hal_link_write((const uint8_t *)"DBG ", 4);
                hal_link_write((const uint8_t *)buf, n);
                hal_link_write((const uint8_t *)"\r\n", 2);
                lk.no_ack = 1;
            }
        }
        break;
    case LINK_DEBUG:
        if (l == 1) {
            ui_settings_set_debug(p[0] ? 1 : 0);
        }
        break;
    case LINK_HOME:
        if (l == 0) {
            ui_app_go_home();
        }
        break;
    default:
        break;
    }
}

void link_snap_request(void) {
    lk.snap_arm = 1;
}

static void snap_raw(uint8_t b) {
    lk.snap_xor ^= b;
    hal_link_write(&b, 1);
}

static void snap_payload(uint8_t b) {
    if (lk.snap_left == 0) {
        return;
    }
    snap_raw(b);
    lk.snap_left--;
}

void link_snap_begin(int w, int h) {
    uint32_t total;

    if (!lk.snap_arm || w <= 0 || h <= 0) {
        return;
    }
    lk.snap_arm = 0;
    lk.snap_on = 1;
    total = 4u + (uint32_t)w * (uint32_t)h * 2u;
    lk.snap_left = total;
    lk.snap_xor = 0;
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

    if (!lk.snap_on || !px || n <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        snap_payload((uint8_t)(px[i] >> 8));
        snap_payload((uint8_t)px[i]);
    }
}

int link_snap_active(void) {
    return lk.snap_on;
}

void link_snap_end(void) {
    uint8_t x;

    if (!lk.snap_on) {
        return;
    }
    x = lk.snap_xor;
    hal_link_write(&x, 1);
    lk.snap_on = 0;
    lk.snap_left = 0;
}

static void feed(uint8_t b) {
    if (lk.st == ST_MARK) {
        if (b == LINK_MARK) {
            lk.sum = b;
            lk.st = ST_CMD;
        }
        return;
    }
    if (lk.st == ST_CMD) {
        lk.cmd = b;
        lk.sum ^= b;
        lk.st = ST_LEN;
        return;
    }
    if (lk.st == ST_LEN) {
        lk.len = b;
        lk.sum ^= b;
        lk.got = 0;
        if (lk.len > LINK_PAY) {
            lk.st = ST_MARK;
            lk.ack_bad++;
            return;
        }
        lk.st = lk.len == 0 ? ST_XOR : ST_PAY;
        return;
    }
    if (lk.st == ST_PAY) {
        lk.pay[lk.got++] = b;
        lk.sum ^= b;
        if (lk.got >= lk.len) {
            lk.st = ST_XOR;
        }
        return;
    }
    if (b == lk.sum) {
        lk.no_ack = 0;
        lk.last_rx_ms = hal_tick_ms();
        apply_frame();
        if (!lk.no_ack) {
            lk.ack_ok++;
        }
    } else {
        lk.ack_bad++;
    }
    lk.st = ST_MARK;
}

void link_init(void) {
#if !defined(AT32_SLIM_ROM)
    copy_cstr(msgs[0], LINK_MSG_LEN, "消息1");
    copy_cstr(msgs[1], LINK_MSG_LEN, "消息2");
    copy_cstr(msgs[2], LINK_MSG_LEN, "消息3");
#endif
    lyric[0] = 0;
    memset(&lk, 0, sizeof lk);
    memset(lk.win_row_id, 0xFF, sizeof lk.win_row_id);
    lk.bright = 1;
}

void link_poll(void) {
    uint8_t b;
    int guard = 0;

    while (guard < 256 && hal_link_read(&b)) {
        feed(b);
        guard++;
    }
}

const char *link_message(int index) {
#if !defined(AT32_SLIM_ROM)
    if (index < 0 || index >= LINK_MSG_N) {
        return "";
    }
    return msgs[index];
#else
    (void)index;
    return "";
#endif
}

int link_brightness(void) {
    return lk.bright;
}

const char *link_media_lyric(void) {
    return lyric;
}

int link_media_mask_ready(void) {
    return lk.media_mask_ready && lk.dyn_buf && lk.dyn_kind == DYN_MEDIA;
}

const uint8_t *link_media_mask(void) {
    return (lk.dyn_buf && lk.dyn_kind == DYN_MEDIA) ? lk.dyn_buf : 0;
}

static const uint8_t *asset_mask_get(uint8_t kind, int16_t *w, int16_t *h) {
    if (!lk.dyn_buf || lk.dyn_kind != kind || lk.dyn_cap < 4u) {
        return 0;
    }
    if (w) *w = (int16_t)u16_get(lk.dyn_buf + 0);
    if (h) *h = (int16_t)u16_get(lk.dyn_buf + 2);
    return lk.dyn_buf + 4;
}

int link_compass_asset_ready(void) {
    return lk.dyn_kind == DYN_COMPASS && lk.dyn_buf != 0 && lk.dyn_cap >= 4u;
}

const uint8_t *link_compass_asset_mask(int16_t *w, int16_t *h) {
    return asset_mask_get(DYN_COMPASS, w, h);
}

int link_compass_loading(void) {
    return lk.load_kind == DYN_COMPASS3D && lk.load_need > 0u;
}

int link_compass3d_ready(void) {
    return lk.c3d_buf != 0 && lk.load_kind != DYN_COMPASS3D &&
           lk.c3d_total >= 16u && lk.c3d_entry >= 12u;
}

const uint8_t *link_compass3d_blob(uint16_t *entry_off, uint16_t *total_size) {
    if (!link_compass3d_ready()) {
        return 0;
    }
    if (entry_off) *entry_off = lk.c3d_entry;
    if (total_size) *total_size = lk.c3d_total;
    return lk.c3d_buf;
}

int link_about_asset_ready(void) {
    return lk.about_ready != 0u;
}

int link_about_loading(void) {
    return lk.dyn_kind == DYN_ABOUT && lk.load_need > 0u;
}

void link_begin_about(void) {
    lk.about_want = 1u;
    if (!lk.about_ready) {
        return;
    }
    if (lk.dyn_kind == DYN_ABOUT && lk.dyn_buf &&
        (lk.load_need > 0u || lk.dyn_cap >= lk.about_total)) {
        return;
    }
    (void)begin_about_load();
}

void link_enter_about(void) {
    link_begin_about();
    while (lk.load_need > 0u) {
        link_asset_pump(lk.load_need);
    }
}

void link_leave_about(void) {
    lk.about_want = 0u;
    if (lk.dyn_kind == DYN_ABOUT) {
        free_dyn_buf();
    }
}

const uint8_t *link_about_blob(uint16_t *total_size) {
    if (!lk.about_ready || lk.dyn_kind != DYN_ABOUT || !lk.dyn_buf ||
        lk.load_need != 0u || lk.about_total < 40u) {
        return 0;
    }
    if (total_size) {
        *total_size = lk.about_total;
    }
    return lk.dyn_buf;
}

int link_msg_list_ready(void) {
    return lk.msg_list_ready && lk.dyn_buf && lk.dyn_kind == DYN_MSG_LIST;
}

int link_msg_list_empty(void) {
    return lk.msg_list_empty;
}

int link_msg_count(void) {
    return (int)lk.ext_dir.msg_count;
}

const uint8_t *link_msg_list_mask(void) {
    return (lk.dyn_buf && lk.dyn_kind == DYN_MSG_LIST) ? lk.dyn_buf : 0;
}

const uint8_t *link_msg_row_mask(int index) {
    uint8_t ws;

    if (lk.dyn_kind != DYN_MSG_LIST || !lk.dyn_buf ||
        index < 0 || index >= (int)lk.ext_dir.msg_count) {
        return 0;
    }
    ws = (uint8_t)(index % LINK_LIST_WIN_ROWS);
    if (lk.win_row_id[ws] == (uint8_t)index) {
        return lk.dyn_buf + ws * LINK_ROW_BYTES;
    }
    if (lk.ext_dir.blks[EXT_BLK_MSG0 + index * 2].len > 0u &&
        ext_load_blob(lk.ext_dir.blks[EXT_BLK_MSG0 + index * 2].off,
                      lk.ext_dir.blks[EXT_BLK_MSG0 + index * 2].len,
                      lk.dyn_buf + ws * LINK_ROW_BYTES, LINK_ROW_BYTES)) {
        lk.win_row_id[ws] = (uint8_t)index;
        return lk.dyn_buf + ws * LINK_ROW_BYTES;
    }
    return 0;
}

int link_msg_detail_ready(void) {
    return lk.msg_detail_ready && lk.dyn_buf && lk.dyn_kind == DYN_MSG_DETAIL;
}

const uint8_t *link_msg_detail_mask(void) {
    return (lk.dyn_buf && lk.dyn_kind == DYN_MSG_DETAIL) ? lk.dyn_buf : 0;
}

int link_img_preview_ready(void) {
    return lk.img_ready && lk.img_msg_preview && lk.img_w > 0 && lk.img_h > 0 && hal_ext_flash_present();
}

void link_img_preview_info(int16_t *w, int16_t *h, uint8_t *fmt, uint32_t *ext_offset) {
    if (w) *w = lk.img_w;
    if (h) *h = lk.img_h;
    if (fmt) *fmt = lk.img_fmt;
    if (ext_offset) *ext_offset = link_ext_img_offset();
}

void link_img_preview_clear(void) {
    lk.img_ready = 0;
    lk.img_msg_preview = 0;
    lk.img_total_bytes = 0;
}

int link_connected(void) {
    uint32_t now = hal_tick_ms();
    return lk.last_rx_ms != 0 && (uint32_t)(now - lk.last_rx_ms) < 4000u;
}

int link_media_playing(void) {
    return lk.playing;
}

uint8_t link_key_level(void) {
    return lk.link_keys;
}

void link_key_edges(uint8_t *down, uint8_t *up) {
    if (down) {
        *down = lk.link_down;
    }
    if (up) {
        *up = lk.link_up;
    }
    lk.link_down = 0;
    lk.link_up = 0;
}

static void send_perf(void) {
    ui_render_stats_t stats;
    uint32_t ram_used = 0, ram_total = 0;
    uint16_t cpu;
    uint8_t frame[36];

    ui_render_get_stats(&stats);
    hal_ram_get(&ram_used, &ram_total);
    cpu = hal_cpu_tenths();
    if (cpu > 1000u) cpu = 1000u;

    frame[1] = LINK_STATS;
    u32_put(frame + 3, stats.prepare_us);
    u32_put(frame + 7, stats.raster_us);
    u32_put(frame + 11, stats.wait_us);
    u32_put(frame + 15, stats.frame_us);
    u32_put(frame + 19, stats.pixels);
    u32_put(frame + 23, stats.bands | ((uint32_t)stats.dropped << 16));
    u32_put(frame + 27, (ram_used & 0xFFFFu) | ((uint32_t)cpu << 16));
    u32_put(frame + 31, (ram_total & 0xFFFFu) | ((uint32_t)ui_app_get_fps() << 16));
    send_frame_buf(frame, 32u);
}

void link_ack_flush(void) {
    if (lk.perf_requested) {
        lk.perf_requested = 0;
        send_perf();
    }
    while (lk.ack_ok > 0) {
        reply("ok\r\n");
        lk.ack_ok--;
    }
    while (lk.ack_bad > 0) {
        reply("bad\r\n");
        lk.ack_bad--;
    }
}

void link_media_play(void) {
    lk.playing = 1;
    send_ctrl(ACT_PLAY);
}

void link_media_pause(void) {
    lk.playing = 0;
    send_ctrl(ACT_PAUSE);
}

void link_media_toggle(void) {
    if (lk.playing) {
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

void link_hdr_active(int active) {
    if (!active) {
        lk.hdr_active = 0u;
        free_hdr_buf();
        return;
    }
    lk.hdr_active = 1u;
    if (!lk.hdr_ready || !hal_ext_flash_present()) {
        return;
    }
    if (!lk.hdr_buf) {
        lk.hdr_buf = (uint8_t *)ui_mem_alloc(HDR_BUF_BYTES, 4);
        if (!lk.hdr_buf) {
            return;
        }
    }
    if (!lk.hdr_loaded) {
        ext_blk_t *b = &lk.ext_dir.blks[EXT_BLK_ASSET_HEADERS];
        uint16_t idx_bytes = (uint16_t)lk.hdr_count * 10u;
        if (b->len >= 12u + idx_bytes &&
            hal_ext_flash_read(LINK_EXT_OFF_POOL + (uint32_t)b->off + 12u,
                               lk.hdr_buf, idx_bytes)) {
            lk.hdr_loaded = 1u;
            lk.hdr_wr = 0u;
            lk.hdr_slot_next = 0u;
            memset(lk.hdr_slot_cp, 0, sizeof lk.hdr_slot_cp);
            memset(lk.hdr_slot_off, 0, sizeof lk.hdr_slot_off);
            memset(lk.hdr_slot_end, 0, sizeof lk.hdr_slot_end);
        } else {
            free_hdr_buf();
        }
    }
}

int link_hdr_lookup(uint32_t cp, int16_t *w, int16_t *h, int16_t *adv,
                    int8_t *ox, int8_t *oy, const uint8_t **rle_out) {
    ext_blk_t *b = &lk.ext_dir.blks[EXT_BLK_ASSET_HEADERS];
    uint8_t *ring;
    uint16_t idx_off = 0u, r_len = 0u;
    int i, found = -1, evicted = 0;

    if (!lk.hdr_ready || !lk.hdr_active || !hal_ext_flash_present()) {
        return 0;
    }
    if (!lk.hdr_buf || !lk.hdr_loaded) {
        link_hdr_active(1);
    }
    if (!lk.hdr_buf || !lk.hdr_loaded) {
        return 0;
    }
    for (i = 0; i < (int)lk.hdr_count; i++) {
        if (u16_get(lk.hdr_buf + i * 10) == (uint16_t)cp) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return 0;
    }
    {
        const uint8_t *ent = lk.hdr_buf + found * 10;
        uint16_t cov_off = u16_get(ent + 2);
        uint16_t nxt_off;
        if (found + 1 < (int)lk.hdr_count) {
            nxt_off = u16_get(lk.hdr_buf + (found + 1) * 10 + 2);
        } else {
            nxt_off = (uint16_t)(lk.hdr_total - lk.hdr_cover_off);
        }
        if (nxt_off <= cov_off) {
            return 0;
        }
        idx_off = cov_off;
        r_len = (uint16_t)(nxt_off - cov_off);
        if (w) *w = ent[4];
        if (h) *h = ent[5];
        if (adv) *adv = ent[6];
        if (ox) *ox = (int8_t)ent[7];
        if (oy) *oy = (int8_t)ent[8];
    }
    if (!rle_out) {
        return 1;
    }
    ring = lk.hdr_buf + HDR_IDX_BYTES;
    for (i = 0; i < HDR_SLOTS; i++) {
        if (lk.hdr_slot_cp[i] == (uint16_t)cp) {
            *rle_out = ring + lk.hdr_slot_off[i];
            return 1;
        }
    }
    if (r_len > HDR_RING_BYTES) {
        return 0;
    }
    if ((uint32_t)lk.hdr_wr + r_len > HDR_RING_BYTES) {
        lk.hdr_wr = 0u;
    }
    if (!hal_ext_flash_read(LINK_EXT_OFF_POOL + (uint32_t)b->off +
                            (uint32_t)lk.hdr_cover_off + (uint32_t)idx_off,
                            ring + lk.hdr_wr, r_len)) {
        return 0;
    }
    /* Invalidate any previous slot whose range overlaps [lk.hdr_wr, lk.hdr_wr + r_len) */
    for (i = 0; i < HDR_SLOTS; i++) {
        if (lk.hdr_slot_cp[i] != 0u &&
            lk.hdr_slot_off[i] < (uint16_t)(lk.hdr_wr + r_len) &&
            lk.hdr_slot_end[i] > lk.hdr_wr) {
            lk.hdr_slot_cp[i] = 0u;
            evicted = 1;
        }
    }
    if (lk.hdr_slot_cp[lk.hdr_slot_next] != 0u) {
        evicted = 1;
    }
    lk.hdr_slot_cp[lk.hdr_slot_next] = (uint16_t)cp;
    lk.hdr_slot_off[lk.hdr_slot_next] = lk.hdr_wr;
    lk.hdr_slot_end[lk.hdr_slot_next] = (uint16_t)(lk.hdr_wr + r_len);
    lk.hdr_slot_next = (uint8_t)((lk.hdr_slot_next + 1u) % HDR_SLOTS);
    *rle_out = ring + lk.hdr_wr;
    lk.hdr_wr = (uint16_t)(lk.hdr_wr + r_len);
    if (evicted) {
        ui_draw_invalidate();
    }
    return 1;
}

void link_begin_pic_sport(void) {
    if (lk.dyn_kind == DYN_SPORT && lk.dyn_buf &&
        (lk.load_need > 0u || lk.pic_ready_kind == DYN_SPORT)) {
        return;
    }
    (void)begin_pic_load(EXT_BLK_ASSET_SPORT, PIC_MAGIC_SPORT, DYN_SPORT, 4608u);
}

void link_leave_pic_sport(void) {
    if (lk.dyn_kind == DYN_SPORT) {
        free_dyn_buf();
    }
}

int link_pic_sport_ready(void) {
    return lk.dyn_kind == DYN_SPORT && lk.pic_ready_kind == DYN_SPORT &&
           lk.dyn_buf != 0 && lk.load_need == 0u && lk.pic_entry >= 12u;
}

const uint8_t *link_pic_sport_blob(uint16_t *entry_off) {
    if (!link_pic_sport_ready()) return 0;
    if (entry_off) *entry_off = lk.pic_entry;
    return lk.dyn_buf;
}

void link_begin_pic_analog(void) {
    if (lk.dyn_kind == DYN_ANALOG && lk.dyn_buf &&
        (lk.load_need > 0u || lk.pic_ready_kind == DYN_ANALOG)) {
        return;
    }
    (void)begin_pic_load(EXT_BLK_ASSET_ANALOG, PIC_MAGIC_ANALOG, DYN_ANALOG, 3072u);
}

void link_leave_pic_analog(void) {
    if (lk.dyn_kind == DYN_ANALOG) {
        free_dyn_buf();
    }
}

int link_pic_analog_ready(void) {
    return lk.dyn_kind == DYN_ANALOG && lk.pic_ready_kind == DYN_ANALOG &&
           lk.dyn_buf != 0 && lk.load_need == 0u && lk.pic_entry >= 12u;
}

const uint8_t *link_pic_analog_blob(uint16_t *entry_off) {
    if (!link_pic_analog_ready()) return 0;
    if (entry_off) *entry_off = lk.pic_entry;
    return lk.dyn_buf;
}

void link_begin_pic_sys(void) {
    if (lk.load_kind == DYN_SYS && lk.load_need > 0u) {
        return;
    }
    if (lk.dyn_kind == DYN_SYS && lk.dyn_buf && lk.pic_ready_kind == DYN_SYS &&
        lk.pic_entry >= 12u) {
        return;
    }
    (void)begin_pic_load(EXT_BLK_ASSET_SYS, PIC_MAGIC_SYS, DYN_SYS, 4096u);
}

int link_pic_sys_ready(void) {
    return lk.dyn_kind == DYN_SYS && lk.pic_ready_kind == DYN_SYS &&
           lk.dyn_buf != 0 && lk.load_need == 0u && lk.pic_entry >= 12u;
}

const uint8_t *link_pic_sys_blob(uint16_t *entry_off) {
    if (!link_pic_sys_ready()) return 0;
    if (entry_off) *entry_off = lk.pic_entry;
    return lk.dyn_buf;
}

void link_leave_pic_sys(void) {
    if (lk.dyn_kind == DYN_SYS) {
        free_dyn_buf();
    }
}

uint16_t link_img_gen(void) {
    return lk.img_gen;
}
