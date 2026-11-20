#include "imu9.h"

#include <string.h>

enum {
    Q14_ONE = 16384,
    Q24_ONE = 16777216,
    STALE_MS = 1000u,
    GYRO_INIT_N = 30
};

/* 0..1 区间 16 段 atan(i/16) * 18000 / pi，单位 0.01 deg */
static const int16_t atan_lut[17] = {
    0, 358, 713, 1062, 1404, 1735, 2056, 2363,
    2657, 2936, 3201, 3451, 3687, 3909, 4119, 4315, 4500
};

typedef struct {
    imu9_state_t st;
    int32_t q0, q1, q2, q3;         /* Q24 单位四元数 (Body -> ENU) */
    int32_t gbx_f, gby_f, gbz_f;    /* Q8 (1/256) * 0.1dps 零偏 */
    int32_t err_ix, err_iy, err_iz; /* 积分误差 */
    int32_t g_sum[3];
    int16_t prev_g[3];
    int16_t m_min[3], m_max[3];
    uint8_t g_cnt;
    uint8_t pose_valid;
    uint8_t mag_pose_snapped;
    uint8_t imu_seen, mag_seen;
    uint32_t imu_ms, mag_ms, last_feed_ms;
} imu9_ctx_t;

static imu9_ctx_t g_ctx;

