/*
 * task_monitor.c  --  系统监控（P3，10 ms）
 *
 * 只做判定，不做动作。发现异常就调 Sys_RaiseFault()，
 * 由状态机去关功率级、由 Task_Fault 去上报。
 *
 * 为什么"判定"和"动作"要分开：
 *   如果监控任务直接关 PWM，那它就得知道功率级的细节；
 *   以后换驱动芯片，这段代码要跟着改。
 *   现在它只知道"报一个故障码"，换硬件不用动这里。
 */
#include <math.h>

#include "config.h"
#include "app_tasks.h"
#include "sys_state.h"
#include "task_metrics.h"
#include "foc_core.h"
#include "encoder.h"
#include "current_sense.h"

extern IWDG_HandleTypeDef hiwdg;

extern volatile float g_omega_meas;

/* 堵转判定：使能状态 + 电流到顶 + 几乎不转，持续 CFG_PROT_STALL_MS */
static uint16_t s_stall_cnt;
static uint32_t s_led_tick;

void Task_Monitor(void *arg)
{
    (void)arg;

    TickType_t last       = xTaskGetTickCount();
    uint32_t   last_cycle = Met_Now();
    const uint32_t period_cyc = (CFG_SYSCLK_HZ / 1000u) * CFG_TASK_MON_MS;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CFG_TASK_MON_MS));

        uint32_t now = Met_Now();
        Metrics_MarkWake(MET_MONITOR, last_cycle + period_cyc, now);
        last_cycle = now;

        /* ---- 1. 母线电压 ---- */
        float vbus = CS_ReadVbus();
        if (vbus > 1.0f) g_foc.vbus = vbus;

        if (vbus > CFG_PROT_OV_V)              Sys_RaiseFault(FAULT_BIT_OV);
        if (vbus < CFG_PROT_UV_V && vbus > 1.0f) Sys_RaiseFault(FAULT_BIT_UV);

        /* ---- 2. DRV8313 nFAULT（开漏，低有效）---- */
        if (HAL_GPIO_ReadPin(GPIO_DRV, PIN_DRV_FAULT) == GPIO_PIN_RESET) {
            Sys_RaiseFault(FAULT_BIT_DRV);
        }

        /* ---- 3. 编码器失联：1 秒内错误超过 100 次 ---- */
        static uint32_t last_enc_err;
        uint32_t enc_err = Encoder_GetErrorCount();
        if (enc_err - last_enc_err > 100u) Sys_RaiseFault(FAULT_BIT_ENC);
        last_enc_err = enc_err;

        /* ---- 4. 堵转 ---- */
        if (Sys_GetState() == SYS_RUN &&
            (fabsf(g_foc.iq) > CFG_CURRENT_MAX_A * 0.9f) &&
            (fabsf(g_omega_meas) < 2.0f)) {

            if (++s_stall_cnt >= (CFG_PROT_STALL_MS / CFG_TASK_MON_MS)) {
                s_stall_cnt = 0;
                Sys_RaiseFault(FAULT_BIT_STALL);
            }
        } else {
            s_stall_cnt = 0;
        }

        /* ---- 5. 指示灯：故障红闪 / 运行绿常亮 / 空闲慢闪 ---- */
        s_led_tick += CFG_TASK_MON_MS;
        sys_state_t st = Sys_GetState();

        if (st == SYS_FAULT) {
            /* 故障：蓝灯 100 ms 闪，肉眼一眼能认出来 */
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_B,
                              ((s_led_tick / 100u) & 1u) ? GPIO_PIN_RESET : GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_G, GPIO_PIN_SET);
        } else if (st == SYS_RUN) {
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_G, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_B, GPIO_PIN_SET);
        } else if (st == SYS_READY) {
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_G,
                              ((s_led_tick / 500u) & 1u) ? GPIO_PIN_RESET : GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_B, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_G, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIO_LED, PIN_LED_B,
                              ((s_led_tick / 1000u) & 1u) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }

        /* ---- 6. 喂狗：能走到这里说明调度器还在转 ---- */
        HAL_IWDG_Refresh(&hiwdg);
    }
}
