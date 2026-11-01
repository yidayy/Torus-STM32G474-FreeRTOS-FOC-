/*
 * app_tasks.c  --  创建全部 IPC 对象和任务
 *
 * 顺序很重要：**先建 IPC，再建任务**。
 * 反过来的话，第一个跑起来的任务拿到的是 NULL 句柄，
 * 后面 xQueueSend 直接触发 configASSERT 挂死。
 */
#include <string.h>

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "data_pipe.h"
#include "task_metrics.h"
#include "foc_core.h"

QueueHandle_t      g_qCmd;
QueueHandle_t      g_qFault;
EventGroupHandle_t g_evtGroup;

TaskHandle_t g_hFault;
TaskHandle_t g_hSpeed;
TaskHandle_t g_hPosition;
TaskHandle_t g_hComm;
TaskHandle_t g_hHMI;
TaskHandle_t g_hMonitor;
TaskHandle_t g_hLog;

void AppTasks_Create(void)
{
    /* ---- 1. IPC 对象 ---- */
    g_qCmd     = xQueueCreate(8, sizeof(app_msg_t));
    g_qFault   = xQueueCreate(4, sizeof(uint16_t));
    g_evtGroup = xEventGroupCreate();

    configASSERT(g_qCmd != NULL);
    configASSERT(g_qFault != NULL);
    configASSERT(g_evtGroup != NULL);

    /* ---- 2. 子系统 ---- */
    Sys_Init();          /* 内含参数互斥量创建 */
    Wave_Init();
    Metrics_Init();
    FOC_Init();          /* 内含 CORDIC 初始化 */

    Sys_ApplyParamToControllers();
    Sys_SetState(SYS_IDLE);

    /* ---- 3. 任务 ---- */
    xTaskCreate(Task_Fault,    "fault",    STK_FAULT,    NULL, PRIO_FAULT,    &g_hFault);
    xTaskCreate(Task_Speed,    "speed",    STK_SPEED,    NULL, PRIO_SPEED,    &g_hSpeed);
    xTaskCreate(Task_Position, "position", STK_POSITION, NULL, PRIO_POSITION, &g_hPosition);
    xTaskCreate(Task_Comm,     "comm",     STK_COMM,     NULL, PRIO_COMM,     &g_hComm);
    xTaskCreate(Task_HMI,      "hmi",      STK_HMI,      NULL, PRIO_HMI,      &g_hHMI);
    xTaskCreate(Task_Monitor,  "monitor",  STK_MONITOR,  NULL, PRIO_MONITOR,  &g_hMonitor);
    xTaskCreate(Task_Log,      "log",      STK_LOG,      NULL, PRIO_LOG,      &g_hLog);

    configASSERT(g_hFault && g_hSpeed && g_hPosition && g_hComm &&
                 g_hHMI && g_hMonitor && g_hLog);
}