static int32_t abs32(int32_t v) {
    return v < 0 ? -v : v;
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#if defined(__GNUC__)
#define IMU_NOINLINE __attribute__((noinline))
#else
#define IMU_NOINLINE
#endif

IMU_NOINLINE static uint32_t isqrt32(uint32_t op) {
    uint32_t res = 0u;
    uint32_t one;

    if (op == 0u) {
        return 0u;
    }
#if defined(__GNUC__)
    one = 1u << ((31u - (unsigned)__builtin_clz(op)) & ~1u);
#else
    one = 1u << 30;
    while (one > op) {
        one >>= 2;
    }
#endif
    while (one != 0u) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return res;
}

IMU_NOINLINE static int16_t atan2_d10(int32_t y, int32_t x) {
    int32_t ax, ay, num, den, t, idx, frac, ang, d10, res;
    int swap;

    if (x == 0 && y == 0) {
        return 0;
    }
    ax = abs32(x);
    ay = abs32(y);
    swap = ay > ax;
    num = swap ? ax : ay;
    den = swap ? ay : ax;
    t = ((int32_t)num << 14) / den;
    if (t >= 16384) {
        ang = 4500;
    } else {
        idx = t >> 10;
        frac = t & 1023;
        ang = atan_lut[idx] + (((atan_lut[idx + 1] - atan_lut[idx]) * frac) >> 10);
    }
    if (swap) {
        ang = 9000 - ang;
    }
    d10 = (ang + 5) / 10;
    if (x >= 0 && y >= 0) {
        res = d10;
    } else if (x < 0 && y >= 0) {
        res = 1800 - d10;
    } else if (x < 0 && y < 0) {
        res = -(1800 - d10);
    } else {
        res = -d10;
    }
    return (int16_t)res;
}

/* 把 3 维向量归一化到 Q14 单位长度，范数为 0 返回 0 */
IMU_NOINLINE static int vec_norm_q14(int32_t x, int32_t y, int32_t z, int32_t out[3]) {
    uint32_t n2;
    int32_t n;

    while (abs32(x) >= 32768 || abs32(y) >= 32768 || abs32(z) >= 32768) {
        x >>= 1;
        y >>= 1;
        z >>= 1;
    }
    n2 = (uint32_t)(x * x + y * y + z * z);
    if (n2 == 0u) {
        return 0;
    }
    n = (int32_t)isqrt32(n2);
    if (n <= 0) {
        return 0;
    }
    out[0] = (x * Q14_ONE) / n;
    out[1] = (y * Q14_ONE) / n;
    out[2] = (z * Q14_ONE) / n;
    return 1;
}

static void cross_q14(const int32_t a[3], const int32_t b[3], int32_t out[3]) {
    out[0] = (a[1] * b[2] - a[2] * b[1]) >> 14;
    out[1] = (a[2] * b[0] - a[0] * b[2]) >> 14;
    out[2] = (a[0] * b[1] - a[1] * b[0]) >> 14;
}

IMU_NOINLINE static void quat_normalize_q24(void) {
    int32_t *q = &g_ctx.q0;
    int i, iter;

    for (iter = 0; iter < 16; iter++) {
        uint64_t n2 = (uint64_t)((int64_t)q[0] * q[0] + (int64_t)q[1] * q[1] +
                                 (int64_t)q[2] * q[2] + (int64_t)q[3] * q[3]);
        uint32_t hi = (uint32_t)(n2 >> 32);
        int32_t k, err;
        if (n2 == 0u) {
            q[0] = Q24_ONE;
            q[1] = q[2] = q[3] = 0;
            return;
        }
        if (hi >= 0x18000u) {
            k = Q24_ONE >> 1;
            err = Q24_ONE;
        } else if (hi < 0x8000u) {
            k = Q24_ONE << 1;
            err = Q24_ONE;
        } else {
            /* Q24 Newton-Raphson inverse sqrt: q *= (3 - ||q||^2) / 2 (zero division). */
            int32_t s2 = (int32_t)(uint32_t)(n2 >> 24);
            k = (3 * Q24_ONE - s2) >> 1;
            err = abs32(s2 - Q24_ONE);
        }
        for (i = 0; i < 4; i++) {
            q[i] = (int32_t)(((int64_t)q[i] * (int64_t)k) >> 24);
        }
        if (err < 4096) {
            return;
        }
    }
}

/* 由正交旋转阵 R (Q14, Body->ENU) 按 Shepperd 方法转换到 Q24 四元数 */
IMU_NOINLINE static void rot_to_quat(const int32_t r[3][3]) {
    int32_t tr = r[0][0] + r[1][1] + r[2][2];
    int32_t s;
    int32_t q[4];

    if (tr > 0) {
        s = (int32_t)isqrt32((uint32_t)Q14_ONE * (uint32_t)(Q14_ONE + tr));
        if (s == 0) s = 1;
        q[0] = (s << 9);
        q[1] = ((r[2][1] - r[1][2]) << 15) / s * 256;
        q[2] = ((r[0][2] - r[2][0]) << 15) / s * 256;
        q[3] = ((r[1][0] - r[0][1]) << 15) / s * 256;
    } else if (r[0][0] >= r[1][1] && r[0][0] >= r[2][2]) {
        s = (int32_t)isqrt32((uint32_t)Q14_ONE *
            (uint32_t)(Q14_ONE + r[0][0] - r[1][1] - r[2][2]));
        if (s == 0) s = 1;
        q[1] = (s << 9);
        q[0] = ((r[2][1] - r[1][2]) << 15) / s * 256;
        q[2] = ((r[0][1] + r[1][0]) << 15) / s * 256;
        q[3] = ((r[0][2] + r[2][0]) << 15) / s * 256;
    } else if (r[1][1] >= r[2][2]) {
        s = (int32_t)isqrt32((uint32_t)Q14_ONE *
            (uint32_t)(Q14_ONE + r[1][1] - r[0][0] - r[2][2]));
        if (s == 0) s = 1;
        q[2] = (s << 9);
        q[0] = ((r[0][2] - r[2][0]) << 15) / s * 256;
        q[1] = ((r[0][1] + r[1][0]) << 15) / s * 256;
        q[3] = ((r[1][2] + r[2][1]) << 15) / s * 256;
    } else {
        s = (int32_t)isqrt32((uint32_t)Q14_ONE *
            (uint32_t)(Q14_ONE + r[2][2] - r[0][0] - r[1][1]));
        if (s == 0) s = 1;
        q[3] = (s << 9);
        q[0] = ((r[1][0] - r[0][1]) << 15) / s * 256;
        q[1] = ((r[0][2] + r[2][0]) << 15) / s * 256;
        q[2] = ((r[1][2] + r[2][1]) << 15) / s * 256;
    }
    g_ctx.q0 = q[0];
    g_ctx.q1 = q[1];
    g_ctx.q2 = q[2];
    g_ctx.q3 = q[3];
    quat_normalize_q24();
}

IMU_NOINLINE static void quat_to_rot(int32_t r[3][3]) {
    int32_t w = g_ctx.q0 >> 10;
    int32_t x = g_ctx.q1 >> 10;
    int32_t y = g_ctx.q2 >> 10;
    int32_t z = g_ctx.q3 >> 10;

    r[0][0] = (w * w + x * x - y * y - z * z) >> 14;
    r[0][1] = (2 * (x * y - w * z)) >> 14;
    r[0][2] = (2 * (x * z + w * y)) >> 14;
    r[1][0] = (2 * (x * y + w * z)) >> 14;
    r[1][1] = (w * w - x * x + y * y - z * z) >> 14;
    r[1][2] = (2 * (y * z - w * x)) >> 14;
    r[2][0] = (2 * (x * z - w * y)) >> 14;
    r[2][1] = (2 * (y * z + w * x)) >> 14;
    r[2][2] = (w * w - x * x - y * y + z * z) >> 14;
}

/* TRIAD：由重力方向 Up 与磁场直接构建初始姿态，实现上电零滞后对齐 */
static int triad_snap(const int32_t u[3], const int32_t *m) {
    int32_t ub[3], eb[3], nb[3], mb[3], raw_e[3];
    int32_t r[3][3];
    int have_e = 0;
    int i;

    if (!vec_norm_q14(u[0], u[1], u[2], ub)) {
        return 0;
    }
    if (m && vec_norm_q14(m[0], m[1], m[2], mb)) {
        cross_q14(mb, ub, raw_e);
        if (abs32(raw_e[0]) + abs32(raw_e[1]) + abs32(raw_e[2]) > 800) {
            have_e = vec_norm_q14(raw_e[0], raw_e[1], raw_e[2], eb);
        }
    }
    if (!have_e) {
        int32_t ref[3];
        ref[0] = (abs32(ub[1]) < 14000) ? 0 : Q14_ONE;
        ref[1] = (abs32(ub[1]) < 14000) ? Q14_ONE : 0;
        ref[2] = 0;
        cross_q14(ref, ub, raw_e);
        if (!vec_norm_q14(raw_e[0], raw_e[1], raw_e[2], eb)) {
            return 0;
        }
    }
    cross_q14(ub, eb, raw_e);
    if (!vec_norm_q14(raw_e[0], raw_e[1], raw_e[2], nb)) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        r[0][i] = eb[i];
        r[1][i] = nb[i];
        r[2][i] = ub[i];
    }
    rot_to_quat(r);
    g_ctx.pose_valid = 1;
    if (m && have_e) {
        g_ctx.mag_pose_snapped = 1;
    }
    return 1;
}

