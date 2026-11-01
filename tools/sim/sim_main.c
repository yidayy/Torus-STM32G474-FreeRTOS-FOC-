/*
 * sim_main.c  --  把 wushua_g474 的控制算法搬到 PC 上跑闭环仿真
 *
 * 目的：在上板之前，先算出三件事
 *   1) config.h 里的 PI 参数到底行不行（超调多少、多长时间到位）
 *   2) 正反转切换会不会冲过头
 *   3) 电流限幅 / 电压圆限幅有没有真的起作用
 *
 * 仿真包含的"不理想因素"：
 *   - PWM 一拍延迟（指令这一拍算出来，下一拍才作用到电机）
 *   - 12 bit 编码器量化
 *   - 12 bit ADC 量化 + 零偏
 *
 * 编译：见 run_sim.sh
 * 输出：CSV 到 stdout，第一行是表头
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "config.h"
#include "foc_core.h"
#include "foc_math.h"
#include "trig.h"

/* ================== 电机模型参数（2804 云台电机）==================
 * Ke 和 Kt 在 SI 单位下数值相等，这是能量守恒的必然结果。
 * KV=100 rpm/V  ->  Ke = 60/(2*pi*100) = 0.0955 V·s/rad
 * psi_f = Kt / (1.5 * 极对数)
 */
#define P_MOTOR_PP      7
#define P_RS_OHM        6.5f
#define P_LS_H          0.0012f
#define P_PSI_F         0.0091f
#define P_J_INERTIA     2.0e-4f
#define P_B_DAMP        2.0e-4f

#define P_SIM_DT        (1.0f / (float)CFG_PWM_FREQ_HZ)   /* 50 us */
#define P_SPD_EVERY     20                                 /* 1 ms */

typedef struct {
    float theta_m;      /* 机械角 rad */
    float omega_m;      /* 机械角速度 rad/s */
    float id, iq;       /* 实际 dq 电流 */
    float ud, uq;       /* 上一拍实际加到电机上的电压（模拟 PWM 延迟）*/
} motor_t;

/* ------------------------------------------------------------------ */
static void motor_step(motor_t *m, float ud, float uq, float dt)
{
    /* 电角速度 = 机械角速度 * 极对数 */
    float we = m->omega_m * (float)P_MOTOR_PP;

    /* 表贴式：Ld = Lq = Ls。反电动势项 -we*psi_f 在 q 轴 */
    float did = (ud - P_RS_OHM * m->id + we * P_LS_H * m->iq) / P_LS_H;
    float diq = (uq - P_RS_OHM * m->iq - we * P_LS_H * m->id
                 - we * P_PSI_F) / P_LS_H;

    m->id += did * dt;
    m->iq += diq * dt;

    /* 转矩 T = 1.5 * p * psi_f * iq（id 不产生转矩，这是 id=0 的依据）*/
    float T = 1.5f * (float)P_MOTOR_PP * P_PSI_F * m->iq;

    float alpha = (T - P_B_DAMP * m->omega_m) / P_J_INERTIA;
    m->omega_m += alpha * dt;
    m->theta_m += m->omega_m * dt;

    while (m->theta_m >= FM_2PI) m->theta_m -= FM_2PI;
    while (m->theta_m <  0.0f)   m->theta_m += FM_2PI;
}

/* 编码器：12 bit 量化 */
static float encoder_read(float theta_m)
{
    float raw = theta_m / FM_2PI * (float)CFG_ENCODER_CPR;
    int32_t q = (int32_t)(raw + 0.5f);
    if (q >= CFG_ENCODER_CPR) q -= CFG_ENCODER_CPR;
    return (float)q / (float)CFG_ENCODER_CPR * FM_2PI;
}

