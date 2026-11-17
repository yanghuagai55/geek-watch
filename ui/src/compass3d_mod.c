#include "compass3d.h"

/* 纯位置无关 3D 指南针模块：
 *   - 零静态可写变量、零 C 库外部符号依赖
 *   - ARM 构建期编译成纯 Thumb-2 PIC 机器码存放在片外 AT24C256，打开指南针时加载到 SRAM 运行
 *   - 具备俯仰+侧倾全向 3D 球形陀螺框、斜视摄影机景深投影与八面体立体磁针
 */

typedef struct {
    int16_t sx;
    int16_t sy;
    int16_t bz;
} c3d_pt_t;

#if defined(__ARM_ARCH) || defined(__thumb__)
#define C3D_ENTRY_ATTR __attribute__((section(".text.entry"), used))
void compass3d_entry(const compass3d_api_t *api, uint8_t alpha, int clear_bg) C3D_ENTRY_ATTR;
#endif

static void c3d_proj(int16_t cx, int16_t cy, int16_t rad, const int16_t r[9],
                     int32_t wx, int32_t wy, int32_t wz, c3d_pt_t *out) {
    /* 1) World ENU -> Watch Body (v_b = R^T * v_w) */
    int32_t vx = (r[0] * wx + r[3] * wy + r[6] * wz) >> 14;
    int32_t vy = (r[1] * wx + r[4] * wy + r[7] * wz) >> 14;
    int32_t vz = (r[2] * wx + r[5] * wy + r[8] * wz) >> 14;
    /* 2) 固定 ~26° 俯视摄影机仰角 (cos=230/256, sin=112/256)，确保平放桌面也有明显 3D 纵深 */
    int32_t by = (vy * 230 + vz * 112) >> 8;
    int32_t bz = (vz * 230 - vy * 112) >> 8;
    int32_t denom = 760 - bz;
    if (denom < 360) denom = 360;
    out->sx = (int16_t)(cx + (vx * rad * 760) / (denom << 8));
    out->sy = (int16_t)(cy - (by * rad * 760) / (denom << 8));
    out->bz = (int16_t)bz;
}

static char *c3d_put_s(char *dst, const char *src) {
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
    return dst;
}

static char *c3d_put_signed2(char *dst, char tag, int32_t d10) {
    int32_t deg = (d10 >= 0) ? ((d10 + 5) / 10) : ((-d10 + 5) / 10);
    if (deg > 99) deg = 99;
    *dst++ = tag;
    *dst++ = (d10 < 0 && deg > 0) ? '-' : '+';
    *dst++ = (char)('0' + (deg / 10) % 10);
    *dst++ = (char)('0' + deg % 10);
    *dst = '\0';
    return dst;
}

