/* 计步算法离线自测。不依赖芯片头文件，只喂合成加速度。
   用法：编译 sport_test.c + ../ui/src/sport.c 后运行，返回0=PASS。 */
#include "../ui/sport.h"
#include <stdio.h>

static void feed(hal_imu_t *s, uint32_t *t) {
    sport_feed(s, *t);
    *t += 20;   /* 50Hz */
}

static void stationary(uint32_t *t, int ms) {
    hal_imu_t s = {0};
    int i;
    s.az = 1000; s.temp = 280;
    for (i = 0; i < ms / 20; i++) feed(&s, t);
}

static void walking(uint32_t *t, int ms) {
    hal_imu_t s = {0};
    int i;
    s.temp = 280;
    for (i = 0; i < ms / 20; i++) {
        uint32_t phase = *t % 500u;
        int swing = (int)((phase < 250u ? phase : 500u - phase) * 400u / 250u);
        s.az = (int16_t)(900 + swing);
        s.gz = (int16_t)(swing - 200);
        feed(&s, t);
    }
}

static void gesture(uint32_t *t) {
    hal_imu_t s = {0};
    int i;
    s.temp = 280;
    /* 陀螺爆发 + 重力方向翻转 */
    for (i = 0; i < 10; i++) { s.az = 400; s.gy = 2000; feed(&s, t); }
    for (i = 0; i < 35; i++) { s.az = 400; s.gy = 0; feed(&s, t); }
}

int main(void) {
    uint32_t t = 0;
    int fail = 0;
    sport_stats_t st;

    sport_init();
    stationary(&t, 5000);
    sport_get(&st);
    printf("stationary steps=%lu (expect 0)\n", (unsigned long)st.steps);
    if (st.steps != 0) fail = 1;

    walking(&t, 10000);
    sport_get(&st);
    printf("walking steps=%lu dist=%lum cal=%lukcal (expect ~20)\n",
           (unsigned long)st.steps, (unsigned long)st.distance_m,
           (unsigned long)st.calories);
    if (st.steps < 16 || st.steps > 24) fail = 1;

    gesture(&t);
    printf("raise_wrist=%d (expect 1)\n", sport_raise_wrist());
    if (!sport_raise_wrist()) fail = 1;

    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}