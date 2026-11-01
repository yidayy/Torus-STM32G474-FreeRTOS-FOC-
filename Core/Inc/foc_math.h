/*
 * foc_math.h  --  纯数学层，不依赖 HAL / FreeRTOS
 *
 * 为什么要查表不用 sinf()：
 *   F103 无 FPU，软件浮点 sinf() 单次约 20~40 us，20 kHz 电流环周期只有 50 us，
 *   一次 FOC 要 2 次 sin + 2 次 cos，直接用 math.h 必然超时。
 *   65 点表 + 线性插值（象限映射）实测约 1.5 us，误差 < 1.5e-4 rad。
 *   换到 G431（有 FPU + CORDIC）后可以把 FOC_USE_LUT 关掉走硬件。
 *
 * 本文件可以在 PC 上编译（不 include 任何 MCU 头），方便跑单元测试。
 */
#ifndef FOC_MATH_H
#define FOC_MATH_H

#include <stdint.h>

#ifndef FOC_USE_LUT
#define FOC_USE_LUT 1
#endif

#define FM_PI       3.14159265358979f
#define FM_PIO2     1.57079632679490f
#define FM_2PI      6.28318530717959f
#define FM_SQRT3_2  0.86602540378444f
#define FM_1_SQRT3  0.57735026918963f

/* 0 .. pi/2 共 65 点（含端点），步长 pi/128 */
static const float FM_SIN_Q[65] = {
    0.000000000f, 0.024541229f, 0.049067674f, 0.073564564f, 0.098017140f, 0.122410675f, 0.146730474f, 0.170961889f,
    0.195090322f, 0.219101240f, 0.242980180f, 0.266712757f, 0.290284677f, 0.313681740f, 0.336889853f, 0.359895037f,
    0.382683432f, 0.405241314f, 0.427555093f, 0.449611330f, 0.471396737f, 0.492898192f, 0.514102744f, 0.534997620f,
    0.555570233f, 0.575808191f, 0.595699304f, 0.615231591f, 0.634393284f, 0.653172843f, 0.671558955f, 0.689540545f,
    0.707106781f, 0.724247083f, 0.740951125f, 0.757208847f, 0.773010453f, 0.788346428f, 0.803207531f, 0.817584813f,
    0.831469612f, 0.844853565f, 0.857728610f, 0.870086991f, 0.881921264f, 0.893224301f, 0.903989293f, 0.914209756f,
    0.923879533f, 0.932992799f, 0.941544065f, 0.949528181f, 0.956940336f, 0.963776066f, 0.970031253f, 0.975702130f,
    0.980785280f, 0.985277642f, 0.989176510f, 0.992479535f, 0.995184727f, 0.997290457f, 0.998795456f, 0.999698819f,
    1.000000000f
};

static inline float fm_sin(float x)
{
    float sign = 1.0f;
    if (x != x) return 0.0f;                 /* NaN 兜底 */

    /* 折到 [-2pi, 2pi]，避免大角度时精度塌掉 */
    if (x > FM_2PI || x < -FM_2PI) {
        x -= FM_2PI * (float)((int32_t)(x / FM_2PI));
    }
    if (x < 0.0f) { x = -x; sign = -sign; }         /* sin(-x) = -sin(x)   */
    if (x > FM_PI) { x -= FM_PI; sign = -sign; }    /* sin(x+pi) = -sin(x) */
    if (x > FM_PIO2) { x = FM_PI - x; }             /* sin(pi-x) = sin(x)  */

    float t = x * (64.0f / FM_PIO2);
    uint32_t i = (uint32_t)t;
    if (i >= 64u) return sign * FM_SIN_Q[64];
    float f = t - (float)i;
    return sign * (FM_SIN_Q[i] + (FM_SIN_Q[i + 1u] - FM_SIN_Q[i]) * f);
}

static inline float fm_cos(float x) { return fm_sin(x + FM_PIO2); }

