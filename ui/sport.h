#ifndef SPORT_H
#define SPORT_H

#include "hal.h"

/* 可移植计步与运动统计。纯整数、无动态内存，50-100Hz 喂加速度采样。 */

typedef struct sport_stats {
    uint32_t steps;        /* 今日步数 */
    uint32_t distance_m;   /* 距离，米 */
    uint32_t calories;     /* 卡路里，kcal */
    uint32_t goal;         /* 步数目标 */
    uint32_t active_min;   /* 活动分钟 */
} sport_stats_t;

void sport_init(void);
void sport_feed(const hal_imu_t *s, uint32_t now_ms);  /* 每个加速度采样调用，约 50-100Hz */
void sport_get(sport_stats_t *out);
void sport_reset_today(void);
void sport_set_goal(uint32_t goal);
int sport_raise_wrist(void);   /* 最近检测到抬腕手势返回 1，否则 0 */

/* 柱状图：12 个 2 小时桶（0..23 点）。out 至少 n 个，n>12 截断。 */
#define SPORT_BARS 12
void sport_get_bars(uint32_t *out, int n);
/* 调试：注入步数（累加到当前时段桶与总数），用于查看柱状图效果。 */
void sport_debug_add_steps(uint32_t n);

#endif