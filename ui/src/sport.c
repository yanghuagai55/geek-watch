/* 计步/运动统计/抬腕。纯整数 C99，无 malloc，不碰芯片头文件。
   计步：幅值近似 -> 快低通去噪 -> 慢低通当基线 -> 交流分量过动态阈值计一步，
   滞回+最小间隔防抖。抬腕：陀螺爆发 + 重力方向明显改变，双判据防误报。 */
#include "sport.h"
#include <string.h>

/* ---- 可调参数 ---- */
#define LP_SHIFT         2     /* 快低通 ~40-80ms：滤采样噪声但保留 2Hz 步行峰 */
#define BASE_SHIFT       6     /* 慢低通 ~1s：跟踪重力基线，走路波动不被吃掉 */
#define ENV_SHIFT        7     /* 峰值包络衰减 ~2s：动态阈值随运动强度自适应 */
#define AC_MIN_MG        60    /* 静止门槛：波动低于 60mg 一律不计步 */
#define AC_MAX_MG        800   /* 阈值上限：剧烈运动也不漏步 */
#define STEP_GAP_MS      250u  /* 相邻步最小间隔，限 ~240 步/分 */
#define STRIDE_MM        600u  /* 默认步幅 0.6m，毫米累加避免精度丢失 */
#define WEIGHT_KG        70u   /* 默认体重，卡路里粗估用 */
#define MKCAL_PER_STEP   (WEIGHT_KG * 1000u / 2000u) /* ≈0.035kcal/步 */
#define ACTIVE_GAP_MS    3000u /* 步间隔小于此视为连续步行，累计活动时长 */
#define DEFAULT_GOAL     8000u
#define GRAV_SHIFT       4     /* 重力方向低通 ~0.3s */
#define GYRO_THR         1500  /* 三轴陀螺绝对值和，0.1dps 即 150dps */
#define GYRO_MIN_N       3     /* 爆发至少持续 3 个采样，拒绝单点毛刺 */
#define WRIST_DELTA_MG   500   /* 爆发前后重力投影变化量，确认真翻了手腕 */
#define WRIST_SETTLE_MS  600u  /* 爆发结束后留给姿态变化的观察窗 */
#define RAISE_WINDOW_MS  1500u /* 抬腕结果保持窗口 */

static struct {
    uint32_t steps, dist_mm, cal_milli, active_ms, goal;
    uint32_t buckets[SPORT_BARS];
    uint32_t last_step_ms;
    uint8_t have_step;
    /* 计步滤波 */
    int32_t lp, base, env;
    uint8_t filt_init, above;
    /* 抬腕 */
    int32_t grav[3], grav0[3];
    uint8_t grav_init, burst_ok;
    uint16_t burst_n;
    uint32_t burst_end_ms;
    uint8_t raise_valid;
    uint32_t raise_ms;
    /* 最近一次喂样时间，供无参的 sport_raise_wrist 判断窗口 */
    uint32_t last_now_ms;
    uint8_t fed;
} sp;

static int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

/* 幅值整数近似 max+mid/2+min/4：免开方，误差 <6%，对腕上任意朝向都成立 */
static int32_t mag_mg(const hal_imu_t *s)
{
    int32_t a = abs32(s->ax), b = abs32(s->ay), c = abs32(s->az), t;
    if (a < b) { t = a; a = b; b = t; }
    if (b < c) { t = b; b = c; c = t; }
    if (a < b) { t = a; a = b; b = t; }
    return a + b / 2 + c / 4;
}

static int cur_bucket(void)
{
    hal_clock_t c;
    hal_clock_get(&c);
    return (c.hour / 2) % SPORT_BARS;
}

static void count_step(uint32_t now)
{
    /* 只累计连续步行的间隔，久停后的第一步不计入活动时长 */
    if (sp.have_step && (now - sp.last_step_ms) <= ACTIVE_GAP_MS)
        sp.active_ms += now - sp.last_step_ms;
    sp.have_step = 1;
    sp.last_step_ms = now;
    sp.steps++;
    sp.buckets[cur_bucket()]++;
    sp.dist_mm += STRIDE_MM;
    sp.cal_milli += MKCAL_PER_STEP;
}

