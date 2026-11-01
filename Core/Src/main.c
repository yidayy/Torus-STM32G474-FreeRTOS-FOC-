/*
 * main.c  --  wushua_g474  FOC 伺服节点
 *              STM32G474VET6 @ GenBotter ST-12 核心板
 *
 * 三相 PWM 走 TIM20（不是 TIM1）：
 *   TIM1_CH1/2/3 的常规引脚 PA8/PA9/PA10 里，PA9/PA10 被板载 CH340 占了，
 *   备选的 PE9/PE11/PE13 又被 FSMC 占了。TIM20 的 PE2/PE3/PF2 全空闲。
 *   TIM20 和 TIM1 同样是高级定时器，有互补输出和硬件死区。
 */
#include <stdio.h>

#include "main.h"
#include "config.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_tasks.h"
#include "foc_core.h"
#include "encoder.h"
#include "current_sense.h"
#include "can_app.h"
#include "trig.h"
#include "trace.h"

TIM_HandleTypeDef   htim20;
ADC_HandleTypeDef   hadc1;
I2C_HandleTypeDef   hi2c3;
UART_HandleTypeDef  huart1;
FDCAN_HandleTypeDef hfdcan2;
IWDG_HandleTypeDef  hiwdg;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM20_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_FDCAN2_Init(void);
static void MX_IWDG_Init(void);

/* ------------------------------------------------------------------ */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_TIM20_Init();
    MX_ADC1_Init();
    MX_I2C3_Init();
    MX_USART1_UART_Init();
    MX_FDCAN2_Init();
    MX_IWDG_Init();

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\r\n================================\r\n");
    printf(" wushua_g474  FOC servo node\r\n");
    printf(" ST-12 / STM32G474VET6 @170MHz\r\n");
    printf("==============================\r\n");

    CS_Init();
    printf("[CAL ] offset A=%u B=%u\r\n", CS_GetOffsetA(), CS_GetOffsetB());
    Encoder_Init();
    CAN_App_Init();
    Trace_Init();

    AppTasks_Create();

    /* CORDIC 自检：证明硬件真的在算，而不是默默退回了查表 */
    printf("[CORD] sin/cos max err vs LUT = %.7f\r\n", (double)Trig_SelfTest());
    printf("[CORD] one call takes %lu cycles\r\n",
           (unsigned long)Trig_GetLastCycles());

    HAL_TIM_PWM_Start(&htim20, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim20, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim20, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim20);          /* 高级定时器必须显式开 MOE */
    __HAL_TIM_ENABLE_IT(&htim20, TIM_IT_UPDATE);

    HAL_GPIO_WritePin(GPIO_DRV, PIN_DRV_EN, GPIO_PIN_RESET);

    printf("[RTOS] start scheduler\r\n");
    vTaskStartScheduler();

    Error_Handler();
    while (1) { }
}

/* ------------------------------------------------------------------ */
/* HSE 8MHz / M=2 -> 4MHz ; xN=85 -> 340MHz ; /R=2 -> 170MHz */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState            = RCC_HSE_ON;
    osc.HSIState            = RCC_HSI_ON;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM            = RCC_PLLM_DIV2;
    osc.PLL.PLLN            = 85;
    osc.PLL.PLLP            = RCC_PLLP_DIV2;
    osc.PLL.PLLQ            = RCC_PLLQ_DIV2;
    osc.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;      /* PCLK1 = 170MHz，FDCAN 可用满 */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

/* ------------------------------------------------------------------ */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* TIM20 三相 PWM：PE2 / PE3 / PF2 */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF3_TIM20;

    g.Pin = PIN_PWM_A | PIN_PWM_B;
    HAL_GPIO_Init(GPIO_PWM_AB, &g);
    g.Pin = PIN_PWM_C;
    HAL_GPIO_Init(GPIO_PWM_C, &g);

    /* 电流采样 PA1 / PA2、母线 PB0 -> 模拟 */
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    g.Pin  = PIN_ADC_IA | PIN_ADC_IB;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = PIN_ADC_VBUS;
    HAL_GPIO_Init(GPIOB, &g);

    /* 板载按键：PA0(WKUP) / PA3(K2)，上拉输入，按下读到低
       ⚠️ PA0 是按键不是 ADC，所以电流采样只能从 PA1 开始 */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin  = GPIO_PIN_0 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &g);

    /* I2C3 必须开漏 */
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF8_I2C3;
    g.Pin       = PIN_I2C3_SCL | PIN_I2C3_SDA;
    HAL_GPIO_Init(GPIO_I2C3, &g);

    /* USART1 */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    g.Pin       = PIN_USART1_TX;
    HAL_GPIO_Init(GPIOA, &g);
    g.Mode = GPIO_MODE_AF_PP;
    g.Pin  = PIN_USART1_RX;
    HAL_GPIO_Init(GPIOA, &g);

    /* FDCAN2 */
    g.Alternate = GPIO_AF9_FDCAN2;
    g.Pin       = PIN_CAN_RX | PIN_CAN_TX;
    HAL_GPIO_Init(GPIOB, &g);

    /* 板载 LED：PE0 蓝 / PE1 绿，低电平点亮 */
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin   = PIN_LED_B | PIN_LED_G;
    HAL_GPIO_Init(GPIO_LED, &g);
    HAL_GPIO_WritePin(GPIO_LED, PIN_LED_B | PIN_LED_G, GPIO_PIN_SET);

    /* DRV8313：PB14 nFAULT 输入上拉，PB15 nSLEEP 输出 */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin  = PIN_DRV_FAULT;
    HAL_GPIO_Init(GPIO_DRV, &g);

    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Pin  = PIN_DRV_EN;
    HAL_GPIO_Init(GPIO_DRV, &g);
    HAL_GPIO_WritePin(GPIO_DRV, PIN_DRV_EN, GPIO_PIN_RESET);
}

