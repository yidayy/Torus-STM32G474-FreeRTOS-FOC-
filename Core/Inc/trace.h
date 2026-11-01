/*
 * trace.h  --  上位机实时波形（VOFA+ JustFloat 协议）
 *
 * 为什么要用 VOFA+ 而不是自己写个 Python 上位机：
 *   1) 它是国产免费软件，装完就能用，不用配 Python 环境
 *   2) JustFloat 是二进制 float 协议，带宽占用比打印字符串小一半
 *   3) 支持多通道、自动量程、暂停、导出 CSV，正好是调 PID 需要的东西
 *   4) 视频里那些"波形一调 PID 就变形状"的演示，用的就是这个
 *
 * JustFloat 帧格式：
 *   [float ch0][float ch1][float ch2]...[0x00 0x00 0x80 0x7F]
 *   最后 4 字节是 +inf 作为帧尾，VOFA+ 靠它切帧。
 *   小端，STM32 本身就是小端，memcpy 直接扔过去就行。
 *
 * 为什么攒一批再发而不是每 1 ms 发一帧：
 *   1 kHz 下每帧 16 字节，一帧一次 HAL_UART_Transmit 会把 CPU 切碎。
 *   攒 32 帧（512 字节）一次发，@921600 约 5.5 ms，平均下来开销小得多。
 */
#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>

/* 通道顺序 —— 和 VOFA+ 里配的通道名一一对应 */
#define TRACE_CH_IQ       0   /* q 轴电流 A */
#define TRACE_CH_SPEED    1   /* 实测转速 rad/s */
#define TRACE_CH_TARGET   2   /* 目标转速 rad/s */
#define TRACE_CH_COUNT    3

void Trace_Init(void);

/* 攒一帧（3 通道），缓冲满了会自动丢弃最旧的整帧 */
void Trace_Push(float iq, float speed, float target);

/* 把攒的数据一次性发出去 */
void Trace_Flush(void);

/* 因为缓冲满被丢掉的帧数，串口带宽不够时会涨 */
uint32_t Trace_GetDropCount(void);

/* 开关：0 = 关掉波形上传，只留串口日志（省带宽、省 CPU）*/
void Trace_SetEnabled(uint8_t en);

#endif /* TRACE_H */
