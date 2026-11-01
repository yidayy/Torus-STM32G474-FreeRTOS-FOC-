/*
 * data_pipe.c  --  环形缓冲实现
 */
#include <string.h>

#include "data_pipe.h"

static wave_pt_t         s_buf[WAVE_DEPTH];
static volatile uint16_t s_head;    /* 下一个写入位置 */
static volatile uint32_t s_seq;     /* 累计写入点数，只增不减 */

void Wave_Init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_head = 0;
    s_seq  = 0;
}

void Wave_Push(int16_t iq_ma, int16_t spd_dps, uint8_t state, uint16_t fault)
{
    uint16_t h = s_head;

    s_buf[h].iq_ma    = iq_ma;
    s_buf[h].spd_dps  = spd_dps;
    s_buf[h].state    = state;
    s_buf[h].fault_hi = (uint8_t)(fault >> 8);

    h++;
    if (h >= WAVE_DEPTH) h = 0;
    s_head = h;                 /* 最后才更新 head，保证读方看不到半条 */
    s_seq++;
}

uint16_t Wave_ReadLatest(wave_pt_t *out, uint16_t n)
{
    if (out == NULL || n == 0) return 0;
    if (n > WAVE_DEPTH) n = WAVE_DEPTH;

    /* 生产者只顾往后写，读方从"当前 head 往前数 n 个"开始 */
    for (uint8_t retry = 0; retry < 2; retry++) {
        uint32_t seq_before = s_seq;
        uint16_t head = s_head;

        uint16_t start = (head >= n) ? (uint16_t)(head - n)
                                     : (uint16_t)(WAVE_DEPTH - (n - head));

        for (uint16_t i = 0; i < n; i++) {
            uint16_t idx = start + i;
            if (idx >= WAVE_DEPTH) idx -= WAVE_DEPTH;
            out[i] = s_buf[idx];
        }

        /* 拷贝期间生产者如果写了超过一整圈，这次数据作废，重来一次 */
        if ((s_seq - seq_before) <= (uint32_t)n) {
            return n;
        }
    }
    return 0;
}

uint32_t Wave_GetSeq(void) { return s_seq; }
