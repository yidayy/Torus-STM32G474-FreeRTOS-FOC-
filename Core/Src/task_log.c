/*
 * task_log.c  --  日志与统计（P1，最低）
 *
 * 优先级给到最低不是"不重要"，是"它可以等"。
 * 串口打印一段任务表要 1800 字符，@460800 约 39 ms ——
 * 这段时间里它必须随时能被控制环打断。
 * 放在 P1，它连 idle 任务都抢不过，绝不会拖累控制。
 */
#include <stdio.h>

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "task_metrics.h"
#include "foc_core.h"
#include "data_pipe.h"
#include "encoder.h"
#include "current_sense.h"

extern volatile uint32_t g_speed_loops;
extern volatile float g_omega_meas;
extern volatile float g_pos_mech;
extern volatile uint32_t g_pos_loops;

void Task_Log(void *arg)
{
    (void)arg;

    TickType_t last       = xTaskGetTickCount();
    uint32_t   last_cycle = Met_Now();
    const uint32_t period_cyc = CFG_SYSCLK_HZ / CFG_TASK_LOG_HZ;

    uint32_t div = 0;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000u / CFG_TASK_LOG_HZ));

        uint32_t now = Met_Now();
        Metrics_MarkWake(MET_LOG, last_cycle + period_cyc, now);
        last_cycle = now;

        /* 每 2 秒一次完整快照 */
        if (++div < (CFG_TASK_LOG_HZ * 2u)) continue;
        div = 0;

        printf("\r\n==== wushua_g474 | %s | fault=0x%04X ====\r\n",
               Sys_StateName(Sys_GetState()), Sys_GetFault());

        printf("vbus %.2f V   ia %.3f A   ib %.3f A   iq %.3f A\r\n",
               (double)g_foc.vbus, (double)g_foc.ia,
               (double)g_foc.ib, (double)g_foc.iq);

        printf("theta_e %.2f rad   omega %.2f rad/s   pos %.2f rad\r\n",
               (double)g_foc.theta_e, (double)g_omega_meas, (double)g_pos_mech);

        printf("isr %lu  spd_loop %lu  pos_loop %lu  wave %lu  enc_err %lu\r\n",
               (unsigned long)g_foc.isr_count,
               (unsigned long)g_speed_loops,
               (unsigned long)g_pos_loops,
               (unsigned long)Wave_GetSeq(),
               (unsigned long)Encoder_GetErrorCount());

        /* 波形采样率是否掉点：1 kHz 任务应每秒涨 1000 */
        static uint32_t last_wave_seq;
        uint32_t d = Wave_GetSeq() - last_wave_seq;
        last_wave_seq = Wave_GetSeq();
        printf("wave rate %lu Hz (应为 1000)\r\n", (unsigned long)(d / 2u));

        Metrics_Report();
    }
}
