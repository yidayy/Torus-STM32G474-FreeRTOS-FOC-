/*
 * encoder.h  --  AS5600 磁编码器
 *
 * AS5600 走 I2C1（PB8/PB9，重映射）。12 bit = 4096 线。
 * 注意：I2C 速率 400 kHz 下单次读 1 个角度约 200 us，
 *       所以位置环 500 Hz（2 ms）够用，但再快就会顶住 I2C 带宽。
 *       这是本项目已知瓶颈，报告里要写清楚。
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

#define AS5600_ADDR_7BIT    0x36u
#define AS5600_REG_ANGLE_H  0x0Eu
#define AS5600_REG_STATUS   0x0Bu

void     Encoder_Init(void);

/* 机械角度，rad，范围 [0, 2pi)。返回 0 表示成功，-1 表示 I2C 失败 */
int8_t   Encoder_Read(void);

/* 最近一次读到的机械角（rad）*/
float    Encoder_GetMechAngle(void);

/* 电角度 = (mech * pole_pairs * dir + offset) mod 2pi */
float    Encoder_GetElecAngle(void);

/* 用当前机械角作为电角度零偏（对齐用）*/
void     Encoder_SetElecOffset(float offset_rad);

/* 读失败计数，用于监控任务判失联 */
uint32_t Encoder_GetErrorCount(void);

#endif /* ENCODER_H */
