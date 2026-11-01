/*
 * task_fault.c  --  故障处理任务（优先级最高 P7）
 *
 * 为什么单独一个任务、还给最高优先级：
 *   电流环中断里能做的只有"立刻关 PWM"，剩下的判定（是过流还是堵转、
 *   要不要锁存、要不要通知上位机）都需要时间。这些活交给 P7 任务，
 *   它能在一进入中断返回后就抢到 CPU，比放在 P1 的日志任务里快 100 倍。
 *
 * 唤醒方式用**任务通知**而不是二值信号量：
 *   任务通知是 FreeRTOS 里最快的 IPC —— 不需要创建队列控制块，
 *   数据直接存在 TCB 里，省一次内存分配和一次临界区。
 *   而且可以用 eSetBits 模式一次带多个故障位上来。
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "task_metrics.h"

void Task_Fault(void *arg)
{
    (void)arg;

    uint32_t bits = 0;
    uint32_t last_cycle = Met_Now();

    for (;;) {
        /* 无限等待通知。xTaskNotifyWait 的第一个参数 0 = 进入时不清位，
           第二个 0xFFFFFFFF = 退出时清全部位。 */
        if (xTaskNotifyWait(0, 0xFFFFFFFFu, &bits, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint32_t now = Met_Now();
        Metrics_MarkWake(MET_FAULT, last_cycle, now);
        last_cycle = now;

        uint16_t fault = (uint16_t)bits;
        if (fault == 0u) continue;

        /* 锁存 + 状态机切入 FAULT（内部会立刻掐功率级）*/
        if (Sys_RaiseFault(fault)) {
            /* 只有新故障才上报，避免同一个故障刷屏 */
            (void)xQueueSend(g_qFault, &fault, 0);
            if (g_evtGroup != NULL) {
                xEventGroupSetBits(g_evtGroup, EVT_FAULT);
            }
            printf("\r\n[FAULT] 0x%04X  state=%s\r\n",
                   (unsigned)fault, Sys_StateName(Sys_GetState()));
        }
    }
}
