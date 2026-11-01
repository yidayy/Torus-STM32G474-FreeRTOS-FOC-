/*
 * foc_core.c  --  电流环实现
 *
 * 一次 FOC_Step 的完整链条：
 *   ia/ib -> Clarke -> Park -> PI(d,q) -> 逆Park -> SVPWM -> 三路占空比
 * 全部在 SRAM 里跑，无阻塞，20 kHz 下目标 < 20 us（F103 @72MHz）。
 */
#include <string.h>
#include <math.h>

#include "config.h"
#include "foc_core.h"
#include "foc_math.h"
#include "trig.h"

volatile foc_state_t g_foc;

static pi_t s_pi_d;
static pi_t s_pi_q;

/* ------------------------------------------------------------------ */
void PI_Reset(pi_t *p, float kp, float ki, float out_max)
{
    p->kp      = kp;
    p->ki      = ki;
    p->out_max = out_max;
    p->integral = 0.0f;
}

float PI_Update(pi_t *p, float err, float dt)
{
    float p_term = p->kp * err;

    p->integral += p->ki * err * dt;

    float out = p_term + p->integral;

    /*
     * 抗积分饱和用 back-calculation，不是简单钳位积分。
     *
     * 老写法（错的）：
     *     if (integral > out_max) integral = out_max;
     * 问题：输出是 out = Kp*err + integral。当 Kp*err 已经 5 A、限幅 2.2 A 时，
     * 积分被钳到 2.2，等误差降到 0，输出还是 2.2（满力矩），电机照样冲过头。
     * 仿真里这表现为"ζ=1.0 却还有 34% 超调"，怎么调参数都压不下去。
     *
     * 正确做法：输出饱和时，把积分项反推成"刚好不饱和"的值。
     * 这样误差一减小，输出立刻跟着掉，不会拖着一堆积分。
     */
    if (out > p->out_max) {
        out = p->out_max;
        p->integral = out - p_term;
    } else if (out < -p->out_max) {
        out = -p->out_max;
        p->integral = out - p_term;
    }

    return out;
}

/* ------------------------------------------------------------------ */
void FOC_Init(void)
{
    memset((void *)&g_foc, 0, sizeof(g_foc));

    Trig_Init();        /* CORDIC 时钟 + CSR 配置，必须在第一次 FOC_Step 之前 */

    g_foc.current_limit = CFG_CURRENT_MAX_A;
    g_foc.vbus          = 12.0f;

    PI_Reset(&s_pi_d, CFG_PID_CURRENT_KP, CFG_PID_CURRENT_KI, CFG_VOLTAGE_LIMIT_V);
    PI_Reset(&s_pi_q, CFG_PID_CURRENT_KP, CFG_PID_CURRENT_KI, CFG_VOLTAGE_LIMIT_V);
}

void FOC_Enable(uint8_t en)
{
    g_foc.enabled = en ? 1u : 0u;
    if (!en) {
        PI_Reset(&s_pi_d, CFG_PID_CURRENT_KP, CFG_PID_CURRENT_KI, CFG_VOLTAGE_LIMIT_V);
        PI_Reset(&s_pi_q, CFG_PID_CURRENT_KP, CFG_PID_CURRENT_KI, CFG_VOLTAGE_LIMIT_V);
        g_foc.da = g_foc.db = g_foc.dc = 0.5f;
    }
}

void FOC_ClearFault(void)
{
    g_foc.fault = FOC_FAULT_NONE;
}

void FOC_SetCurrentGains(float kp, float ki)
{
    /* 只改增益，不动 integral。
       在线整定时如果连积分一起清，每次改参数都会产生一次力矩台阶，
       在示波器上看起来像"调参把电机调抖了"，其实是清了积分。 */
    s_pi_d.kp = kp;
    s_pi_d.ki = ki;
    s_pi_q.kp = kp;
    s_pi_q.ki = ki;
}

/* ------------------------------------------------------------------ */
/* 一次电流环迭代。dt 固定 = 1/CFG_PWM_FREQ_HZ */
#define FOC_DT  (1.0f / (float)CFG_PWM_FREQ_HZ)

void FOC_Step(void)
{
    g_foc.isr_count++;

    /* ---- 1. 过流保护：先判后算，超限直接关断 ---- */
    if ((g_foc.ia > CFG_PROT_OC_A) || (g_foc.ia < -CFG_PROT_OC_A) ||
        (g_foc.ib > CFG_PROT_OC_A) || (g_foc.ib < -CFG_PROT_OC_A)) {
        g_foc.fault |= FOC_FAULT_OC;
    }

    if (g_foc.fault != FOC_FAULT_NONE) {
        g_foc.enabled = 0u;
        g_foc.da = g_foc.db = g_foc.dc = 0.5f;   /* 50% = 三相短路，电机自由停 */
        g_foc.iq = 0.0f;
        g_foc.id = 0.0f;
        return;
    }

    if (!g_foc.enabled) {
        g_foc.da = g_foc.db = g_foc.dc = 0.5f;
        return;
    }

    /* ---- 2. Clarke（三相电流和恒为 0，只需两相）---- */
    float ialpha, ibeta, id, iq;
    fm_clarke(g_foc.ia, g_foc.ib, &ialpha, &ibeta);
    g_foc.ialpha = ialpha;
    g_foc.ibeta  = ibeta;

    /* ---- 3. sin/cos 只算一次，Park 和后面的逆 Park 共用 ----
       走 CORDIC 时这一次调用约 0.15 us；
       老写法各算一遍 = 4 次三角函数，是 20 kHz 里最贵的一段。 */
    float s_th, c_th;
    Trig_SinCos(g_foc.theta_e, &s_th, &c_th);

    fm_park_sc(ialpha, ibeta, s_th, c_th, &id, &iq);
    g_foc.id = id;
    g_foc.iq = iq;

    /* ---- 4. 电流限幅：d 轴恒为 0，q 轴受上限约束 ---- */
    if (g_foc.iq_ref >  g_foc.current_limit) g_foc.iq_ref =  g_foc.current_limit;
    if (g_foc.iq_ref < -g_foc.current_limit) g_foc.iq_ref = -g_foc.current_limit;

    float ud = PI_Update(&s_pi_d, g_foc.id_ref - id, FOC_DT);
    float uq = PI_Update(&s_pi_q, g_foc.iq_ref - iq, FOC_DT);
    g_foc.ud = ud;
    g_foc.uq = uq;

    /* ---- 5. 电压圆限幅：dq 合成不能超过母线能力，否则波形削顶 ---- */
    float umag = ud * ud + uq * uq;
    float ulim = g_foc.vbus * FM_1_SQRT3;
    if (umag > ulim * ulim) {
        float scale = ulim / sqrtf(umag);
        ud *= scale;
        uq *= scale;
    }

    /* ---- 6. 逆 Park（复用同一对 sin/cos）+ SVPWM ---- */
    float ualpha, ubeta;
    fm_inv_park_sc(ud, uq, s_th, c_th, &ualpha, &ubeta);

    float da, db, dc;
    fm_svpwm(ualpha, ubeta, g_foc.vbus, &da, &db, &dc);
    g_foc.da = da;
    g_foc.db = db;
    g_foc.dc = dc;
}
