/*
 * task_hmi.c  --  人机界面 + 波形上传（P3，33 ms）
 *
 * 干两件事：
 *   1) 把波形管道里攒的 33 个点打包成 VOFA+ 的 JustFloat 帧发出去
 *      —— 这就是视频里那个"上位机实时波形"的效果
 *   2) 每 10 帧（约 330 ms）在串口打一次 ASCII 波形
 *      —— 没接电脑跑 VOFA+ 时的兜底，一眼能看出电机在不在转
 *
 * 为什么优先级只有 3：
 *   掉一帧没人看得出来，但它一旦抢 CPU，表现就是电机抖。
 *   宁可波形掉帧，不能控制环抖动。
 *
 * ⚠️ 以后接 FSMC 屏，就把 draw_wave() 换成屏的写点函数，
 *    上面这层逻辑（读管道 + 打包 + 推送）一行不用改。
 */
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "task_metrics.h"
#include "data_pipe.h"
#include "foc_core.h"
#include "trace.h"

#define HMI_PERIOD_MS   33u
#define HMI_COLS        48u      /* ASCII 波形画 48 列 */
#define ASCII_EVERY     10u      /* 每 10 帧打一次 ASCII */

extern volatile float g_spd_ref;

static wave_pt_t s_snap[HMI_COLS];
static char      s_line[HMI_COLS + 1];

/* -range..+range 映射成一个字符；负值用小写字母，一眼看出反转 */
static char level_char(int16_t v, int16_t range)
{
    static const char ramp[] = " .:-=+*#%@";
    static const char neg[]  = "abcdefghi";
    if (range <= 0) return ' ';

    int32_t x = (int32_t)v * 9 / range;
    if (x >  9) x =  9;
    if (x < -9) x = -9;

    if (x < 0) {
        int32_t i = (-x) - 1;
        if (i > 8) i = 8;
        return neg[i];
    }
    return ramp[x];
}

static void draw_wave(int16_t *vals, uint16_t n, int16_t range, const char *label)
{
    for (uint16_t i = 0; i < n; i++) s_line[i] = level_char(vals[i], range);
    s_line[n] = '\0';
    printf("%-4s |%s|\r\n", label, s_line);
}

/* ------------------------------------------------------------------ */
void Task_HMI(void *arg)
{
    (void)arg;

    TickType_t last       = xTaskGetTickCount();
    uint32_t   last_cycle = Met_Now();
    const uint32_t period_cyc = (CFG_SYSCLK_HZ / 1000u) * HMI_PERIOD_MS;

    int16_t spd_vals[HMI_COLS];
    int16_t iq_vals[HMI_COLS];

    uint32_t frame = 0;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(HMI_PERIOD_MS));

        uint32_t now = Met_Now();
        Metrics_MarkWake(MET_HMI, last_cycle + period_cyc, now);
        last_cycle = now;

        uint16_t n = Wave_ReadLatest(s_snap, HMI_PERIOD_MS);
        if (n == 0) continue;

        /* ---- 1. 逐点推给 VOFA+（每点 1 ms，正好补上 33 ms 的空窗）---- */
        for (uint16_t i = 0; i < n; i++) {
            Trace_Push((float)s_snap[i].iq_ma / 1000.0f,
                       (float)s_snap[i].spd_dps / 10.0f,
                       g_spd_ref);
        }
        Trace_Flush();

        /* ---- 2. 每 10 帧打一次 ASCII 波形（兜底）---- */
        if ((frame++ % ASCII_EVERY) != 0u) continue;

        uint16_t m = Wave_ReadLatest(s_snap, HMI_COLS);
        if (m == 0) continue;

        for (uint16_t i = 0; i < m; i++) {
            spd_vals[i] = s_snap[i].spd_dps;
            iq_vals[i]  = s_snap[i].iq_ma;
        }

        printf("\r\n---- %s  fault=0x%04X  drop=%lu ----\r\n",
               Sys_StateName((sys_state_t)s_snap[m - 1].state),
               Sys_GetFault(), (unsigned long)Trace_GetDropCount());

        int16_t spd_peak = 10, iq_peak = 100;
        for (uint16_t i = 0; i < m; i++) {
            int16_t a = spd_vals[i] < 0 ? (int16_t)-spd_vals[i] : spd_vals[i];
            if (a > spd_peak) spd_peak = a;
            a = iq_vals[i] < 0 ? (int16_t)-iq_vals[i] : iq_vals[i];
            if (a > iq_peak) iq_peak = a;
        }

        draw_wave(spd_vals, m, spd_peak, "spd");
        draw_wave(iq_vals,  m, iq_peak,  "iq");

        printf("     spd 峰值 %.1f rad/s   iq 峰值 %.2f A\r\n",
               (double)spd_peak / 10.0, (double)iq_peak / 1000.0);
    }
}
