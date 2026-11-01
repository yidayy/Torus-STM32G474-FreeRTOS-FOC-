/*
 * app_msg.h  --  任务间消息定义
 *
 * 设计原则：任务之间不许直接改对方的变量。
 *   任何"我要你干点什么"都必须变成一条消息，走队列进到对方手里。
 *
 * 为什么这么较真：
 *   裸全局变量在单任务里没问题，多任务里就是数据竞争。
 *   典型事故 —— Task_Comm 正在写 g_cmd.speed_ref（4 字节浮点，分两次写），
 *   Task_Speed 恰好读到一个"高两字节是新的、低两字节是旧的"值，
 *   于是电机瞬间收到一个荒唐的目标速度。这种 bug 一个月出现一次，
 *   而且永远复现不了。
 */
#ifndef APP_MSG_H
#define APP_MSG_H

#include <stdint.h>

/* 消息类型 */
typedef enum {
    MSG_NONE = 0,
    MSG_ENABLE,          /* value: 0/1 */
    MSG_SET_MODE,        /* value: MODE_TORQUE / MODE_SPEED / MODE_POSITION */
    MSG_SET_SPEED,       /* value: rad/s，负值为反转 */
    MSG_SET_POS,         /* value: rad */
    MSG_SET_IQ,          /* value: A，负值为反向力矩 */
    MSG_CLEAR_FAULT,
    MSG_SET_PARAM,       /* param_id + value */
    MSG_GET_STATUS,
    MSG_ESTOP,           /* 急停，最高优先级的软件命令 */
} msg_id_t;

/* 可在线修改的参数索引 */
typedef enum {
    PARAM_KP_CUR = 0,
    PARAM_KI_CUR,
    PARAM_KP_SPD,
    PARAM_KI_SPD,
    PARAM_KP_POS,
    PARAM_IQ_LIMIT,
    PARAM_SPD_LIMIT,
    PARAM_COUNT
} param_id_t;

typedef struct {
    uint8_t id;         /* msg_id_t */
    uint8_t param;      /* param_id_t，仅 MSG_SET_PARAM 用 */
    float   value;
} app_msg_t;

/* 故障码（与 foc_core.h 的 FOC_FAULT_* 位定义保持一致，另外扩展系统级故障）*/
#define FAULT_BIT_OC        0x0001u
#define FAULT_BIT_OV        0x0002u
#define FAULT_BIT_UV        0x0004u
#define FAULT_BIT_STALL     0x0008u
#define FAULT_BIT_ENC       0x0010u
#define FAULT_BIT_DRV       0x0020u
#define FAULT_BIT_OVERTEMP  0x0040u
#define FAULT_BIT_TIMEOUT   0x0080u   /* 上位机通信超时 */

#endif /* APP_MSG_H */
