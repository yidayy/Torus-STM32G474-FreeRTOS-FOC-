/*
 * task_metrics.h  --  调度度量
 *
 * 简历上"任务抖动 P99 4.2 us""CPU 占用 51%"这类数字只能来自这里。
 * 没有度量就没有数字，没有数字的项目在面试里等于没做。
 *
 * 三个度量：
 *   1) 栈高水位   uxTaskGetStackHighWaterMark
 *   2) CPU 占用   vTaskGetRunTimeStats（DWT->CYCCNT 做时基）
 *   3) 唤醒抖动   自建 64 桶直方图，单位 us
 */
#ifndef TASK_METRICS_H
#define TASK_METRICS_H

#include <stdint.h>
#include "config.h"

#define MET_FAULT      0
#define MET_SPEED      1
#define MET_POSITION   2
#define MET_COMM       3
#define MET_HMI        4
#define MET_MONITOR    5
#define MET_LOG        6
#define MET_COUNT      7

void Metrics_Init(void);

/* 任务每次被唤醒时调用：报告"这一拍实际比期望晚了多少" */
void Metrics_MarkWake(uint8_t idx, uint32_t expected_cycle, uint32_t actual_cycle);

/* 每秒在 Task_Log 里调一次，串口打印任务表 */
void Metrics_Report(void);

/* 给 FreeRTOSConfig.h 用的 runtime counter 回调 */
void     wushua_rtos_runtime_stats_init(void);
uint32_t wushua_rtos_runtime_counter(void);

/* 当前 DWT 周期计数 */
static inline uint32_t Met_Now(void)
{
    return DWT->CYCCNT;
}

#endif /* TASK_METRICS_H */
