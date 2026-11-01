/*
 * encoder.c  --  AS5600 驱动
 */
#include "config.h"
#include "encoder.h"
#include "foc_math.h"

extern I2C_HandleTypeDef hi2c3;

static volatile float    s_mech;
static volatile float    s_elec_offset;
static volatile uint32_t s_err;

#if (CFG_ENC_SIMULATE)
static float s_sim_angle;
#endif

void Encoder_Init(void)
{
    s_mech = 0.0f;
    s_elec_offset = 0.0f;
    s_err = 0;
#if (CFG_ENC_SIMULATE)
    s_sim_angle = 0.0f;
#endif
}

int8_t Encoder_Read(void)
{
#if (CFG_ENC_SIMULATE)
    /* 无硬件模式：按固定速率累加，用于验证上层链路 */
    s_sim_angle = fm_wrap_2pi(s_sim_angle + 0.02f);
    s_mech = s_sim_angle;
    return 0;
#else
    uint8_t buf[2];
    if (HAL_I2C_Mem_Read(&hi2c3, (uint16_t)(AS5600_ADDR_7BIT << 1),
                         AS5600_REG_ANGLE_H, I2C_MEMADD_SIZE_8BIT,
                         buf, 2, 5) != HAL_OK) {
        s_err++;
        return -1;
    }
    uint16_t raw = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]) & 0x0FFFu;
    s_mech = ((float)raw / (float)CFG_ENCODER_CPR) * FM_2PI;
    return 0;
#endif
}

float Encoder_GetMechAngle(void) { return s_mech; }

float Encoder_GetElecAngle(void)
{
    /* 对齐时把"转子 d 轴对准 A 相"那一刻的 elec_raw 记下来，
       之后每次减掉它，才是真正的电角度。加/减搞反的现象是电机原地抖 */
    float e = s_mech * (float)CFG_MOTOR_POLE_PAIRS * (float)CFG_ENCODER_DIR - s_elec_offset;
    return fm_wrap_2pi(e);
}

void Encoder_SetElecOffset(float elec_raw_rad)
{
    s_elec_offset = fm_wrap_2pi(elec_raw_rad);
}

uint32_t Encoder_GetErrorCount(void) { return s_err; }
