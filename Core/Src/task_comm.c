/*
 * task_comm.c  --  外部命令入口（P4，事件驱动）
 *
 * 它的唯一职责：**把外部世界说的话，翻译成 app_msg_t 投进 g_qCmd。**
 * 不做任何控制决策，不直接改任何控制变量。
 *
 * 命令有两个来源：
 *   1) CAN  —— 由 can_app.c 在中断里收帧，Task_Comm 轮询取出并转消息
 *   2) 串口 —— 单字符命令，本任务直接解析
 *
 * 为什么不让 CAN 中断直接投消息：
 *   中断里只允许做最轻的搬运（xQueueSendFromISR 到原始帧队列），
 *   解析 CRC、判断命令类型这些活留给任务做。
 *   中断里干活越多，电流环被推迟的概率越大。
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "task_metrics.h"
#include "can_app.h"

extern UART_HandleTypeDef huart1;

/* 串口命令 -> 消息 */
static void uart_to_msg(uint8_t ch)
{
    app_msg_t m = {0};
    static float s_spd = 0.0f;

    switch (ch) {
    case '+':
        s_spd += 10.0f;
        m.id = MSG_SET_SPEED; m.value = s_spd;
        printf("\r\n[UART] speed = %.1f rad/s (forward)\r\n", (double)s_spd);
        break;

    case '-':
        s_spd -= 10.0f;
        m.id = MSG_SET_SPEED; m.value = s_spd;
        printf("\r\n[UART] speed = %.1f rad/s (reverse)\r\n", (double)s_spd);
        break;

    case '0':
        s_spd = 0.0f;
        m.id = MSG_SET_SPEED; m.value = 0.0f;
        printf("\r\n[UART] speed = 0\r\n");
        break;

    case 'e': m.id = MSG_ENABLE;    m.value = 1.0f; break;
    case 'd': m.id = MSG_ENABLE;    m.value = 0.0f; break;
    case 'f': m.id = MSG_CLEAR_FAULT; break;
    case 'x': m.id = MSG_ESTOP;     break;

    case '?': m.id = MSG_GET_STATUS; break;

    case 'm': {
        /* 循环切模式：力矩 -> 速度 -> 位置 */
        static uint8_t mode = 0;
        mode = (uint8_t)((mode + 1u) % 3u);
        m.id = MSG_SET_MODE; m.value = (float)mode;
        break;
    }

    default:
        return;      /* 不认识的字符直接丢，不投消息 */
    }

    (void)xQueueSend(g_qCmd, &m, 0);
}

/* ------------------------------------------------------------------ */
/*
 * 按键扫描：板载 PA0(WKUP) / PA3(K2)，上拉输入。
 * 在 2 ms 的任务里每 10 拍（20 ms）采一次，就足够消抖了 ——
 * 机械按键抖动通常在 5~10 ms，20 ms 采样的等效就是"连续两次读到同一个值才算数"。
 *
 *   K1(PA0) 短按 → 目标转速 +10 rad/s（累加到上限）
 *   K2(PA3) 短按 → 反向（当前目标取负）
 */
static void key_scan(void)
{
    static uint8_t  div;
    static uint8_t  prev_k1 = 1, prev_k2 = 1;
    static float    spd;

    if (++div < 10u) return;
    div = 0;

    uint8_t k1 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 0u : 1u;
    uint8_t k2 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_RESET) ? 0u : 1u;

    /* 下降沿（1 -> 0）才算一次按下 */
    if (prev_k1 == 1u && k1 == 0u) {
        spd += 10.0f;
        if (spd > 100.0f) spd = 100.0f;

        app_msg_t m = { .id = MSG_SET_SPEED, .value = spd };
        (void)xQueueSend(g_qCmd, &m, 0);
        printf("\r\n[KEY] K1  speed = %.1f\r\n", (double)spd);
    }

    if (prev_k2 == 1u && k2 == 0u) {
        spd = -spd;                     /* 反向 */
        app_msg_t m = { .id = MSG_SET_SPEED, .value = spd };
        (void)xQueueSend(g_qCmd, &m, 0);
        printf("\r\n[KEY] K2  reverse -> %.1f\r\n", (double)spd);
    }

    prev_k1 = k1;
    prev_k2 = k2;
}

/* ------------------------------------------------------------------ */
void Task_Comm(void *arg)
{
    (void)arg;

    TickType_t last       = xTaskGetTickCount();
    uint32_t   last_cycle = Met_Now();
    const uint32_t period_cyc = CFG_SYSCLK_HZ / 500u;   /* 2 ms */

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(2));

        uint32_t now = Met_Now();
        Metrics_MarkWake(MET_COMM, last_cycle + period_cyc, now);
        last_cycle = now;

        /* ---- 1. CAN 原始帧 -> 消息（can_app 内部解析 CRC 和命令）---- */
        CAN_App_Process();

        /* ---- 2. 串口单字符命令，超时 0 = 不阻塞 ---- */
        uint8_t ch;
        if (HAL_UART_Receive(&huart1, &ch, 1, 0) == HAL_OK) {
            uart_to_msg(ch);
        }

        /* ---- 3. 板载按键 ---- */
        key_scan();
    }
}
