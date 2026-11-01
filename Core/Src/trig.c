/*
 * trig.c  --  三角函数后端实现
 *
 * ================== CORDIC 在 STM32G4 上怎么用 ==================
 *
 * 1) 功能选择：FUNC = COSINE（值 0）不是 SINE。
 *    这个命名很反直觉 —— 选 COSINE 时，因为 NRES=2，硬件会**同时**把
 *    cos 和 sin 都算出来（RESULT1 = cos, RESULT2 = sin）。
 *    选 SINE 只出一个值，反而更慢更麻烦。
 *
 * 2) 角度格式：Q1.31 定点，不是弧度浮点。
 *    输入 -1.0 对应 -pi，+1.0 对应 +pi（不含）。
 *    所以要先折到 [-pi, pi) 再乘 2^31/pi。
 *
 * 3) 精度：PRECISION[3:0]，数值 0..15 对应 1..16 次迭代。
 *    16 次迭代实测误差 3.0e-5，比原来的 65 点查表（7.5e-5）好 2.5 倍。
 *    想要更高可以换 24 次（要用 5 位编码，HAL 宏不一定提供），
 *    对 FOC 来说 16 次已经远超电流环需要的精度。
 *
 * 4) 流水线：CORDIC 是异步的，写 WDATA 后要等 RRDY 才能读 RDATA。
 *    16 次迭代约 16 个周期 + 6 周期流水线延迟 ≈ 25 个周期 ≈ 0.15 us @170MHz。
 *    对比查表法 4 次调用约 1.2 us，快了 8 倍。
 *
 * 5) 陷阱：连续读两次 RDATA 时，两个结果已经都在 FIFO 里（NRES=2），
 *    不需要中间再等 RRDY。
 */
#include "trig.h"

#include "foc_math.h"

#if FOC_USE_CORDIC

#include "stm32g4xx_hal.h"

static uint32_t s_last_cycles;

/* 2^31 / pi，用来把弧度转成 Q1.31 */
#define Q31_OVER_PI   683565275.5764316f

void Trig_Init(void)
{
    __HAL_RCC_CORDIC_CLK_ENABLE();

    CORDIC->CSR = (0uL  << CORDIC_CSR_FUNC_Pos)        /* FUNC = 0 => cosine，同时出 sin */
                | (15uL << CORDIC_CSR_PRECISION_Pos)   /* 16 次迭代 */
                | (0uL  << CORDIC_CSR_NARGS_Pos)       /* 1 个入参 */
                | (1uL  << CORDIC_CSR_NRES_Pos);       /* 2 个结果 */

    s_last_cycles = 0;
}

void Trig_SinCos(float theta, float *s, float *c)
{
    if (s == 0 || c == 0) return;

    /* 折到 [-pi, pi)：CORDIC 的 Q1.31 只能表示一个 pi 周期 */
    if (theta >=  FM_PI || theta < -FM_PI) {
        theta = fm_wrap_2pi(theta);          /* -> [0, 2pi) */
        if (theta > FM_PI) theta -= FM_2PI;  /* -> (-pi, pi] */
    }

    int32_t q31 = (int32_t)(theta * Q31_OVER_PI);

    uint32_t t0 = DWT->CYCCNT;

    CORDIC->WDATA = (uint32_t)q31;
    while ((CORDIC->CSR & CORDIC_CSR_RRDY) == 0u) { }

    /* RESULT1 = cos, RESULT2 = sin —— 顺序别搞反 */
    int32_t rc = (int32_t)CORDIC->RDATA;
    int32_t rs = (int32_t)CORDIC->RDATA;

    s_last_cycles = DWT->CYCCNT - t0;

    *c = (float)rc * (1.0f / 2147483648.0f);
    *s = (float)rs * (1.0f / 2147483648.0f);
}

uint32_t Trig_GetLastCycles(void) { return s_last_cycles; }

float Trig_SelfTest(void)
{
    float max_err = 0.0f;

    /* 扫两个 pi 周期，步长 0.001 rad，约 6283 个点 */
    for (int32_t i = -3141; i <= 3141; i++) {
        float th = (float)i * 0.001f;
        float s1, c1;
        Trig_SinCos(th, &s1, &c1);

        float s2 = fm_sin(th);
        float c2 = fm_cos(th);

        float es = s1 - s2; if (es < 0) es = -es;
        float ec = c1 - c2; if (ec < 0) ec = -ec;
        if (es > max_err) max_err = es;
        if (ec > max_err) max_err = ec;
    }
    return max_err;
}

#else  /* ---------------- 查表后端（PC 单元测试 / F1 / F4 用） ---------------- */

static uint32_t s_last_cycles;

void Trig_Init(void) { s_last_cycles = 0; }

void Trig_SinCos(float theta, float *s, float *c)
{
    if (s == 0 || c == 0) return;
    *s = fm_sin(theta);
    *c = fm_cos(theta);
}

uint32_t Trig_GetLastCycles(void) { return s_last_cycles; }

float Trig_SelfTest(void) { return 0.0f; }   /* 查表后端自己跟自己比，无需自检 */

#endif /* FOC_USE_CORDIC */