static void extract_euler(void) {
    int32_t r[3][3];
    int32_t horiz, hd10, yaw_d10, hdeg;

    quat_to_rot(r);
    horiz = (int32_t)isqrt32((uint32_t)(r[2][0] * r[2][0] + r[2][2] * r[2][2]));
    g_ctx.st.pitch_d10 = atan2_d10(r[2][1], horiz);
    g_ctx.st.roll_d10 = atan2_d10(-r[2][0], r[2][2]);

    /* 常规握持/平放用 +X 体轴水平投影避免抬头 90° 死锁；大侧倾切回 +Y 体轴 */
    if (abs32(r[2][0]) < 14200) {
        yaw_d10 = atan2_d10(r[1][0], r[0][0]);
        hd10 = -yaw_d10;
    } else {
        hd10 = atan2_d10(r[0][1], r[1][1]);
        yaw_d10 = -hd10;
    }
    if (yaw_d10 > 1800) yaw_d10 -= 3600;
    if (yaw_d10 <= -1800) yaw_d10 += 3600;
    g_ctx.st.yaw_d10 = (int16_t)yaw_d10;

    hdeg = (hd10 >= 0) ? (hd10 + 5) / 10 : (hd10 - 5) / 10;
    hdeg = ((hdeg % 360) + 360) % 360;
    g_ctx.st.heading_deg = (uint16_t)hdeg;
}