/* ------------------------------------------------------------------ */
static void MX_TIM20_Init(void)
{
    TIM_ClockConfigTypeDef  clk = {0};
    TIM_MasterConfigTypeDef mst = {0};
    TIM_OC_InitTypeDef      oc  = {0};

    __HAL_RCC_TIM20_CLK_ENABLE();

    htim20.Instance               = TIM20;
    htim20.Init.Prescaler         = 0;
    htim20.Init.CounterMode       = TIM_COUNTERMODE_CENTERALIGNED1;
    htim20.Init.Period            = CFG_PWM_PERIOD - 1;
    htim20.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim20.Init.RepetitionCounter = 0;
    htim20.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim20) != HAL_OK) Error_Handler();

    clk.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim20, &clk) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_Init(&htim20) != HAL_OK) Error_Handler();

    mst.MasterOutputTrigger = TIM_TRGO_UPDATE;
    mst.MasterOutputTrigger2 = TIM_TRGO2_RESET;
    mst.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim20, &mst) != HAL_OK) Error_Handler();

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = CFG_PWM_PERIOD / 2;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    if (HAL_TIM_PWM_ConfigChannel(&htim20, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_ConfigChannel(&htim20, &oc, TIM_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_ConfigChannel(&htim20, &oc, TIM_CHANNEL_3) != HAL_OK) Error_Handler();

    /* 死区 500ns @170MHz = 85 个时钟 */
    HAL_TIMEx_ConfigBreakDeadTime(&htim20, &(TIM_BreakDeadTimeConfigTypeDef){
        .OffStateRunMode  = TIM_OSSR_DISABLE,
        .OffStateIDLEMode = TIM_OSSI_DISABLE,
        .LockLevel        = TIM_LOCKLEVEL_OFF,
        .DeadTime         = 85,
        .BreakState       = TIM_BREAK_ENABLE,
        .BreakPolarity    = TIM_BREAKPOLARITY_HIGH,
        .BreakFilter      = 0,
        .AutomaticOutput  = TIM_AUTOMATICOUTPUT_ENABLE,
    });

    HAL_NVIC_SetPriority(TIM20_UP_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM20_UP_IRQn);
}

/* ------------------------------------------------------------------ */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef  ch  = {0};
    ADC_InjectionConfTypeDef inj = {0};

    __HAL_RCC_ADC12_CLK_ENABLE();

    hadc1.Instance                      = ADC1;
    hadc1.Init.ClockPrescaler           = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution               = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign                = ADC_DATAALIGN_RIGHT;
    hadc1.Init.GainCompensation         = 0;
    hadc1.Init.ScanConvMode             = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection             = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait         = DISABLE;
    hadc1.Init.ContinuousConvMode       = DISABLE;
    hadc1.Init.NbrOfConversion          = 1;
    hadc1.Init.DiscontinuousConvMode    = DISABLE;
    hadc1.Init.ExternalTrigConv         = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge     = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests    = DISABLE;
    hadc1.Init.Overrun                  = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode         = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

    /* G4 的校准分单端/差分两步 */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) Error_Handler();

    /* 规则组：母线电压（慢速读）*/
    ch.Channel      = ADC_CHANNEL_15;      /* PB0 */
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_92CYCLES_5;
    ch.SingleDiff   = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    ch.Offset       = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) Error_Handler();

    /* 注入组：IA(PA1) / IB(PA2)，20kHz 电流环专用 */
    inj.InjectedNbrOfConversion = 2;
    inj.InjectedSamplingTime    = ADC_SAMPLETIME_12CYCLES_5;
    inj.InjectedSingleDiff      = ADC_SINGLE_ENDED;
    inj.InjectedOffsetNumber    = ADC_OFFSET_NONE;
    inj.InjectedOffset          = 0;
    inj.ExternalTrigInjecConv   = ADC_EXTERNALTRIGINJEC_T20_TRGO;   /* TIM20 TRGO */
    inj.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_RISING;
    inj.AutoInjectedConv        = DISABLE;
    inj.QueueInjectedContext    = DISABLE;
    inj.InjectedDiscontinuousConvMode = DISABLE;

    inj.InjectedChannel = ADC_CHANNEL_2;   /* PA1 */
    inj.InjectedRank    = ADC_INJECTED_RANK_1;
    if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &inj) != HAL_OK) Error_Handler();

    inj.InjectedChannel = ADC_CHANNEL_3;   /* PA2 */
    inj.InjectedRank    = ADC_INJECTED_RANK_2;
    if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &inj) != HAL_OK) Error_Handler();

    /* 注入组由 TIM20 TRGO 触发：PWM 中心对齐点采样，避开开关噪声 */
}

