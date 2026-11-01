/*
 * trig.h  --  三角函数后端（可切换）
 *
 * 为什么单独抽一层：
 *   foc_core.c 只关心"给我 sin 和 cos"，不关心是查表还是 CORDIC。
 *   这样 PC 上的单元测试还能跑（后端退回查表），
 *   而 G4 上用硬件加速（后端切 CORDIC）。
 *
 * 关键优化点（CORDIC 版才有的收益）：
 *   Park 变换和逆 Park 用的是**同一个电角度**。
 *   老代码各调一次 fm_sin/fm_cos = 4 次三角函数，
 *   现在一次 Trig_SinCos() 同时出 sin 和 cos，砍掉 75% 的计算量。
 */
#ifndef TRIG_H
#define TRIG_H

#include <stdint.h>

/* 1 = 用 G4 的 CORDIC 硬件；0 = 用 foc_math.h 的查表。
   在 G474 上开 1，在没有 CORDIC 的芯片（F1/F4）上必须 0。 */
#ifndef FOC_USE_CORDIC
#define FOC_USE_CORDIC  1
#endif

void Trig_Init(void);

/* 同时返回 sin 和 cos，输入角度单位 rad，内部自动折到 [-pi, pi) */
void Trig_SinCos(float theta, float *s, float *c);

/* 最近一次 Trig_SinCos 消耗的 CPU 周期数（DWT 测量，调试用） */
uint32_t Trig_GetLastCycles(void);

/*
 * 开机自检：把 CORDIC 和查表法的结果逐个对比，返回最大绝对误差。
 * 用途 —— 证明硬件真的在跑，而不是默默退回了查表。
 * 正常值：CORDIC 版应 <= 3e-5（16 次迭代理论误差 1.5e-5 + 查表误差 7.5e-5 的量级重叠）
 * 如果返回 0.0 或明显偏大，说明 CORDIC 没配上（CSR 写错 / 时钟没开）。
 */
float Trig_SelfTest(void);

#endif /* TRIG_H */