/* ADC：12 bit 量化 + 零偏，再从 dq 反算两相电流 */
static void adc_read(const motor_t *m, float theta_e, float *ia, float *ib)
{
    float s, c;
    Trig_SinCos(theta_e, &s, &c);

    /* 反 Park：dq -> alpha-beta（和 FOC_Step 里的正变换互为逆运算）*/
    float ialpha = m->id * c - m->iq * s;
    float ibeta  = m->id * s + m->iq * c;

    /* 反 Clarke：alpha-beta -> a-b */
    float ra = ialpha;
    float rb = -0.5f * ialpha + FM_SQRT3_2 * ibeta;

    /* 换成 ADC 码再换回来，模拟量化误差 */
    const float amp_per_volt = CFG_CS_AMPERE_PER_VOLT;
    const float vref = CFG_ADC_VREF_V, fs = CFG_ADC_FULL_SCALE;

    float ca = 2048.0f + (ra / amp_per_volt) * (fs / vref);
    float cb = 2048.0f + (rb / amp_per_volt) * (fs / vref);
    ca = floorf(ca + 0.5f);
    cb = floorf(cb + 0.5f);

    *ia = (ca - 2048.0f) * (vref / fs) * amp_per_volt;
    *ib = (cb - 2048.0f) * (vref / fs) * amp_per_volt;
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    float t_end = (argc > 1) ? (float)atof(argv[1]) : 3.0f;
    float kp_in = (argc > 2) ? (float)atof(argv[2]) : CFG_PID_SPEED_KP;
    float ki_in = (argc > 3) ? (float)atof(argv[3]) : CFG_PID_SPEED_KI;

    FOC_Init();
    FOC_Enable(1);
    g_foc.vbus = 12.0f;

    motor_t m = {0};

    pi_t pi_spd;
    PI_Reset(&pi_spd, kp_in, ki_in, CFG_CURRENT_MAX_A);

    float target = 0.0f;
    float prev_mech = 0.0f;
    float omega_lpf = 0.0f;
    int   primed = 0;

    float dt = P_SIM_DT;
    int   n_steps = (int)(t_end / dt);

    /* CSV 表头 */
    printf("t,target,omega,omega_meas,iq,id,da,db,dc,limit_hit\n");

    for (int k = 0; k < n_steps; k++) {
        float t = (float)k * dt;

        /* ---- 场景编排 ---- */
        if (t < 0.05f)        target = 0.0f;      /* 先停 50 ms */
        else if (t < 1.5f)    target = 50.0f;     /* 0 -> +50 rad/s 阶跃 */
        else                  target = -50.0f;    /* 1.5 s 反向到 -50 */

        /* ---- 速度环：1 ms 一次（模拟 Task_Speed）---- */
        if ((k % P_SPD_EVERY) == 0) {
            float mech = encoder_read(m.theta_m);

            float dmech = fm_angle_diff(mech, prev_mech);
            prev_mech = mech;

            float omega_raw = dmech * (float)(CFG_PWM_FREQ_HZ / P_SPD_EVERY);
            if (!primed) { omega_lpf = omega_raw; primed = 1; }
            omega_lpf += 0.35f * (omega_raw - omega_lpf);

            g_foc.iq_ref = PI_Update(&pi_spd, target - omega_lpf,
                                     1.0f / (float)(CFG_PWM_FREQ_HZ / P_SPD_EVERY));
        }

        /* ---- 电角度（编码器量化后 * 极对数）---- */
        float mech_q = encoder_read(m.theta_m);
        float theta_e = fm_wrap_2pi(mech_q * (float)P_MOTOR_PP);

        /* ---- ADC 采样 ---- */
        float ia, ib;
        adc_read(&m, theta_e, &ia, &ib);

        g_foc.ia = ia;
        g_foc.ib = ib;
        g_foc.theta_e = theta_e;

        /* ---- FOC 电流环 ---- */
        FOC_Step();

        /* ---- 电机步进：电压用上一拍的结果，模拟 PWM 延迟 ---- */
        motor_step(&m, m.ud, m.uq, dt);
        m.ud = g_foc.ud;
        m.uq = g_foc.uq;

        /* ---- 每 1 ms 记录一次 ---- */
        if ((k % P_SPD_EVERY) == 0) {
            int lim = (fabsf(g_foc.iq_ref) >= CFG_CURRENT_MAX_A - 1e-4f) ? 1 : 0;
            printf("%.4f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n",
                   (double)t, (double)target, (double)m.omega_m,
                   (double)omega_lpf, (double)m.iq, (double)m.id,
                   (double)g_foc.da, (double)g_foc.db, (double)g_foc.dc, lim);
        }
    }

    fprintf(stderr, "sim done: %.1f s, %d steps, iq_max_ref=%.2f\n",
            (double)t_end, n_steps, (double)CFG_CURRENT_MAX_A);
    return 0;
}
