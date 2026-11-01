/*
 * foc_core.h  --  电流环（硬实时层）
 *
 * 只在 TIM1 更新中断里被调用，频率 = PWM 频率 = 20 kHz。
 * 这个文件不允许出现任何 FreeRTOS API、不允许 printf、不允许阻塞。
 */
#ifndef FOC_CORE_H
#define FOC_CORE_H

#include <stdint.h>

/* 故障位 */
#define FOC_FAULT_NONE      0x0000u
#define FOC_FAULT_OC        0x0001u   /* 过流 */
#define FOC_FAULT_OV        0x0002u   /* 过压 */
#define FOC_FAULT_UV        0x0004u   /* 欠压 */
#define FOC_FAULT_STALL     0x0008u   /* 堵转 */
#define FOC_FAULT_ENC       0x0010u   /* 编码器失联 */
#define FOC_FAULT_DRV       0x0020u   /* DRV8313 nFAULT 拉低 */

typedef struct {
    /* 输入 */
    float   theta_e;        /* 电角度 rad */
    float   vbus;           /* 母线电压 V */
    float   ia, ib;         /* 相电流 A */

    /* 中间量（在线调试时最常看这几项）*/
    float   ialpha, ibeta;
    float   id, iq;
    float   ud, uq;

    /* 指令 */
    float   id_ref, iq_ref;
    float   current_limit;  /* A */

    /* 输出 */
    float   da, db, dc;     /* 0..1 */

    /* 状态 */
    uint8_t  enabled;
    uint16_t fault;
    uint32_t isr_count;

} foc_state_t;

extern volatile foc_state_t g_foc;

void FOC_Init(void);
void FOC_Enable(uint8_t en);
void FOC_ClearFault(void);

/* 在线改电流环增益：只改增益不清积分，避免参数变更瞬间产生力矩扰动 */
void FOC_SetCurrentGains(float kp, float ki);

/* 在 20 kHz ISR 里调用一次 */
void FOC_Step(void);

/* 简单 PI，带抗积分饱和 */
typedef struct {
    float kp;
    float ki;
    float integral;
    float out_max;
} pi_t;

void  PI_Reset(pi_t *p, float kp, float ki, float out_max);
float PI_Update(pi_t *p, float err, float dt);

#endif /* FOC_CORE_H */
