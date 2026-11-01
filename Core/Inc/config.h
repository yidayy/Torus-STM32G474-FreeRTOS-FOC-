/*
 * config.h  --  wushua_g474 全局参数总表
 *
 * 目标板：GenBotter ST-12 核心板（STM32G474VET6 / LQFP100）
 * 原理图：gitee.com/gaoyanzeng/drg_st_12_g474vet6  02原理图/ST-12核心板原理图V1.0.pdf
 *
 * ==================================================================
 * 这块板做 FOC 的最大障碍：PA9/PA10 被板载 CH340（USART1）占了
 * ==================================================================
 * TIM1_CH1/2/3 的常规位置正是 PA8/PA9/PA10，而 PA9/PA10 在这块板上
 * 接了 CH340K 做调试串口 —— 调试串口对 FOC 是刚需，不能让。
 *
 * 备选 PE9/PE11/PE13（TIM1_CH1/2/3 的另一组）被 FSMC 占用（接并口屏）。
 * 备选 PC6/PC7/PC8（TIM8）被 FSMC/SDIO 占用。
 *
 * ⇒ 唯一干净的方案：用 G4 专属的第三个高级定时器 **TIM20**
 *     TIM20_CH1 = PE2   （空闲，已引出）
 *     TIM20_CH2 = PE3   （空闲，已引出）
 *     TIM20_CH3 = PF2   （空闲，已引出）
 *     TIM20_CH1N/2N/3N = PE4/PE5/PE6（同样空闲，将来换分立半桥时直接用）
 *
 * TIM20 是高级定时器，带互补输出和死区，和 TIM1 同级。
 * 这是这块通用核心板能跑 FOC 的关键，也是它唯一「运气好」的地方。
 */
#ifndef CONFIG_H
#define CONFIG_H

/*
 * HOST_SIM：在 PC 上编译控制算法做仿真时定义它。
 * 这样 config.h 不去拉 stm32g4xx_hal.h，foc_core.c / trig.c 就能
 * 直接用 gcc 编出可执行文件，不用改一行算法代码。
 */
#ifdef HOST_SIM
  #include <stdint.h>
  #ifndef GPIO_PIN_0
    #define GPIO_PIN_0   0x0001u
    #define GPIO_PIN_1   0x0002u
    #define GPIO_PIN_2   0x0004u
    #define GPIO_PIN_3   0x0008u
    #define GPIO_PIN_4   0x0010u
    #define GPIO_PIN_8   0x0100u
    #define GPIO_PIN_9   0x0200u
    #define GPIO_PIN_10  0x0400u
    #define GPIO_PIN_12  0x1000u
    #define GPIO_PIN_13  0x2000u
    #define GPIO_PIN_14  0x4000u
    #define GPIO_PIN_15  0x8000u
    #define GPIOA  ((void*)0)
    #define GPIOB  ((void*)1)
    #define GPIOC  ((void*)2)
    #define GPIOE  ((void*)3)
    #define GPIOF  ((void*)4)
  #endif
#else
  #include "stm32g4xx_hal.h"
#endif

/* ==================================================================
 * 1. 引脚映射（ST-12 板）
 * ================================================================== */
#define PIN_PWM_A               GPIO_PIN_2      /* PE2  TIM20_CH1 -> DRV8313 IN1 */
#define PIN_PWM_B               GPIO_PIN_3      /* PE3  TIM20_CH2 -> DRV8313 IN2 */
#define PIN_PWM_C               GPIO_PIN_2      /* PF2  TIM20_CH3 -> DRV8313 IN3 */
#define GPIO_PWM_AB             GPIOE
#define GPIO_PWM_C              GPIOF

/* 互补下管（用 DRV8313 时不用；将来换分立半桥直接接这里）*/
#define PIN_PWM_AN              GPIO_PIN_4      /* PE4  TIM20_CH1N */
#define PIN_PWM_BN              GPIO_PIN_5      /* PE5  TIM20_CH2N */
#define PIN_PWM_CN              GPIO_PIN_6      /* PE6  TIM20_CH3N */

/* 电流采样：避开 PA0(WKUP按键)/PA3(K2按键) */
#define PIN_ADC_IA              GPIO_PIN_1      /* PA1  ADC1_IN2  A 相 */
#define PIN_ADC_IB              GPIO_PIN_2      /* PA2  ADC1_IN3  B 相 */
#define PIN_ADC_VBUS            GPIO_PIN_0      /* PB0  ADC1_IN15 母线 */

/* AS5600 走 I2C3（PC0/PC1），绕开板载 AT24C02 占用的 I2C1 */
#define PIN_I2C3_SCL            GPIO_PIN_0      /* PC0 */
#define PIN_I2C3_SDA            GPIO_PIN_1      /* PC1 */
#define GPIO_I2C3               GPIOC

/* 板载 TJA1050 -> FDCAN2 */
#define PIN_CAN_RX              GPIO_PIN_12     /* PB12 FDCAN2_RX */
#define PIN_CAN_TX              GPIO_PIN_13     /* PB13 FDCAN2_TX */

/* 板载 CH340K -> USART1 */
#define PIN_USART1_TX           GPIO_PIN_9      /* PA9  */
#define PIN_USART1_RX           GPIO_PIN_10     /* PA10 */

