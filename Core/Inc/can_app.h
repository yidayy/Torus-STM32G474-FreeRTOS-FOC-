/*
 * can_app.h  --  CAN 应用层（命令下发 + 遥测上报）
 *
 * 帧格式沿用你 dianji 里已经验证过的那套，只是载荷按伺服语义重新分配：
 *   [0] = 0xA0 | len   (len 仅允许 0..4)
 *   [1] = seq
 *   [2] = cmd
 *   [3..] = payload（小端）
 *   [7] = CRC-8/SMBUS，覆盖 [0..6]
 *
 * ID：
 *   0x101  主机 -> 本节点   命令
 *   0x201  本节点 -> 主机   应答 / 遥测
 */
#ifndef CAN_APP_H
#define CAN_APP_H

#include <stdint.h>

#define CAN_ID_CMD      0x101u
#define CAN_ID_RSP      0x201u

/* 命令码 */
#define CMD_NOP         0x00u
#define CMD_ENABLE      0x01u   /* payload: 0=停 1=使能 */
#define CMD_SET_MODE    0x02u   /* 0=力矩 1=速度 2=位置 */
#define CMD_SET_SPEED   0x03u   /* payload: float rad/s（4 字节）*/
#define CMD_SET_POS     0x04u   /* payload: float rad */
#define CMD_SET_IQ      0x05u   /* payload: float A */
#define CMD_CLEAR_FAULT 0x06u
#define CMD_GET_STATUS  0x07u

/* 控制模式 */
#define MODE_TORQUE     0
#define MODE_SPEED      1
#define MODE_POSITION   2

typedef struct {
    uint8_t  mode;
    uint8_t  enable;
    float    speed_ref;     /* rad/s */
    float    pos_ref;       /* rad   */
    float    iq_ref;        /* A     */
} can_cmd_t;

extern can_cmd_t g_cmd;

void     CAN_App_Init(void);
uint8_t  CRC8_SMBUS(const uint8_t *data, uint8_t len);

/* 由 CAN RX 中断调用（只做搬运，不做解析）*/
void     CAN_App_OnRxFrame(const uint8_t *data, uint8_t len);

/* 由 Task_Comm 调用，解析排队中的帧 */
void     CAN_App_Process(void);

/* 主动上报一帧状态 */
void     CAN_App_SendStatus(void);

#endif /* CAN_APP_H */
