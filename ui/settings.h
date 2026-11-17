#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

enum {
    UI_DIAL_AMBER  = 0, /* 琥珀双色数字 */
    UI_DIAL_ANALOG = 1, /* 墨竹极简指针 */
    UI_DIAL_IMU    = 2, /* 九轴运动仪表 */
    UI_DIAL_PHOTO  = 3, /* 自定义压缩相册表盘（读外置 AT24C256） */
    UI_DIAL_COUNT  = 4
};

enum {
    UI_DIAL_COMP_DATE = 0, /* 日期与星期 / 媒体 */
    UI_DIAL_COMP_STEP = 1, /* 今日步数进度 */
    UI_DIAL_COMP_HDG  = 2, /* 九轴航向与姿态 */
    UI_DIAL_COMP_BAT  = 3, /* 电池电压与电量 */
    UI_DIAL_COMP_NONE = 4  /* 极简留白 */
};

enum {
    UI_DIAL_FLAG_SECONDS   = 1u << 0, /* 冒号脉冲 / 秒针 */
    UI_DIAL_FLAG_POS_MASK  = 3u << 1, /* 相册表盘时间位置: 0=居中, 1=居顶, 2=居底 */
    UI_DIAL_FLAG_SCRIM     = 1u << 3  /* 相册表盘半透明暗角时间胶囊 */
};

enum {
    UI_CFG_PRIO_DEFAULT = 0, /* 固件默认 */
    UI_CFG_PRIO_I2C     = 1, /* 未连手机：I2C EEPROM / 本地生效 */
    UI_CFG_PRIO_APP     = 2  /* App 最高优先级覆盖并写入 I2C */
};

typedef struct settings {
    uint8_t debug;        /* 调试角标 0/1 */
    uint8_t brightness;   /* 0低 1中 2高 */
    uint8_t raise_wrist;  /* 抬腕亮屏 0/1 */
    uint8_t key_beep;     /* 按键音 0/1 */
    uint8_t bluetooth;    /* 蓝牙开关 0/1 */
    uint8_t led_mode;     /* 状态灯 0=关闭 1=常亮 2=闪烁 */
    uint8_t screen_off;   /* 熄屏 0=关 1=30秒 2=1分钟 3=2分钟 4=5分钟 */
    uint8_t use_24h;      /* 24小时制 1 / 12小时制 0 */
    uint8_t msg_auto_clear;/* 通知清理开关: 0=重启保留消息, 1=自动清理 */
    uint8_t dial_id;      /* 0..3 当前表盘 */
    uint8_t dial_top;     /* 顶部信息组件 0..4 */
    uint8_t dial_bot;     /* 底部信息组件 0..4 */
    uint8_t dial_flags;   /* UI_DIAL_FLAG_* */
    uint8_t cfg_source;   /* UI_CFG_PRIO_* */
    uint8_t cfg_loaded;   /* 上电是否成功从 AT24C256 恢复 */
    uint16_t dial_text;   /* 主时间色 RGB565 */
    uint16_t dial_accent; /* 表盘强调色 RGB565 */
    uint16_t dial_bg;     /* 表盘背景色 RGB565 */
    int16_t img_w;        /* 自定义表盘底图宽 */
    int16_t img_h;        /* 自定义表盘底图高 */
    uint8_t img_fmt;      /* UI_EXT_FMT_* */
    uint8_t img_ready;    /* 自定义表盘图就绪 (0=无, 1=就绪) */
    uint8_t img_on_chip;  /* 1=已交换进片内 Flash 热槽(0 I2C 读), 0=片外 AT24C256 */
    uint16_t img_bytes;   /* 压缩或原始字节长度 */
    uint8_t mag_cal_saved;/* 是否曾写入磁强计偏置 */
} settings_t;

void settings_init(void);
settings_t *settings_get(void);
void settings_pack_wire(uint8_t *dst20);
void settings_unpack_wire(const uint8_t *src20);
int settings_load_ext(void);
int settings_save_ext(uint8_t source_prio);
void settings_record_clock(const void *hal_clock_ptr);
uint16_t settings_crc16_step(uint16_t crc, const uint8_t *buf, uint32_t len);
uint16_t settings_crc16(const uint8_t *buf, uint32_t len);

#endif