/* ---- Clarke（等幅值）：ia+ib+ic = 0 ---- */
static inline void fm_clarke(float ia, float ib, float *ialpha, float *ibeta)
{
    *ialpha = ia;
    *ibeta  = (ia + 2.0f * ib) * FM_1_SQRT3;
}

/* ---- Park：静止 alpha-beta -> 旋转 d-q ---- */
static inline void fm_park(float ialpha, float ibeta, float theta, float *id, float *iq)
{
    float s = fm_sin(theta);
    float c = fm_cos(theta);
    *id =  ialpha * c + ibeta * s;
    *iq = -ialpha * s + ibeta * c;
}

/* ---- 逆 Park：d-q -> alpha-beta ---- */
static inline void fm_inv_park(float ud, float uq, float theta, float *ualpha, float *ubeta)
{
    float s = fm_sin(theta);
    float c = fm_cos(theta);
    *ualpha = ud * c - uq * s;
    *ubeta  = ud * s + uq * c;
}

/* ---- 带 sin/cos 入参的版本 ----
 * Park 和逆 Park 用的是同一个电角度，把 sin/cos 提到外面算一次就够。
 * 走 CORDIC 时这一改直接把 4 次三角函数降到 1 次硬件调用。 */
static inline void fm_park_sc(float ialpha, float ibeta, float s, float c,
                              float *id, float *iq)
{
    *id =  ialpha * c + ibeta * s;
    *iq = -ialpha * s + ibeta * c;
}

static inline void fm_inv_park_sc(float ud, float uq, float s, float c,
                                  float *ualpha, float *ubeta)
{
    *ualpha = ud * c - uq * s;
    *ubeta  = ud * s + uq * c;
}

/*
 * SVPWM（等效实现：正弦调制 + 零序注入）
 *   输入 ualpha/ubeta（伏特），vbus（伏特），输出三路占空比 0..1
 *   零序注入 -(max+min)/2 与七段式 SVPWM 的线性区完全等价，
 *   但不用查扇区、不用算 T1/T2，代码量少一个数量级，且天然支持过调制。
 *   线性区上限：相电压峰值 <= vbus/sqrt(3) （比 SPWM 高 15.5%）
 */
static inline void fm_svpwm(float ualpha, float ubeta, float vbus,
                            float *da, float *db, float *dc)
{
    float ua = ualpha;
    float ub = -0.5f * ualpha + FM_SQRT3_2 * ubeta;
    float uc = -0.5f * ualpha - FM_SQRT3_2 * ubeta;

    float vmax = ua, vmin = ua;
    if (ub > vmax) vmax = ub;
    if (uc > vmax) vmax = uc;
    if (ub < vmin) vmin = ub;
    if (uc < vmin) vmin = uc;

    float offset = -0.5f * (vmax + vmin);
    float half   = 0.5f * vbus;
    if (half < 1e-6f) half = 1e-6f;

    *da = (ua + offset) / vbus + 0.5f;
    *db = (ub + offset) / vbus + 0.5f;
    *dc = (uc + offset) / vbus + 0.5f;

    /* 限幅，防止 100% 占空比把自举电容饿死 */
    if (*da > 0.98f) *da = 0.98f; else if (*da < 0.02f) *da = 0.02f;
    if (*db > 0.98f) *db = 0.98f; else if (*db < 0.02f) *db = 0.02f;
    if (*dc > 0.98f) *dc = 0.98f; else if (*dc < 0.02f) *dc = 0.02f;
}

/* ---- 角度归一化到 [0, 2pi) ---- */
static inline float fm_wrap_2pi(float x)
{
    while (x >= FM_2PI) x -= FM_2PI;
    while (x < 0.0f)    x += FM_2PI;
    return x;
}

/* ---- 最短角差，结果在 (-pi, pi] ---- */
static inline float fm_angle_diff(float a, float b)
{
    float d = fm_wrap_2pi(a - b);
    if (d > FM_PI) d -= FM_2PI;
    return d;
}

#endif /* FOC_MATH_H */
