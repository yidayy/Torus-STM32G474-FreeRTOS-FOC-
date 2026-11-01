/*
 * sys_state.c  --  状态机与参数表实现
 *
 * 参数表用互斥量保护，不是因为它复杂，而是因为有两个任务会同时改：
 *   Task_Comm（CAN/串口下发参数）和 Task_HMI（按键调参数）。
 * 没有锁的话，一次 Kp 的半个浮点被另一个任务读到，就是一次炸管级别的意外。
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "config.h"
#include "sys_state.h"
#include "app_tasks.h"
#include "foc_core.h"

static volatile sys_state_t s_state = SYS_INIT;
static volatile uint16_t    s_fault;
static sys_param_t          s_param;
static SemaphoreHandle_t    s_mtxParam;

extern EventGroupHandle_t g_evtGroup;

/* ------------------------------------------------------------------ */
void Sys_Init(void)
{
    s_state = SYS_INIT;
    s_fault = 0;

    s_param.kp_cur    = CFG_PID_CURRENT_KP;
    s_param.ki_cur    = CFG_PID_CURRENT_KI;
    s_param.kp_spd    = CFG_PID_SPEED_KP;
    s_param.ki_spd    = CFG_PID_SPEED_KI;
    s_param.kp_pos    = CFG_PID_POS_KP;
    s_param.iq_limit  = CFG_CURRENT_MAX_A;
    s_param.spd_limit = CFG_SPEED_MAX_RAD_S;

    s_mtxParam = xSemaphoreCreateMutex();
    configASSERT(s_mtxParam != NULL);
}

/* ------------------------------------------------------------------ */
sys_state_t Sys_GetState(void) { return s_state; }

const char *Sys_StateName(sys_state_t s)
{
    switch (s) {
    case SYS_INIT:  return "INIT";
    case SYS_IDLE:  return "IDLE";
    case SYS_READY: return "READY";
    case SYS_RUN:   return "RUN";
    case SYS_FAULT: return "FAULT";
    default:        return "?";
    }
}

int8_t Sys_SetState(sys_state_t next)
{
    sys_state_t cur = s_state;

    /* 非法迁移直接拒 —— 这是状态机存在的意义，
       否则一个错误顺序就能让功率级在故障状态下被使能 */
    switch (next) {
    case SYS_IDLE:
        if (cur != SYS_INIT && cur != SYS_FAULT) return -1;
        break;
    case SYS_READY:
        if (cur != SYS_IDLE && cur != SYS_RUN) return -1;
        break;
    case SYS_RUN:
        if (cur != SYS_READY) return -1;
        break;
    case SYS_FAULT:
        break;                       /* 任何状态都能进故障 */
    default:
        return -1;
    }

    s_state = next;

    /* 一进故障立刻掐功率级，不等任务调度 */
    if (next == SYS_FAULT) {
        FOC_Enable(0);
        HAL_GPIO_WritePin(GPIO_DRV, PIN_DRV_EN, GPIO_PIN_RESET);
    }

    Sys_PublishState();
    return 0;
}

void Sys_PublishState(void)
{
    if (g_evtGroup != NULL) {
        xEventGroupSetBits(g_evtGroup, EVT_STATE_CHG);
    }
}

/* ------------------------------------------------------------------ */
int8_t Sys_RequestEnable(uint8_t en)
{
    if (en) {
        if (s_state == SYS_FAULT) return -1;      /* 故障未清不能使能 */
        if (s_state != SYS_IDLE) return -1;
        FOC_Enable(1);
        HAL_GPIO_WritePin(GPIO_DRV, PIN_DRV_EN, GPIO_PIN_SET);
        return Sys_SetState(SYS_READY);
    } else {
        FOC_Enable(0);
        HAL_GPIO_WritePin(GPIO_DRV, PIN_DRV_EN, GPIO_PIN_RESET);
        if (s_state == SYS_FAULT) return 0;
        return Sys_SetState(SYS_IDLE);
    }
}

/* ------------------------------------------------------------------ */
uint8_t Sys_RaiseFault(uint16_t bit)
{
    uint16_t old = s_fault;
    s_fault |= bit;

    if (s_fault != old) {
        Sys_SetState(SYS_FAULT);
        return 1;
    }
    return 0;
}

void Sys_ClearFault(void)
{
    s_fault = 0;
    if (s_state == SYS_FAULT) {
        s_state = SYS_IDLE;
        Sys_PublishState();
    }
}

uint16_t Sys_GetFault(void) { return s_fault; }

/* ------------------------------------------------------------------ */
static float *param_ptr(param_id_t id)
{
    switch (id) {
    case PARAM_KP_CUR:    return &s_param.kp_cur;
    case PARAM_KI_CUR:    return &s_param.ki_cur;
    case PARAM_KP_SPD:    return &s_param.kp_spd;
    case PARAM_KI_SPD:    return &s_param.ki_spd;
    case PARAM_KP_POS:    return &s_param.kp_pos;
    case PARAM_IQ_LIMIT:  return &s_param.iq_limit;
    case PARAM_SPD_LIMIT: return &s_param.spd_limit;
    default:              return NULL;
    }
}

void Sys_GetParam(sys_param_t *out)
{
    if (out == NULL) return;
    if (xSemaphoreTake(s_mtxParam, pdMS_TO_TICKS(5)) == pdTRUE) {
        *out = s_param;
        xSemaphoreGive(s_mtxParam);
    }
}

float Sys_GetParamItem(param_id_t id)
{
    float *p = param_ptr(id);
    if (p == NULL) return 0.0f;

    float v = 0.0f;
    if (xSemaphoreTake(s_mtxParam, pdMS_TO_TICKS(5)) == pdTRUE) {
        v = *p;
        xSemaphoreGive(s_mtxParam);
    }
    return v;
}

int8_t Sys_SetParamItem(param_id_t id, float v)
{
    float *p = param_ptr(id);
    if (p == NULL) return -1;

    /* 基本合法性检查，防止上位机写进 1e9 直接把 PI 打炸 */
    if (v != v) return -1;                       /* NaN */
    if (v > 1.0e6f || v < -1.0e6f) return -1;

    if (xSemaphoreTake(s_mtxParam, pdMS_TO_TICKS(5)) != pdTRUE) return -1;
    *p = v;
    xSemaphoreGive(s_mtxParam);

    Sys_ApplyParamToControllers();
    if (g_evtGroup != NULL) xEventGroupSetBits(g_evtGroup, EVT_PARAM_CHG);
    return 0;
}

/* 把参数表同步进 foc_core 的 PI 控制器 */
void Sys_ApplyParamToControllers(void)
{
    sys_param_t p;
    Sys_GetParam(&p);
    FOC_SetCurrentGains(p.kp_cur, p.ki_cur);
    g_foc.current_limit = p.iq_limit;
}
