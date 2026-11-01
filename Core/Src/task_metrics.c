/*
 * task_metrics.c  --  调度度量实现
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "task_metrics.h"
#include "app_tasks.h"

#define MET_BUCKETS     64u         /* 0..63 us，超出的并入最后一桶 */

typedef struct {
    uint16_t hist[MET_BUCKETS];
    uint16_t max_us;
    uint32_t count;
} met_jitter_t;

static const char *const s_name[MET_COUNT] = {
    "fault", "speed", "position", "comm", "hmi", "monitor", "log"
};
static const uint16_t s_stk[MET_COUNT] = {
    STK_FAULT, STK_SPEED, STK_POSITION, STK_COMM, STK_HMI, STK_MONITOR, STK_LOG
};
static met_jitter_t s_jit[MET_COUNT];

/* CPU 占用统计的上一拍快照 */
static uint32_t s_snap_total;
static uint32_t s_snap_task[MET_COUNT];
static uint32_t s_snap_idle;
static uint8_t  s_have_snap;

/* ------------------------------------------------------------------ */
/* V10.3.1 没有 ulTaskGetRunTimeCounter（只有 idle 版），
   所以统一走 vTaskGetInfo 取 ulRunTimeCounter —— 这也是 vTaskGetRunTimeStats
   内部用的同一条路径，数值一致。 */
static uint32_t met_runtime(TaskHandle_t h)
{
    TaskStatus_t st;
    vTaskGetInfo(h, &st, pdFALSE, eInvalid);
    return st.ulRunTimeCounter;
}

void Metrics_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    memset(s_jit, 0, sizeof(s_jit));
    s_have_snap  = 0;
    s_snap_total = 0;
}

void wushua_rtos_runtime_stats_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t wushua_rtos_runtime_counter(void)
{
    return DWT->CYCCNT;
}

/* ------------------------------------------------------------------ */
void Metrics_MarkWake(uint8_t idx, uint32_t expected_cycle, uint32_t actual_cycle)
{
    if (idx >= MET_COUNT) return;

    int32_t d = (int32_t)(actual_cycle - expected_cycle);   /* 有符号，天然处理回绕 */
    if (d < 0) d = -d;

    uint32_t us = (uint32_t)d / (CFG_SYSCLK_HZ / 1000000U);
    if (us >= MET_BUCKETS) us = MET_BUCKETS - 1u;

    met_jitter_t *j = &s_jit[idx];
    if (j->hist[us] != 0xFFFFu) j->hist[us]++;
    if (us > j->max_us) j->max_us = (uint16_t)us;
    j->count++;
}

static uint16_t jit_percentile(const met_jitter_t *j, uint8_t pct)
{
    if (j->count == 0u) return 0;

    uint32_t target = (j->count / 100u) * pct;
    if (target == 0u) target = 1u;

    uint32_t acc = 0;
    for (uint32_t i = 0; i < MET_BUCKETS; i++) {
        acc += j->hist[i];
        if (acc >= target) return (uint16_t)i;
    }
    return MET_BUCKETS - 1u;
}

/* ------------------------------------------------------------------ */
void Metrics_Report(void)
{
    TaskHandle_t hs[MET_COUNT] = { g_hFault, g_hSpeed, g_hPosition,
                                   g_hComm, g_hHMI, g_hMonitor, g_hLog };

    uint32_t now = DWT->CYCCNT;

    if (!s_have_snap) {
        s_snap_total = now;
        for (uint8_t i = 0; i < MET_COUNT; i++) {
            s_snap_task[i] = (hs[i] != NULL) ? (uint32_t)met_runtime(hs[i]) : 0u;
        }
        TaskHandle_t hidle = xTaskGetIdleTaskHandle();
        s_snap_idle = (hidle != NULL) ? (uint32_t)met_runtime(hidle) : 0u;
        s_have_snap = 1;
        printf("\r\n[metrics] 采集窗口建立，下一秒开始出数\r\n");
        return;
    }

    uint32_t total = now - s_snap_total;
    if (total < 1000u) return;

    printf("\r\n%-9s %-4s %-6s %-6s %-7s %-6s %-6s %s\r\n",
           "task", "prio", "stack", "left", "cpu%", "j50us", "j99us", "jmax");
    printf("------------------------------------------------------------\r\n");

    uint32_t sum_permille = 0;

    for (uint8_t i = 0; i < MET_COUNT; i++) {
        if (hs[i] == NULL) continue;

        uint32_t used  = (uint32_t)met_runtime(hs[i]);
        uint32_t delta = used - s_snap_task[i];
        s_snap_task[i] = used;

        uint32_t permille = (uint32_t)(((uint64_t)delta * 1000u) / total);
        sum_permille += permille;

        UBaseType_t left  = uxTaskGetStackHighWaterMark(hs[i]);
        UBaseType_t alloc = (UBaseType_t)s_stk[i];

        printf("%-9s %-4u %-6u %-6u %-2u.%-3u  %-6u %-6u %u\r\n",
               s_name[i],
               (unsigned)uxTaskPriorityGet(hs[i]),
               (unsigned)alloc,
               (unsigned)left,
               (unsigned)(permille / 10u), (unsigned)((permille % 10u) * 100u),
               (unsigned)jit_percentile(&s_jit[i], 50),
               (unsigned)jit_percentile(&s_jit[i], 99),
               (unsigned)s_jit[i].max_us);
    }

    TaskHandle_t hidle = xTaskGetIdleTaskHandle();
    uint32_t idle_permille = 0;
    if (hidle != NULL) {
        uint32_t used  = (uint32_t)met_runtime(hidle);
        idle_permille = (uint32_t)(((uint64_t)(used - s_snap_idle) * 1000u) / total);
        s_snap_idle = used;
    }
    uint32_t cpu_permille = (sum_permille > 1000u) ? 1000u : sum_permille;

    printf("------------------------------------------------------------\r\n");
    printf("cpu %u.%u%%   idle %u.%u%%   heap free %u B   min free %u B\r\n",
           (unsigned)(cpu_permille / 10u), (unsigned)((cpu_permille % 10u) * 100u),
           (unsigned)(idle_permille / 10u), (unsigned)((idle_permille % 10u) * 100u),
           (unsigned)xPortGetFreeHeapSize(),
           (unsigned)xPortGetMinimumEverFreeHeapSize());

    s_snap_total = now;
}
