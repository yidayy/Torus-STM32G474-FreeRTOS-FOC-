/*
 * current_sense.h  --  INA199A1 双相电流采样
 *
 * 硬件链：5 mR 采样电阻 -> INA199A1（增益 50）-> PA0 / PA1
 * 换算：I = (Vadc - Vref) / (Rshunt * Gain) = (Vadc - 1.65) * 4  [A]
 *
 * ⚠ 关键：Vref 必须开机实测，不能写死 1.65V。
 *   实际值大概率在 1.62~1.68 之间，偏差 30 mV 就是 120 mA 的静态误差，
 *   电流环会把这点误差积分到饱和 —— 这是 90% 新手炸管的原因。
 */
#ifndef CURRENT_SENSE_H
#define CURRENT_SENSE_H

#include <stdint.h>

/* 开机标定零偏，必须在电机静止、驱动关断时调用 */
void  CS_Init(void);

/* 返回 0 成功；-1 表示零偏偏离合理范围（>200 mV），说明硬件有问题 */
int8_t CS_Calibrate(void);

/* 读一次两相电流（A），在 20 kHz ISR 里调用 */
void  CS_Read(float *ia, float *ib);

/* 读母线电压（V），可以低频调用 */
float CS_ReadVbus(void);

/* 标定得到的零偏（ADC 原始码），调试用 */
uint16_t CS_GetOffsetA(void);
uint16_t CS_GetOffsetB(void);

#endif /* CURRENT_SENSE_H */
