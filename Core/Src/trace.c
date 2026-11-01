/*
 * trace.c  --  VOFA+ JustFloat 打包发送
 */
#include <string.h>

#include "main.h"
#include "trace.h"

extern UART_HandleTypeDef huart1;

#define TRACE_FRAME_BYTES   (TRACE_CH_COUNT * 4 + 4)          /* 16 */
#define TRACE_BUF_FRAMES    32u
#define TRACE_BUF_BYTES     (TRACE_FRAME_BYTES * TRACE_BUF_FRAMES)  /* 512 */

static uint8_t  s_buf[TRACE_BUF_BYTES];
static uint16_t s_len;
static uint32_t s_drop;
static uint8_t  s_enabled = 1;

/* JustFloat 帧尾 = 小端 float 的 +inf */
static const uint8_t s_tail[4] = { 0x00, 0x00, 0x80, 0x7F };

void Trace_Init(void)
{
    s_len = 0;
    s_drop = 0;
    s_enabled = 1;
}

void Trace_SetEnabled(uint8_t en) { s_enabled = en; }

void Trace_Push(float iq, float speed, float target)
{
    if (!s_enabled) return;

    if (s_len + TRACE_FRAME_BYTES > TRACE_BUF_BYTES) {
        /* 缓冲满说明串口发不过来。这里选择丢新帧而不是丢旧帧：
           旧帧已经攒好了，发出去至少波形是连续的；
           丢掉最旧的一帧会让 VOFA+ 看到时间轴跳变。 */
        s_drop++;
        return;
    }

    memcpy(&s_buf[s_len], &iq,     4); s_len += 4;
    memcpy(&s_buf[s_len], &speed,  4); s_len += 4;
    memcpy(&s_buf[s_len], &target, 4); s_len += 4;
    memcpy(&s_buf[s_len], s_tail,  4); s_len += 4;
}

void Trace_Flush(void)
{
    if (s_len == 0 || !s_enabled) return;

    /* 超时给足：512 字节 @921600 约 5.6 ms，留 20 ms 余量 */
    (void)HAL_UART_Transmit(&huart1, s_buf, s_len, 20);
    s_len = 0;
}

uint32_t Trace_GetDropCount(void) { return s_drop; }