static void learn_gyro_bias(const hal_imu_t *imu, uint32_t a_norm_mg) {
    int32_t dg;

    if (g_ctx.st.mag_cal == 1) {
        return;
    }
    if (a_norm_mg < 860u || a_norm_mg > 1140u) {
        return;
    }
    dg = abs32((int32_t)imu->gx - g_ctx.prev_g[0]) +
         abs32((int32_t)imu->gy - g_ctx.prev_g[1]) +
         abs32((int32_t)imu->gz - g_ctx.prev_g[2]);
    if (dg > 45) {
        return;
    }
    if (!g_ctx.st.gyro_cal) {
        if (abs32(imu->gx) > 150 || abs32(imu->gy) > 150 || abs32(imu->gz) > 150) {
            return;
        }
        g_ctx.g_sum[0] += imu->gx;
        g_ctx.g_sum[1] += imu->gy;
        g_ctx.g_sum[2] += imu->gz;
        g_ctx.g_cnt++;
        if (g_ctx.g_cnt >= GYRO_INIT_N) {
            g_ctx.gbx_f = (g_ctx.g_sum[0] << 8) / GYRO_INIT_N;
            g_ctx.gby_f = (g_ctx.g_sum[1] << 8) / GYRO_INIT_N;
            g_ctx.gbz_f = (g_ctx.g_sum[2] << 8) / GYRO_INIT_N;
            g_ctx.st.gbx = (int16_t)((g_ctx.gbx_f + 128) >> 8);
            g_ctx.st.gby = (int16_t)((g_ctx.gby_f + 128) >> 8);
            g_ctx.st.gbz = (int16_t)((g_ctx.gbz_f + 128) >> 8);
            g_ctx.st.gyro_cal = 1;
        }
    } else if (abs32((int32_t)imu->gx - g_ctx.st.gbx) < 40 &&
               abs32((int32_t)imu->gy - g_ctx.st.gby) < 40 &&
               abs32((int32_t)imu->gz - g_ctx.st.gbz) < 40) {
        /* 静止期慢速跟踪温度零漂 (alpha = 1/64) */
        g_ctx.gbx_f += (((int32_t)imu->gx << 8) - g_ctx.gbx_f) >> 6;
        g_ctx.gby_f += (((int32_t)imu->gy << 8) - g_ctx.gby_f) >> 6;
        g_ctx.gbz_f += (((int32_t)imu->gz << 8) - g_ctx.gbz_f) >> 6;
        g_ctx.st.gbx = (int16_t)(g_ctx.gbx_f >> 8);
        g_ctx.st.gby = (int16_t)(g_ctx.gby_f >> 8);
        g_ctx.st.gbz = (int16_t)(g_ctx.gbz_f >> 8);
    }
}

static void track_mag_cal(const hal_mag_t *mag) {
    if (g_ctx.st.mag_cal != 1 || !mag) {
        return;
    }
    if (mag->x < g_ctx.m_min[0]) g_ctx.m_min[0] = mag->x;
    if (mag->y < g_ctx.m_min[1]) g_ctx.m_min[1] = mag->y;
    if (mag->z < g_ctx.m_min[2]) g_ctx.m_min[2] = mag->z;
    if (mag->x > g_ctx.m_max[0]) g_ctx.m_max[0] = mag->x;
    if (mag->y > g_ctx.m_max[1]) g_ctx.m_max[1] = mag->y;
    if (mag->z > g_ctx.m_max[2]) g_ctx.m_max[2] = mag->z;
}

void imu9_init(void) {
    memset(&g_ctx, 0, sizeof g_ctx);
    g_ctx.q0 = Q24_ONE;
}

