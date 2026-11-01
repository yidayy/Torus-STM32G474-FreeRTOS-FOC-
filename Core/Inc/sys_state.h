/*
 * sys_state.h  --  系统状态机 + 可在线修改的参数表
 *
 * 状态机是整个项目的"骨架"。没有它，任务之间就靠一堆 bool 互相猜，
 * 上电顺序、使能流程、故障清除都会变成玄学。
 *
 *   INIT ──自检通过──> IDLE ──使能──> READY ──目标非零──> RUN
 *    ↑                  ↑              ↑                 │
 *    │                  │              └──目标归零────────┘
 *    │                  │
 *    └──────清故障──── FAULT <──任何时刻保护触发──┘
 */
#ifndef SYS_STATE_H
#define SYS_STATE_H

#include <stdint.h>
#include "app_msg.h"

typedef enum {
    SYS_INIT = 0,
    SYS_IDLE,       /* 上电完成，功率级关断，可接受使能 */
    SYS_READY,      /* 已使能，目标为零 */
    SYS_RUN,        /* 已使能，正在输出力矩 */
    SYS_FAULT,      /* 故障锁存，必须显式清 */
} sys_state_t;

/* 参数表：所有可在线整定的量都放这里，读写必须走接口加锁 */
typedef struct {
    float kp_cur, ki_cur;
    float kp_spd, ki_spd;
    float kp_pos;
    float iq_limit;
    float spd_limit;
} sys_param_t;

void Sys_Init(void);

sys_state_t Sys_GetState(void);
const char *Sys_StateName(sys_state_t s);

/* 状态迁移（内部会检查合法性，非法迁移直接拒绝并返回 -1）*/
int8_t Sys_SetState(sys_state_t next);

/* 把当前状态通知界面上其他任务 */
void Sys_PublishState(void);

/* 使能/失能请求（只有 IDLE/READY/RUN 之间有效）*/
int8_t Sys_RequestEnable(uint8_t en);

/* 故障：置位并锁存状态，返回是否新产生 */
uint8_t Sys_RaiseFault(uint16_t bit);
void    Sys_ClearFault(void);
uint16_t Sys_GetFault(void);

/* 参数读写（内部加互斥锁）*/
void   Sys_GetParam(sys_param_t *out);
float  Sys_GetParamItem(param_id_t id);
int8_t Sys_SetParamItem(param_id_t id, float v);
void   Sys_ApplyParamToControllers(void);

#endif /* SYS_STATE_H */
