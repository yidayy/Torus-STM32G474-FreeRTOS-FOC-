/*
 * data_pipe.h  --  波形数据管道（无锁环形缓冲）
 *
 * 为什么要单独搞一层：
 *   ISR / 控制任务 1 kHz 产数据，HMI 30 fps 消费，Log 100 Hz 消费。
 *   生产者和消费者速度差 30 倍，中间必须有个缓冲区。
 *
 * 为什么不用 FreeRTOS 队列：
 *   队列每次入队都要进临界区、改指针、可能触发调度。
 *   1 kHz 还好，但如果以后加到 5 kHz 就不划算了。
 *   环形缓冲 + 序号校验是"单生产者单消费者"场景下最快的方案 ——
 *   写方只动 head，读方只读 head 和 seq，天生没有竞争。
 */
#ifndef DATA_PIPE_H
#define DATA_PIPE_H

#include <stdint.h>

#define WAVE_DEPTH      512     /* 512 点，1 kHz 采样 ≈ 0.5 秒窗口 */

typedef struct {
    int16_t  iq_ma;         /* q 轴电流，mA */
    int16_t  spd_dps;       /* 转速，0.1 rad/s */
    uint8_t  state;         /* sys_state_t */
    uint8_t  fault_hi;      /* 故障码高字节 */
} wave_pt_t;

void        Wave_Init(void);

/* 生产者：控制任务每 1 ms 调一次 */
void        Wave_Push(int16_t iq_ma, int16_t spd_dps, uint8_t state, uint16_t fault);

/* 消费者：把最近 n 点拷贝到 out，返回实际拷贝点数。
 * 读取过程中如果生产者又写了，会自动重试一次，保证拿到的一致快照。 */
uint16_t    Wave_ReadLatest(wave_pt_t *out, uint16_t n);

/* 总写入点数，用来算采样率是否掉点 */
uint32_t    Wave_GetSeq(void);

#endif /* DATA_PIPE_H */
