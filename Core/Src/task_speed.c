/*
 * task_speed.c  --  速度环 + 命令消费（P6，1 kHz）
 *
 * 这个任务是控制链的"调度中枢"，干四件事：
 *   1) 从 g_qCmd 取命令（CAN / 串口下来的都在这里落地）
 *   2) 读编码器，算机械角 / 电角度 / 机械角速度
 *   3) 按系统状态决定要不要输出力矩，跑速度 PI
 *   4) 把这一拍的数据推给波形管道
 *
 * 为什么命令在这里消费而不是单独开一个 Task_App：
 *   命令最终都是改"控制目标"，而控制目标只有控制环自己最清楚怎么用。
 *   单独开一个任务处理命令，那个任务还是得写共享变量给控制环读，
 *   等于把数据竞争换个地方放。
 */
#include <math.h>
#include <stdio.h>

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "task_metrics.h"
#include "foc_core.h"
#include "foc_math.h"
#include "encoder.h"
#include "data_pipe.h"
#include "can_app.h"

#define SPD_PERIOD_MS   (1000u / CFG_TASK_SPEED_HZ)

/* 控制目标：只有本任务写；位置环通过 g_pos_ref / g_spd_ref 单向投递 */
static struct {
    uint8_t mode;
    float   speed_ref;
    float   iq_ref;
} s_tgt;

volatile float g_pos_ref    = 0.0f;   /* 位置环写，本任务读 */
volatile float g_spd_ref    = 0.0f;   /* 本任务写（位置模式给速度环用）*/
volatile float g_omega_meas = 0.0f;
volatile uint32_t g_speed_loops = 0;

static pi_t        s_pi;
static sys_param_t s_prm;

/* ------------------------------------------------------------------ */
static void handle_msg(const app_msg_t *m)
{
    switch (m->id) {
    case MSG_ENABLE:
        if (Sys_RequestEnable(m->value > 0.5f) != 0) {
            printf("\r\n[CMD] enable refused, state=%s\r\n",
                   Sys_StateName(Sys_GetState()));
        }
        break;

    case MSG_SET_MODE:
        s_tgt.mode = (uint8_t)m->value;
        printf("\r\n[CMD] mode=%u\r\n", (unsigned)s_tgt.mode);
        break;

    case MSG_SET_SPEED:
        s_tgt.mode      = MODE_SPEED;
        s_tgt.speed_ref = m->value;          /* 负值 = 反转 */
        break;

    case MSG_SET_POS:
        s_tgt.mode  = MODE_POSITION;
        g_pos_ref   = m->value;
        break;

    case MSG_SET_IQ:
        s_tgt.mode   = MODE_TORQUE;
        s_tgt.iq_ref = m->value;
        break;

    case MSG_CLEAR_FAULT:
        Sys_ClearFault();
        printf("\r\n[CMD] fault cleared\r\n");
        break;

    case MSG_SET_PARAM:
        (void)Sys_SetParamItem((param_id_t)m->param, m->value);
        break;

    case MSG_ESTOP:
        (void)Sys_RequestEnable(0);
        printf("\r\n[CMD] ESTOP\r\n");
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
void Task_Speed(void *arg)
{
    (void)arg;

    TickType_t last       = xTaskGetTickCount();
    uint32_t   last_cycle = Met_Now();
    const uint32_t period_cyc = CFG_SYSCLK_HZ / CFG_TASK_SPEED_HZ;

    float   prev_mech = Encoder_GetMechAngle();
    float   omega_lpf = 0.0f;
    uint8_t primed    = 0;

    Sys_GetParam(&s_prm);
    PI_Reset(&s_pi, s_prm.kp_spd, s_prm.ki_spd, s_prm.iq_limit);

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SPD_PERIOD_MS));

        uint32_t now = Met_Now();
        Metrics_MarkWake(MET_SPEED, last_cycle + period_cyc, now);
        last_cycle = now;

        /* ---- 1. 参数变更：只在事件置位时才重新读，避免每拍抢锁 ---- */
        if ((xEventGroupGetBits(g_evtGroup) & EVT_PARAM_CHG) != 0) {
            xEventGroupClearBits(g_evtGroup, EVT_PARAM_CHG);
            Sys_GetParam(&s_prm);
            PI_Reset(&s_pi, s_prm.kp_spd, s_prm.ki_spd, s_prm.iq_limit);
        }

        /* ---- 2. 命令消费（非阻塞，一次清空队列）---- */
        app_msg_t m;
        while (xQueueReceive(g_qCmd, &m, 0) == pdTRUE) {
            handle_msg(&m);
        }

        /* ---- 3. 编码器 ---- */
        (void)Encoder_Read();
        float mech = Encoder_GetMechAngle();
        g_foc.theta_e = Encoder_GetElecAngle();

        float dmech = fm_angle_diff(mech, prev_mech);
        prev_mech = mech;

        float omega_raw = dmech * (float)CFG_TASK_SPEED_HZ;
        if (!primed) { omega_lpf = omega_raw; primed = 1; }
        omega_lpf += 0.35f * (omega_raw - omega_lpf);
        g_omega_meas = omega_lpf;

        /* ---- 4. 状态机决定是否出力 ---- */
        sys_state_t st = Sys_GetState();

        if (st != SYS_READY && st != SYS_RUN) {
            g_foc.iq_ref  = 0.0f;
            s_pi.integral = 0.0f;
            Wave_Push(0, 0, (uint8_t)st, Sys_GetFault());
            g_speed_loops++;
            continue;
        }

        /* ---- 5. 目标值 + READY<->RUN 迁移 ---- */
        float target;

        switch (s_tgt.mode) {
        case MODE_SPEED:    target = s_tgt.speed_ref; break;
        case MODE_POSITION: target = g_spd_ref;       break;
        default:            target = 0.0f;            break;   /* TORQUE */
        }

        if (target >  s_prm.spd_limit) target =  s_prm.spd_limit;
        if (target < -s_prm.spd_limit) target = -s_prm.spd_limit;

        if (st == SYS_READY && fabsf(target) > 0.05f) {
            Sys_SetState(SYS_RUN);
        } else if (st == SYS_RUN && fabsf(target) < 0.05f) {
            Sys_SetState(SYS_READY);
        }

        /* ---- 6. 速度 PI ---- */
        if (s_tgt.mode == MODE_TORQUE) {
            s_pi.integral = 0.0f;            /* 切模式清积分，防突跳 */
            g_foc.iq_ref  = s_tgt.iq_ref;
            g_spd_ref     = 0.0f;
        } else {
            g_foc.iq_ref = PI_Update(&s_pi, target - omega_lpf,
                                     1.0f / (float)CFG_TASK_SPEED_HZ);
            g_spd_ref = target;
        }

        /* ---- 7. 推一拍给波形管道 ---- */
        int16_t iq_ma  = (int16_t)(g_foc.iq * 1000.0f);
        int16_t spd_dp = (int16_t)(omega_lpf * 10.0f);
        Wave_Push(iq_ma, spd_dp, (uint8_t)Sys_GetState(), Sys_GetFault());

        g_speed_loops++;
    }
}
