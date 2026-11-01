/*
 * app_tasks.h  --  任务总表 + 全部 IPC 对象
 *
 * =============== 任务拆分依据 ===============
 * 一条铁律：**按"响应时间要求"拆，不按"功能模块"拆。**
 *
 * 反面例子：把"通信"拆成 CAN 任务 + 串口任务。两者响应要求一样，
 *           拆开只是多一次上下文切换，没有任何收益。
 * 正面例子：把"控制"拆成速度环（1 kHz）和位置环（500 Hz）两个任务，
 *           因为响应要求差一倍，且需要不同优先级保证内环不被外环拖累。
 *
 * 本项目 7 个任务：
 *   Task_Fault     P7  事件驱动   故障上报 -> 锁存，最快响应
 *   Task_Speed     P6  1 ms       读编码器 + 速度 PI
 *   Task_Position  P5  2 ms       位置 P
 *   Task_Comm      P4  事件驱动   CAN / 串口 -> 命令队列
 *   Task_HMI       P3  33 ms      屏刷新（宁可掉帧，不能抢 CPU）
 *   Task_Monitor   P3  10 ms      母线 / 堵转 / 喂狗
 *   Task_Log       P1  10 ms      日志 + 波形落 Flash
 *
 * 优先级排序的唯一依据：**频率高的、不能被延迟的，优先级高。**
 * Task_Fault 放最高是因为它由中断唤醒、必须立刻响应；
 * Task_HMI 放低是因为掉一帧没人看得出来，但它抢 CPU 会直接表现成电机抖。
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

#include "app_msg.h"

/* ---------------- 优先级 ---------------- */
#define PRIO_FAULT      7
#define PRIO_SPEED      6
#define PRIO_POSITION   5
#define PRIO_COMM       4
#define PRIO_HMI        3
#define PRIO_MONITOR    3
#define PRIO_LOG        1

/* ---------------- 栈（单位 word）----------------
 * G474 有 128 KB RAM，比 F103 宽松，但 Cortex-M4F 的任务栈要额外
 * 保存 S0-S15 + FPSCR = 17 个字，所以栈别抠太狠。
 * heap 给 24 KB；这 7 个任务加起来约 2.3 K words ≈ 9.2 KB，够。
 */
#define STK_FAULT       224
#define STK_SPEED       320
#define STK_POSITION    256
#define STK_COMM        384
#define STK_HMI         512
#define STK_MONITOR     256
#define STK_LOG         384

/* ---------------- 事件位 ---------------- */
#define EVT_STATE_CHG   (1u << 0)   /* 系统状态迁移 */
#define EVT_PARAM_CHG   (1u << 1)   /* 参数被改，PI 需要重载 */
#define EVT_FAULT       (1u << 2)   /* 有故障待处理 */
#define EVT_WAVE_RDY    (1u << 3)   /* 一段波形攒满，可以刷屏 / 落盘 */
#define EVT_SAVE_REQ    (1u << 4)   /* 请求把参数存进 EEPROM */

/* ---------------- IPC 对象 ---------------- */
extern QueueHandle_t      g_qCmd;      /* app_msg_t，Comm -> 控制类任务 */
extern QueueHandle_t      g_qFault;    /* uint16_t 故障码，Fault -> Log/HMI */
extern EventGroupHandle_t g_evtGroup;  /* 全局事件组 */

/* ---------------- 任务句柄 ---------------- */
extern TaskHandle_t g_hFault;
extern TaskHandle_t g_hSpeed;
extern TaskHandle_t g_hPosition;
extern TaskHandle_t g_hComm;
extern TaskHandle_t g_hHMI;
extern TaskHandle_t g_hMonitor;
extern TaskHandle_t g_hLog;

void AppTasks_Create(void);

void Task_Fault(void *arg);
void Task_Speed(void *arg);
void Task_Position(void *arg);
void Task_Comm(void *arg);
void Task_HMI(void *arg);
void Task_Monitor(void *arg);
void Task_Log(void *arg);

#endif /* APP_TASKS_H */