/* ------------------------------------------------------------------ */
static void MX_I2C3_Init(void)
{
    hi2c3.Instance              = I2C3;
    hi2c3.Init.Timing           = 0x10707DBE;   /* 400kHz @170MHz，CubeMX 算出来的 */
    hi2c3.Init.OwnAddress1      = 0;
    hi2c3.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c3.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c3.Init.OwnAddress2      = 0;
    hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c3.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c3.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c3) != HAL_OK) Error_Handler();

    HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0);
}

/* ------------------------------------------------------------------ */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 921600;   /* VOFA+ 波形上传需要更宽的带宽 */
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

/* ------------------------------------------------------------------ */
/* 500 kbps @170MHz：Prescaler 20 -> 8.5MHz，17 tq/bit
   SyncSeg 1 + TSeg1 13 + TSeg2 3 = 17，采样点 82% */
static void MX_FDCAN2_Init(void)
{
    hfdcan2.Instance                  = FDCAN2;
    hfdcan2.Init.ClockDivider         = FDCAN_CLOCK_DIV1;
    hfdcan2.Init.FrameFormat          = FDCAN_FRAME_CLASSIC;
    hfdcan2.Init.Mode                 = FDCAN_MODE_NORMAL;
    hfdcan2.Init.AutoRetransmission   = ENABLE;
    hfdcan2.Init.TransmitPause        = DISABLE;
    hfdcan2.Init.ProtocolException    = DISABLE;
    hfdcan2.Init.NominalPrescaler     = 20;
    hfdcan2.Init.NominalSyncJumpWidth = 3;
    hfdcan2.Init.NominalTimeSeg1      = 13;
    hfdcan2.Init.NominalTimeSeg2      = 3;
    hfdcan2.Init.DataPrescaler        = 1;
    hfdcan2.Init.DataSyncJumpWidth    = 1;
    hfdcan2.Init.DataTimeSeg1         = 1;
    hfdcan2.Init.DataTimeSeg2         = 1;
    hfdcan2.Init.StdFiltersNbr        = 1;
    hfdcan2.Init.ExtFiltersNbr        = 0;
    hfdcan2.Init.TxFifoQueueMode      = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(&hfdcan2) != HAL_OK) Error_Handler();

    FDCAN_FilterTypeDef f = {0};
    f.IdType       = FDCAN_STANDARD_ID;
    f.FilterIndex  = 0;
    f.FilterType   = FDCAN_FILTER_MASK;
    f.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    f.FilterID1    = CAN_ID_CMD;
    f.FilterID2    = 0x7FF;
    if (HAL_FDCAN_ConfigFilter(&hfdcan2, &f) != HAL_OK) Error_Handler();

    HAL_FDCAN_ConfigGlobalFilter(&hfdcan2, FDCAN_REJECT, FDCAN_REJECT,
                                 FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
    HAL_FDCAN_Start(&hfdcan2);

    HAL_NVIC_SetPriority(FDCAN2_IT0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(FDCAN2_IT0_IRQn);
}

/* ------------------------------------------------------------------ */
static void MX_IWDG_Init(void)
{
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload    = 625;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) Error_Handler();
}

/* ------------------------------------------------------------------ */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    __disable_irq();
    for (;;) HAL_GPIO_TogglePin(GPIO_LED, PIN_LED_B);
}

void vApplicationMallocFailedHook(void)
{
    __disable_irq();
    for (;;) HAL_GPIO_TogglePin(GPIO_LED, PIN_LED_G);
}

volatile uint32_t g_idle_count;

void vApplicationIdleHook(void)
{
    g_idle_count++;
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIO_LED, PIN_LED_B | PIN_LED_G);
        for (volatile uint32_t i = 0; i < 2000000u; i++) { }
    }
}