static void c3d_render_impl(const compass3d_api_t *api, uint8_t alpha, int clear_bg) {
    /* 12 等分地平方位圈 (Q8 = 256) */
    static const int16_t c12[12] = { 0, 128, 222, 256, 222, 128, 0, -128, -222, -256, -222, -128 };
    static const int16_t s12[12] = { 256, 222, 128, 0, -128, -222, -256, -222, -128, 0, 128, 222 };
    /* 8 点天顶子午陀螺拱环 (World X=0, Y/Z 平面) */
    static const int16_t m_a[8] = { 240, 170, 0, -170, -240, -170, 0, 170 };
    static const int16_t m_b[8] = { 0, 170, 240, 170, 0, -170, -240, -170 };
    /* 3D 八面体指南针指针 6 顶点（World ENU，Q8 单位）: N, S, E, W, Up, Down */
    static const int16_t n_vx[6] = { 0, 0, 62, -62, 0, 0 };
    static const int16_t n_vy[6] = { 215, -170, 0, 0, 0, 0 };
    static const int16_t n_vz[6] = { 0, 0, 0, 0, 88, -64 };
    /* 12 条八面体棱边: 前 4 条北尖端, 中 4 条赤道菱环, 后 4 条南尾尖 */
    static const uint8_t e_a[12] = { 0, 0, 0, 0, 2, 4, 3, 5, 1, 1, 1, 1 };
    static const uint8_t e_b[12] = { 2, 3, 4, 5, 4, 3, 5, 2, 2, 3, 4, 5 };
    static const char dir_zh[8][8] = {
        "\xe5\x8c\x97", "\xe4\xb8\x9c\xe5\x8c\x97", "\xe4\xb8\x9c", "\xe4\xb8\x9c\xe5\x8d\x97",
        "\xe5\x8d\x97", "\xe8\xa5\xbf\xe5\x8d\x97", "\xe8\xa5\xbf", "\xe8\xa5\xbf\xe5\x8c\x97"
    };
    static const char card_s[4][2] = { "N", "E", "S", "W" };

    imu9_state_t s9;
    int16_t rot[9];
    c3d_pt_t p_first, p_prev, p_cur, z_zen, z_nad;
    c3d_pt_t oct[6];
    int sc = api->ui_w >= 200 ? 2 : 1;
    int margin = (int)api->ui_w * 5 / 100;
    int title_y = (int)api->ui_h * 55 / 1000;
    int title_h = api->text_h("\xe6\x8c\x87", 1);
    int hdg_h = title_h * sc;
    int sub_y = (int)api->ui_h - (int)api->ui_h * 45 / 1000 - title_h;
    int hdg_y = sub_y - (int)api->ui_h * 22 / 1000 - hdg_h;
    int dial_top = title_y + title_h + (int)api->ui_h * 26 / 1000;
    int dial_bot = hdg_y - (int)api->ui_h * 18 / 1000;
    int max_r = (dial_bot - dial_top) >> 1;
    int w_r = (int)api->ui_w * 24 / 100;
    int rad = max_r * 74 / 100;
    int cx = (int)api->ui_w >> 1;
    int cy = dial_top + ((dial_bot - dial_top) >> 1);
    int k;
    char line[28];
    char *p;

    if (!api || alpha == 0) {
        return;
    }
    if (w_r < max_r) {
        rad = w_r;
    }
    if (rad < 18) {
        rad = 18;
    }
    if (margin < 6) {
        margin = 6;
    }

    api->imu_get(&s9);
    api->imu_rot(rot);

    if (clear_bg && alpha == 255) {
        api->fill(0, 0, (int16_t)api->ui_w, (int16_t)api->ui_h, api->col_bg, alpha);
    }

    /* 顶栏：左侧校准态脉冲条 + “指南针” + 右侧 3D 标志 */
    api->round((int16_t)margin, (int16_t)(title_y + 1),
               (int16_t)(sc + 2), (int16_t)(title_h - 2),
               1, (s9.mag_cal == 1) ? api->col_accent : api->col_ring, alpha);
    api->text((int16_t)(margin + sc + 5), (int16_t)title_y,
              "\xe6\x8c\x87\xe5\x8d\x97\xe9\x92\x88", api->col_text, alpha, 1);
    api->text((int16_t)(api->ui_w - margin - 18), (int16_t)title_y + (title_y < 10 ? 5 : 0),
              "3D", api->col_accent, alpha, 1);

    /* 1. 绘制纵深东西陀螺拱环（World Y=0, X/Z 平面）：随侧倾实时摆动（流式投影节省栈空间） */
    z_zen.sx = z_zen.sy = z_zen.bz = 0;
    z_nad = p_first = p_prev = z_zen;
    for (k = 0; k < 8; k++) {
        c3d_proj((int16_t)cx, (int16_t)cy, (int16_t)rad, rot,
                 m_a[k], 0, m_b[k], &p_cur);
        if (k == 2) z_zen = p_cur;
        if (k == 6) z_nad = p_cur;
        if (k == 0) {
            p_first = p_cur;
        } else {
            int z_mid = (int)p_prev.bz + (int)p_cur.bz;
            api->line(p_prev.sx, p_prev.sy, p_cur.sx, p_cur.sy,
                      (z_mid >= 0) ? api->col_dim : api->col_line, alpha);
        }
        p_prev = p_cur;
    }
    {
        int z_mid = (int)p_prev.bz + (int)p_first.bz;
        api->line(p_prev.sx, p_prev.sy, p_first.sx, p_first.sy,
                  (z_mid >= 0) ? api->col_dim : api->col_line, alpha);
    }
    /* 天顶-天底纵轴与顶部天顶节点（mer[2]=Zenith, mer[6]=Nadir） */
    api->line(z_nad.sx, z_nad.sy, z_zen.sx, z_zen.sy, api->col_line, alpha);
    api->round((int16_t)(z_zen.sx - 2), (int16_t)(z_zen.sy - 2),
               4, 4, 2, api->col_ring, alpha);

    /* 2. 绘制 3D 地平方位圈（前亮后暗，直观呈现斜视透视椭圆） */
    for (k = 0; k < 12; k++) {
        c3d_proj((int16_t)cx, (int16_t)cy, (int16_t)rad, rot,
                 c12[k], s12[k], 0, &p_cur);
        if (k == 0) {
            p_first = p_cur;
        } else {
            int z_mid = (int)p_prev.bz + (int)p_cur.bz;
            api->line(p_prev.sx, p_prev.sy, p_cur.sx, p_cur.sy,
                      (z_mid >= 0) ? api->col_ring : api->col_line, alpha);
        }
        p_prev = p_cur;
    }
    {
        int z_mid = (int)p_prev.bz + (int)p_first.bz;
        api->line(p_prev.sx, p_prev.sy, p_first.sx, p_first.sy,
                  (z_mid >= 0) ? api->col_ring : api->col_line, alpha);
    }

    /* 3. 绘制中心 3D 八面体磁针 */
    for (k = 0; k < 6; k++) {
        c3d_proj((int16_t)cx, (int16_t)cy, (int16_t)rad, rot,
                 n_vx[k], n_vy[k], n_vz[k], &oct[k]);
    }
    for (k = 0; k < 12; k++) {
        int a_i = e_a[k];
        int b_i = e_b[k];
        int z_mid = (int)oct[a_i].bz + (int)oct[b_i].bz;
        uint16_t col;
        if (k < 4) {
            col = api->col_accent;
        } else if (k < 8) {
            col = (z_mid >= 0) ? api->col_text : api->col_dim;
        } else {
            col = (z_mid >= 0) ? api->col_dim : api->col_line;
        }
        api->line(oct[a_i].sx, oct[a_i].sy, oct[b_i].sx, oct[b_i].sy, col, alpha);
    }
    api->line((int16_t)cx, (int16_t)cy, oct[0].sx, oct[0].sy, api->col_accent, alpha);
    api->round((int16_t)(cx - 2), (int16_t)(cy - 2), 5, 5, 2, api->col_ring, alpha);

    /* 4. 3D 方位标识 N / E / S / W */
    for (k = 0; k < 4; k++) {
        c3d_pt_t pt;
        int idx = k * 3;
        uint16_t col;
        c3d_proj((int16_t)cx, (int16_t)cy, (int16_t)(rad + sc * 7), rot,
                 c12[idx], s12[idx], 0, &pt);
        col = (k == 0) ? api->col_accent : (pt.bz >= -40 ? api->col_text : api->col_dim);
        api->text_center(pt.sx, (int16_t)(pt.sy - (title_h >> 1)),
                         card_s[k], col, alpha, 1);
    }

    /* 5. 底部航向角 + 3D 俯仰/横滚角与融合阶数 */
    p = line;
    if (s9.fused_axes) {
        unsigned h = s9.heading_deg % 360u;
        int dir_i = (int)((h + 22u) / 45u) & 7;
        *p++ = (char)('0' + (h / 100u) % 10u);
        *p++ = (char)('0' + (h / 10u) % 10u);
        *p++ = (char)('0' + h % 10u);
        p = c3d_put_s(p, "\xe5\xba\xa6 ");
        p = c3d_put_s(p, dir_zh[dir_i]);
    } else {
        p = c3d_put_s(p, "---\xe5\xba\xa6 --");
    }
    api->text_center((int16_t)cx, (int16_t)hdg_y, line, api->col_text, alpha, (uint8_t)sc);

    p = line;
    if (s9.mag_cal == 1) {
        p = c3d_put_s(p, "\xe6\xa0\xa1\xe5\x87\x86\xe4\xb8\xad 360\xe5\xba\xa6 K2");
    } else if (s9.fused_axes) {
        *p++ = (char)('0' + s9.fused_axes);
        p = c3d_put_s(p, "D ");
        p = c3d_put_signed2(p, 'P', s9.pitch_d10);
        *p++ = ' ';
        p = c3d_put_signed2(p, 'R', s9.roll_d10);
    } else {
        p = c3d_put_s(p, "-- K2\xe6\xa0\xa1\xe5\x87\x86");
    }
    api->text_center((int16_t)cx, (int16_t)sub_y, line,
                     (s9.mag_cal == 1) ? api->col_accent : api->col_dim, alpha, 1);
}

#if defined(__ARM_ARCH) || defined(__thumb__)
void compass3d_entry(const compass3d_api_t *api, uint8_t alpha, int clear_bg) {
    c3d_render_impl(api, alpha, clear_bg);
}
#endif

#if !defined(COMPASS3D_BLOB_ONLY)
void compass3d_render_c(const compass3d_api_t *api, uint8_t alpha, int clear_bg) {
    c3d_render_impl(api, alpha, clear_bg);
}
#endif
