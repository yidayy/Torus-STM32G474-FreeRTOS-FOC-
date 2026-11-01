/*
 * stm32g4xx_it.c  --  中断向量
 *
 * 三个"必须这么写"的地方：
 *  1) TIM20 是高级定时器，电流环跑在 TIM20_UP 中断里，20 kHz，
 *     NVIC 优先级 2（低于 configMAX_SYSCALL=5），因为这里不调任何 FreeRTOS API。
 *  2) SysTick 归 FreeRTOS，HAL 时基在 TIM6（见 stm32g4xx_hal_timebase_tim.c）。
 *  3) FDCAN2 有两个中断线 IT0/IT1，RX FIFO0 新消息挂在 IT0 上。
 */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

#include "config.h"
#include "foc_core.h"
#include "current_sense.h"
#include "can_app.h"
#include "app_tasks.h"
#include "sys_state.h"

extern TIM_HandleTypeDef   htim20;
extern FDCAN_HandleTypeDef hfdcan2;
extern void xPortSysTickHandler(void);

void NMI_Handler(void)        { while (1) { } }
void HardFault_Handler(void)  { while (1) { } }
void MemManage_Handler(void)  { while (1) { } }
void BusFault_Handler(void)   { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
void DebugMon_Handler(void)   { }

void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    } else {
        HAL_IncTick();
    }
}

/* ==================================================================
 * TIM20 更新中断 —— 电流环 20 kHz
 *
 * 这里只能做"必须立刻做"的事：采样、算 FOC、改占空比。
 * 故障判定和上报一律丢给 P7 的 Task_Fault，通过任务通知唤醒。
 * 用任务通知而不是二值信号量，因为前者不需要额外的内核对象，
 * 且能用 eSetBits 模式直接把故障位带上去。
 * ================================================================== */
void TIM20_UP_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim20, TIM_FLAG_UPDATE) == RESET) return;
    __HAL_TIM_CLEAR_FLAG(&htim20, TIM_FLAG_UPDATE);

    float ia, ib;
    CS_Read(&ia, &ib);
    g_foc.ia = ia;
    g_foc.ib = ib;

    FOC_Step();

    const uint32_t p = CFG_PWM_PERIOD - 1u;
    TIM20->CCR1 = (uint32_t)(g_foc.da * (float)p);
    TIM20->CCR2 = (uint32_t)(g_foc.db * (float)p);
    TIM20->CCR3 = (uint32_t)(g_foc.dc * (float)p);

    /* 过流：只在"刚发生"的那一拍通知一次，不能 20 kHz 每拍都发，
       否则 Task_Fault 会被自己刷死，其他任务永远抢不到 CPU。 */
    static uint8_t s_oc_reported;
    if ((g_foc.fault & FOC_FAULT_OC) != 0u) {
        if (!s_oc_reported) {
            s_oc_reported = 1u;
            BaseType_t hpw = pdFALSE;
            if (g_hFault != NULL) {
                xTaskNotifyFromISR(g_hFault, FAULT_BIT_OC, eSetBits, &hpw);
                portYIELD_FROM_ISR(hpw);
            }
        }
    } else {
        s_oc_reported = 0u;
    }
}

/* ==================================================================
 * FDCAN2
 * ================================================================== */
void FDCAN2_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&hfdcan2);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    (void)RxFifo0ITs;
    FDCAN_RxHeaderTypeDef h;
    uint8_t data[8];

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &h, data) == HAL_OK) {
        CAN_App_OnRxFrame(data, (uint8_t)h.DataLength);
    }
}