void imu9_reset(void) {
    g_ctx.q0 = Q24_ONE;
    g_ctx.q1 = g_ctx.q2 = g_ctx.q3 = 0;
    g_ctx.pose_valid = 0;
    g_ctx.mag_pose_snapped = 0;
    g_ctx.st.gyro_cal = 0;
    g_ctx.g_cnt = 0;
    g_ctx.g_sum[0] = g_ctx.g_sum[1] = g_ctx.g_sum[2] = 0;
    g_ctx.err_ix = g_ctx.err_iy = g_ctx.err_iz = 0;
}

void imu9_cal_start(void) {
    if (g_ctx.st.mag_cal == 1) {
        int32_t sx = (int32_t)g_ctx.m_max[0] - g_ctx.m_min[0];
        int32_t sy = (int32_t)g_ctx.m_max[1] - g_ctx.m_min[1];
        int32_t sz = (int32_t)g_ctx.m_max[2] - g_ctx.m_min[2];
        if (sx >= 120 && sy >= 120 && sz >= 120) {
            g_ctx.st.mox = (int16_t)(((int32_t)g_ctx.m_min[0] + g_ctx.m_max[0]) / 2);
            g_ctx.st.moy = (int16_t)(((int32_t)g_ctx.m_min[1] + g_ctx.m_max[1]) / 2);
            g_ctx.st.moz = (int16_t)(((int32_t)g_ctx.m_min[2] + g_ctx.m_max[2]) / 2);
            g_ctx.st.mag_cal = 2;
            g_ctx.mag_pose_snapped = 0;
        } else {
            g_ctx.st.mag_cal = (g_ctx.st.mox || g_ctx.st.moy || g_ctx.st.moz) ? 2 : 0;
        }
        return;
    }
    g_ctx.st.mag_cal = 1;
    g_ctx.m_min[0] = g_ctx.m_min[1] = g_ctx.m_min[2] = 32767;
    g_ctx.m_max[0] = g_ctx.m_max[1] = g_ctx.m_max[2] = -32768;
}

void imu9_load_mag_cal(int16_t ox, int16_t oy, int16_t oz) {
    g_ctx.st.mox = ox;
    g_ctx.st.moy = oy;
    g_ctx.st.moz = oz;
    g_ctx.st.mag_cal = 2;
    g_ctx.mag_pose_snapped = 0;
}

