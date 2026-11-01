/*
 * task_position.c  --  位置环（P5，500 Hz）
 *
 * 只做一件事：位置误差 -> 速度指令。
 * 没有积分项 —— 位置环用 P 就够了，加了 I 反而在换向时产生过冲，
 * 因为速度环本身已经把稳态误差吃掉了。
 *
 * 它和 Task_Speed 的耦合只有一个变量 g_pos_ref（单向读），
 * 以及写回 g_spd_ref。单写单读的 32 位 float 在 Cortex-M4 上是原子操作，
 * 不需要加锁。
 */
#include <math.h>

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "task_metrics.h"
#include "foc_math.h"
#include "encoder.h"

#define POS_PERIOD_MS   (1000u / CFG_TASK_POS_HZ)

extern volatile float g_pos_ref;
extern volatile float g_spd_ref;

volatile float g_pos_mech  = 0.0f;
volatile float g_pos_error = 0.0f;
volatile uint32_t g_pos_loops = 0;

void Task_Position(void *arg)
{
    (void)arg;

    TickType_t last       = xTaskGetTickCount();
    uint32_t   last_cycle = Met_Now();
    const uint32_t period_cyc = CFG_SYSCLK_HZ / CFG_TASK_POS_HZ;

    /* 机械角解卷绕：编码器只有 0..2pi，位置环要的是连续角度 */
    float accum = Encoder_GetMechAngle();
    float prev  = accum;
    float kp_pos = CFG_PID_POS_KP;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(POS_PERIOD_MS));

        uint32_t now = Met_Now();
        Metrics_MarkWake(MET_POSITION, last_cycle + period_cyc, now);
        last_cycle = now;

        /* 参数变更时重读 Kp（位置环只有一个参数，直接取）*/
        if ((xEventGroupGetBits(g_evtGroup) & EVT_PARAM_CHG) != 0) {
            kp_pos = Sys_GetParamItem(PARAM_KP_POS);
        }

        float cur = Encoder_GetMechAngle();
        accum += fm_angle_diff(cur, prev);
        prev = cur;
        g_pos_mech = accum;

        /* 只有在位置模式下才输出速度指令，否则把目标清零，
           避免从位置模式切走时速度环还拿着一个旧指令 */
        if (Sys_GetState() == SYS_RUN || Sys_GetState() == SYS_READY) {
            float err = g_pos_ref - accum;
            g_pos_error = err;

            float cmd = kp_pos * err;

            /* 限速：位置环不能把速度环的指令甩到天上 */
            if (cmd >  CFG_SPEED_MAX_RAD_S) cmd =  CFG_SPEED_MAX_RAD_S;
            if (cmd < -CFG_SPEED_MAX_RAD_S) cmd = -CFG_SPEED_MAX_RAD_S;

            g_spd_ref = cmd;
        } else {
            g_pos_error = 0.0f;
            g_spd_ref   = 0.0f;
        }

        g_pos_loops++;
    }
}