static void wrist_feed(const hal_imu_t *s, uint32_t now)
{
    int32_t a[3], gsum, delta;
    int i;
    a[0] = s->ax; a[1] = s->ay; a[2] = s->az;
    if (!sp.grav_init) {
        sp.grav_init = 1;
        for (i = 0; i < 3; i++) sp.grav[i] = a[i];
    }
    for (i = 0; i < 3; i++)
        sp.grav[i] += (a[i] - sp.grav[i]) / (1 << GRAV_SHIFT);
    gsum = abs32(s->gx) + abs32(s->gy) + abs32(s->gz);
    if (gsum > GYRO_THR) {
        if (sp.burst_n == 0)
            for (i = 0; i < 3; i++) sp.grav0[i] = sp.grav[i]; /* 爆发前姿态快照 */
        if (sp.burst_n < 100) sp.burst_n++;
        sp.burst_end_ms = now;
    } else {
        sp.burst_n = 0;
    }
    if (sp.burst_n >= GYRO_MIN_N) sp.burst_ok = 1;
    if ((now - sp.burst_end_ms) > WRIST_SETTLE_MS) sp.burst_ok = 0;
    if (sp.burst_ok &&
        (!sp.raise_valid || (now - sp.raise_ms) >= RAISE_WINDOW_MS)) {
        delta = 0;
        for (i = 0; i < 3; i++) delta += abs32(sp.grav[i] - sp.grav0[i]);
        if (delta > WRIST_DELTA_MG) {
            sp.raise_valid = 1;
            sp.raise_ms = now;
            sp.burst_ok = 0; /* 一次手势只报一次，等新爆发 */
        }
    }
}

void sport_init(void)
{
    memset(&sp, 0, sizeof sp);
    sp.goal = DEFAULT_GOAL;
}

void sport_feed(const hal_imu_t *s, uint32_t now_ms)
{
    int32_t m, ac, thr;
    if (!s) return;
    sp.last_now_ms = now_ms;
    sp.fed = 1;
    m = mag_mg(s);
    if (!sp.filt_init) {
        sp.filt_init = 1; /* 首帧直接装填，避免上电瞬态被当成一步 */
        sp.lp = sp.base = m;
    }
    /* 用除法而非移位：负数右移是实现定义行为，除法保证可移植 */
    sp.lp += (m - sp.lp) / (1 << LP_SHIFT);
    sp.base += (m - sp.base) / (1 << BASE_SHIFT);
    ac = sp.lp - sp.base;
    if (ac > sp.env) sp.env = ac;
    else sp.env -= sp.env / (1 << ENV_SHIFT);
    thr = sp.env / 2;
    if (thr < AC_MIN_MG) thr = AC_MIN_MG;
    if (thr > AC_MAX_MG) thr = AC_MAX_MG;
    if (!sp.above) {
        if (ac > thr) {
            sp.above = 1;
            /* 回绕安全：uint32 差值比较 */
            if (!sp.have_step || (now_ms - sp.last_step_ms) >= STEP_GAP_MS)
                count_step(now_ms);
        }
    } else if (ac < thr / 2) {
        sp.above = 0; /* 滞回：跌回半阈值以下才允许计下一步 */
    }
    wrist_feed(s, now_ms);
}

void sport_get(sport_stats_t *out)
{
    if (!out) return;
    out->steps = sp.steps;
    out->distance_m = sp.dist_mm / 1000u;
    out->calories = sp.cal_milli / 1000u;
    out->goal = sp.goal;
    out->active_min = sp.active_ms / 60000u;
}

void sport_reset_today(void)
{
    int i;
    sp.steps = sp.dist_mm = sp.cal_milli = sp.active_ms = 0;
    for (i = 0; i < SPORT_BARS; i++) sp.buckets[i] = 0;
    sp.have_step = 0;
    sp.above = 0;
    /* 滤波与手势状态保留：它们是物理状态，不属于“今日” */
}

void sport_get_bars(uint32_t *out, int n)
{
    int i;
    if (!out) return;
    if (n > SPORT_BARS) n = SPORT_BARS;
    for (i = 0; i < n; i++) out[i] = sp.buckets[i];
}

void sport_debug_add_steps(uint32_t n)
{
    sp.steps += n;
    sp.buckets[cur_bucket()] += n;
    sp.dist_mm += n * STRIDE_MM;
    sp.cal_milli += n * MKCAL_PER_STEP;
}

void sport_set_goal(uint32_t goal)
{
    sp.goal = goal;
}

int sport_raise_wrist(void)
{
    return (sp.raise_valid && sp.fed &&
            (sp.last_now_ms - sp.raise_ms) <= RAISE_WINDOW_MS) ? 1 : 0;
}