/* 板载 LED（低电平点亮）*/
#define PIN_LED_B               GPIO_PIN_0      /* PE0 蓝 */
#define PIN_LED_G               GPIO_PIN_1      /* PE1 绿 */
#define GPIO_LED                GPIOE

/* DRV8313 控制脚：ST-12 板没有，从排针飞线到自己焊的驱动板 */
#define PIN_DRV_EN              GPIO_PIN_15     /* PB15 nSLEEP */
#define PIN_DRV_FAULT           GPIO_PIN_14     /* PB14 nFAULT（开漏，上拉）*/
#define GPIO_DRV                GPIOB

/* ==================================================================
 * 2. 时基与调度
 * ================================================================== */
#define CFG_SYSCLK_HZ           170000000UL
#define CFG_PWM_FREQ_HZ         20000U
/* 中心对齐：ARR = 170M / (2 * 20k) - 1 */
#define CFG_PWM_PERIOD          (CFG_SYSCLK_HZ / (2UL * CFG_PWM_FREQ_HZ))
#define CFG_PWM_DEADTIME_NS     500U

#define CFG_TASK_SPEED_HZ       1000U
#define CFG_TASK_POS_HZ         500U
#define CFG_TASK_MON_MS         10U
#define CFG_TASK_LOG_HZ         100U

/* ==================================================================
 * 3. 电机参数（2804 云台电机 12N14P）
 * ================================================================== */
#define CFG_MOTOR_POLE_PAIRS    7
#define CFG_MOTOR_RS_OHM        6.5f
#define CFG_MOTOR_LS_UH         1200.0f
#define CFG_ENCODER_CPR         4096
#define CFG_ENCODER_DIR         (-1)

/* ==================================================================
 * 4. 电流采样（INA199A1，增益 50，5mR）
 * ================================================================== */
#define CFG_CS_VREF_V           1.65f
#define CFG_CS_GAIN             (50.0f)
#define CFG_CS_RSHUNT_OHM       (0.005f)
#define CFG_CS_AMPERE_PER_VOLT  (1.0f / (CFG_CS_GAIN * CFG_CS_RSHUNT_OHM))
#define CFG_ADC_VREF_V          3.3f
#define CFG_ADC_FULL_SCALE      4095.0f
#define CFG_VBUS_DIV            5.545f

/* ==================================================================
 * 5. 控制限幅
 * ================================================================== */
#define CFG_CURRENT_MAX_A       2.2f
#define CFG_VOLTAGE_LIMIT_V     11.0f
#define CFG_SPEED_MAX_RAD_S     150.0f
#define CFG_POS_TOL_RAD         0.005f

/* ==================================================================
 * PID 参数 —— 下面这组是 PC 闭环仿真的结果，不是拍脑袋
 * ==================================================================
 * 仿真条件：含 PWM 一拍延迟、12 bit 编码器量化、12 bit ADC 量化，
 *          电机按 2804（Rs=6.5Ω, Ls=1.2mH, Kt=0.0955, J=2e-4）
 *
 * 速度环（1 kHz）：
 *   按二阶系统设计，ωn=sqrt(Kt·Ki/J)，ζ=Kp/2·sqrt(Kt/(J·Ki))
 *   仿真扫出来的最佳平衡点 Kp=0.075, Ki=0.95：
 *     正转超调 2.9%，建立时间 90 ms，反转超调 1.2%，稳态 50.00 rad/s
 *
 *   ⚠️ 原值 Kp=0.02/Ki=0.2 的超调是 29.9%，反转 63.2%，不能用。
 *   ⚠️ 仿真最优 Kp=0.094/Ki=1.31（超调 0%）但实机有噪声和机械共振，
 *      建议上板从这个 0.075 起步，观察后再往上加。
 *
 * 电流环（20 kHz）：
 *   带宽 ωc = Kp/Ls。原值 0.35 对应 ωc=292 rad/s = 46 Hz，太低，
 *   电流跟不上指令，外环看到的对象带额外滞后。
 *   取 ωc=1 kHz（20 kHz 采样的安全区间）：Kp=1.2, Ki=6.5k
 */
#define CFG_PID_CURRENT_KP      1.2f
#define CFG_PID_CURRENT_KI      6500.0f
#define CFG_PID_SPEED_KP        0.075f
#define CFG_PID_SPEED_KI        0.95f
#define CFG_PID_POS_KP          12.0f

/* ==================================================================
 * 6. 保护阈值
 * ================================================================== */
#define CFG_PROT_OC_A           2.4f
#define CFG_PROT_OV_V           14.5f
#define CFG_PROT_UV_V           9.0f
#define CFG_PROT_STALL_MS       300U

/* ==================================================================
 * 7. 运行开关
 * ================================================================== */
#define CFG_ENC_SIMULATE        0
#define CFG_CS_SIMULATE         0
#define CFG_OPEN_LOOP_ONLY      0

/* ==================================================================
 * 8. 度量
 * ================================================================== */
#define CFG_METRICS_ENABLE      1
#define CFG_METRICS_REPORT_MS   1000U
#define CFG_JITTER_BUCKETS      256

#endif /* CONFIG_H */
