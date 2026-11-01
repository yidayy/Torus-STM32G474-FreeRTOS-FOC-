/*
 * stm32g4xx_hal_timebase_tim.c  --  用 TIM6 做 HAL 1ms 时基
 *
 * FreeRTOS 独占 SysTick，HAL 必须换一个定时器。
 * G4 有 TIM6/TIM7（F1 没有），选 TIM6 因为它是最纯的基本定时器，不占 PWM 资源。
 */
#include "main.h"

TIM_HandleTypeDef htim6;

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    RCC_ClkInitTypeDef clkconfig;
    uint32_t uwTimclock, uwPrescalerValue;
    uint32_t pFLatency;

    __HAL_RCC_TIM6_CLK_ENABLE();

    HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

    /* TIM6 挂 APB1；APB1 分频不为 1 时定时器时钟 = PCLK1 * 2 */
    uwTimclock = 2UL * HAL_RCC_GetPCLK1Freq();
    uwPrescalerValue = (uint32_t)((uwTimclock / 1000000U) - 1U);

    htim6.Instance          = TIM6;
    htim6.Init.Period       = 1000U - 1U;
    htim6.Init.Prescaler    = uwPrescalerValue;
    htim6.Init.ClockDivision = 0;
    htim6.Init.CounterMode  = TIM_COUNTERMODE_UP;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) return HAL_ERROR;

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    HAL_TIM_Base_Start_IT(&htim6);
    return HAL_OK;
}

void HAL_SuspendTick(void) { __HAL_TIM_DISABLE_IT(&htim6, TIM_IT_UPDATE); }
void HAL_ResumeTick(void)  { __HAL_TIM_ENABLE_IT(&htim6, TIM_IT_UPDATE); }

void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        HAL_IncTick();
    }
}