void imu9_feed(const hal_imu_t *imu, const hal_mag_t *mag, uint32_t now_ms) {
    uint32_t dt_ms;
    uint32_t a_norm = 0;
    int32_t mc[3] = {0, 0, 0};
    int32_t ac[3] = {0, 0, 0};
    int32_t err[3] = {0, 0, 0};
    int a_valid = 0, m_valid = 0;

    dt_ms = g_ctx.last_feed_ms ? (now_ms - g_ctx.last_feed_ms) : 20u;
    if (dt_ms < 5u) dt_ms = 5u;
    if (dt_ms > 100u) dt_ms = 100u;
    g_ctx.last_feed_ms = now_ms;

    if (imu) {
        uint32_t a2 = (uint32_t)((int32_t)imu->ax * (int32_t)imu->ax) +
                      (uint32_t)((int32_t)imu->ay * (int32_t)imu->ay) +
                      (uint32_t)((int32_t)imu->az * (int32_t)imu->az);
        a_norm = isqrt32(a2);
        learn_gyro_bias(imu, a_norm);
        g_ctx.prev_g[0] = imu->gx;
        g_ctx.prev_g[1] = imu->gy;
        g_ctx.prev_g[2] = imu->gz;

        g_ctx.st.ax = imu->ax;
        g_ctx.st.ay = imu->ay;
        g_ctx.st.az = imu->az;
        g_ctx.st.gx = (int16_t)(imu->gx - g_ctx.st.gbx);
        g_ctx.st.gy = (int16_t)(imu->gy - g_ctx.st.gby);
        g_ctx.st.gz = (int16_t)(imu->gz - g_ctx.st.gbz);
        g_ctx.st.temp = imu->temp;
        g_ctx.imu_seen = 1;
        g_ctx.imu_ms = now_ms;
        a_valid = (a_norm >= 650u && a_norm <= 1450u);
        ac[0] = imu->ax;
        ac[1] = imu->ay;
        ac[2] = imu->az;
    }

    if (mag) {
        track_mag_cal(mag);
        mc[0] = (int32_t)mag->x - g_ctx.st.mox;
        mc[1] = (int32_t)mag->y - g_ctx.st.moy;
        mc[2] = (int32_t)mag->z - g_ctx.st.moz;
        g_ctx.st.mx = (int16_t)clamp32(mc[0], -32768, 32767);
        g_ctx.st.my = (int16_t)clamp32(mc[1], -32768, 32767);
        g_ctx.st.mz = (int16_t)clamp32(mc[2], -32768, 32767);
        g_ctx.mag_seen = 1;
        g_ctx.mag_ms = now_ms;
        m_valid = (abs32(mc[0]) + abs32(mc[1]) + abs32(mc[2])) > 50;
    }

    g_ctx.st.imu_ok = (uint8_t)(g_ctx.imu_seen && (now_ms - g_ctx.imu_ms) < STALE_MS);
    g_ctx.st.mag_ok = (uint8_t)(g_ctx.mag_seen && (now_ms - g_ctx.mag_ms) < STALE_MS);
    g_ctx.st.fused_axes = (uint8_t)((g_ctx.st.imu_ok ? 6u : 0u) +
                                    (g_ctx.st.mag_ok ? 3u : 0u));

    if (a_valid && (!g_ctx.pose_valid || (m_valid && !g_ctx.mag_pose_snapped))) {
        if (triad_snap(ac, m_valid ? mc : NULL)) {
            extract_euler();
            return;
        }
    }

    /* 仅磁力计可用时做 2D 电子罗盘降级 */
    if (!g_ctx.st.imu_ok && m_valid) {
        int32_t hd = atan2_d10(-mc[0], mc[1]);
        int32_t yd = -hd;
        int32_t hdeg;
        g_ctx.st.roll_d10 = 0;
        g_ctx.st.pitch_d10 = 0;
        g_ctx.st.yaw_d10 = (int16_t)yd;
        hdeg = (hd >= 0) ? (hd + 5) / 10 : (hd - 5) / 10;
        g_ctx.st.heading_deg = (uint16_t)(((hdeg % 360) + 360) % 360);
        return;
    }

    if (!g_ctx.st.imu_ok || !g_ctx.pose_valid) {
        return;
    }

    /* 9 轴 / 6 轴 Mahony Q14 误差反馈 + Q24 四元数积分 */
    {
        int32_t r[3][3];
        int32_t gx = g_ctx.st.gx, gy = g_ctx.st.gy, gz = g_ctx.st.gz;
        int64_t step_x, step_y, step_z;
        int32_t dq0, dq1, dq2, dq3;

        quat_to_rot(r);

        if (a_valid) {
            int32_t a_hat[3], ea[3];
            if (vec_norm_q14(ac[0], ac[1], ac[2], a_hat)) {
                cross_q14(a_hat, r[2], ea);
                err[0] += ea[0];
                err[1] += ea[1];
                err[2] += ea[2];
            }
        }

        if (m_valid) {
            int32_t m_hat[3], h[2], by_ref, bz_ref, w_hat[3], em[3];
            if (vec_norm_q14(mc[0], mc[1], mc[2], m_hat)) {
                h[0] = (r[0][0] * m_hat[0] + r[0][1] * m_hat[1] + r[0][2] * m_hat[2]) >> 14;
                h[1] = (r[1][0] * m_hat[0] + r[1][1] * m_hat[1] + r[1][2] * m_hat[2]) >> 14;
                by_ref = (int32_t)isqrt32((uint32_t)(h[0] * h[0] + h[1] * h[1]));
                bz_ref = (r[2][0] * m_hat[0] + r[2][1] * m_hat[1] + r[2][2] * m_hat[2]) >> 14;
                w_hat[0] = (r[1][0] * by_ref + r[2][0] * bz_ref) >> 14;
                w_hat[1] = (r[1][1] * by_ref + r[2][1] * bz_ref) >> 14;
                w_hat[2] = (r[1][2] * by_ref + r[2][2] * bz_ref) >> 14;
                cross_q14(m_hat, w_hat, em);
                /* 磁倾角较大时水平分量偏小，适度放大磁误差增益 */
                err[0] += (em[0] * 3) >> 1;
                err[1] += (em[1] * 3) >> 1;
                err[2] += (em[2] * 3) >> 1;
            }
        }

        if ((err[0] || err[1] || err[2]) &&
            (abs32(gx) + abs32(gy) + abs32(gz)) < 1500) {
            g_ctx.err_ix = clamp32(g_ctx.err_ix + (int32_t)(((int64_t)err[0] * dt_ms) >> 10), -25600, 25600);
            g_ctx.err_iy = clamp32(g_ctx.err_iy + (int32_t)(((int64_t)err[1] * dt_ms) >> 10), -25600, 25600);
            g_ctx.err_iz = clamp32(g_ctx.err_iz + (int32_t)(((int64_t)err[2] * dt_ms) >> 10), -25600, 25600);
        }

        /* Kp ~ 1.5 rad/s -> (err * 54) >> 10 (0.1dps); Ki 限幅约 ±10 dps */
        gx += ((err[0] * 54) >> 10) + (g_ctx.err_ix >> 8);
        gy += ((err[1] * 54) >> 10) + (g_ctx.err_iy >> 8);
        gz += ((err[2] * 54) >> 10) + (g_ctx.err_iz >> 8);

        /* 0.5 * (0.1 dps) * dt_ms -> Q24 增量: * 14992 >> 10 */
        step_x = ((int64_t)gx * (int64_t)dt_ms * 14992LL) >> 10;
        step_y = ((int64_t)gy * (int64_t)dt_ms * 14992LL) >> 10;
        step_z = ((int64_t)gz * (int64_t)dt_ms * 14992LL) >> 10;

        dq0 = (int32_t)((-(int64_t)g_ctx.q1 * step_x - (int64_t)g_ctx.q2 * step_y - (int64_t)g_ctx.q3 * step_z) >> 24);
        dq1 = (int32_t)(( (int64_t)g_ctx.q0 * step_x + (int64_t)g_ctx.q2 * step_z - (int64_t)g_ctx.q3 * step_y) >> 24);
        dq2 = (int32_t)(( (int64_t)g_ctx.q0 * step_y - (int64_t)g_ctx.q1 * step_z + (int64_t)g_ctx.q3 * step_x) >> 24);
        dq3 = (int32_t)(( (int64_t)g_ctx.q0 * step_z + (int64_t)g_ctx.q1 * step_y - (int64_t)g_ctx.q2 * step_x) >> 24);

        g_ctx.q0 += dq0;
        g_ctx.q1 += dq1;
        g_ctx.q2 += dq2;
        g_ctx.q3 += dq3;
        quat_normalize_q24();
        extract_euler();
    }
}

void imu9_get(imu9_state_t *out) {
    if (out) {
        *out = g_ctx.st;
    }
}

void imu9_get_rot(int16_t out9_q14[9]) {
    int32_t r[3][3];
    int i, j;

    if (!out9_q14) {
        return;
    }
    quat_to_rot(r);
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            out9_q14[i * 3 + j] = (int16_t)clamp32(r[i][j], -16384, 16384);
        }
    }
}

int imu9_is_stale(uint32_t now_ms, uint32_t max_age_ms) {
    if (!g_ctx.last_feed_ms) {
        return 1;
    }
    return (now_ms - g_ctx.last_feed_ms) >= max_age_ms;
}
