/*
 * current_sense.c  --  ADC 注入组采样
 *
 * 用注入组而不是规则组：
 *   规则组被谁启动都行，容易被别的代码抢；注入组专供电流环，
 *   而且注入组有独立的 JDR1/JDR2 数据寄存器，不会被后续转换覆盖。
 *   这是工业伺服里的标准做法。
 */
#include "config.h"
#include "current_sense.h"

extern ADC_HandleTypeDef hadc1;

static uint16_t s_off_a = 2048;     /* 零电流 ADC 码 */
static uint16_t s_off_b = 2048;

/* ADC 码 -> 安培 */
static inline float code_to_amp(uint16_t code, uint16_t offset)
{
    float v = ((float)code - (float)offset) * (CFG_ADC_VREF_V / CFG_ADC_FULL_SCALE);
    return v * CFG_CS_AMPERE_PER_VOLT;
}

void CS_Init(void)
{
    s_off_a = 2048;
    s_off_b = 2048;
}

static void read_raw(uint16_t *ra, uint16_t *rb)
{
#if (CFG_CS_SIMULATE)
    *ra = s_off_a;
    *rb = s_off_b;
#else
    /* 直接写寄存器，比 HAL_ADCEx_InjectedStart 少约 3 us */
    ADC1->CR |= ADC_CR_JADSTART;
    while ((ADC1->ISR & ADC_ISR_JEOC) == 0u) { }
    ADC1->ISR &= ~ADC_ISR_JEOC;

    *ra = (uint16_t)(ADC1->JDR1 & 0x0FFFu);
    *rb = (uint16_t)(ADC1->JDR2 & 0x0FFFu);
#endif
}

int8_t CS_Calibrate(void)
{
    /* 必须：驱动关断、电机静止。取 256 次平均压噪声 */
    uint32_t sum_a = 0, sum_b = 0;

    for (uint32_t i = 0; i < 256u; i++) {
        uint16_t ra, rb;
        read_raw(&ra, &rb);
        sum_a += ra;
        sum_b += rb;
    }
    s_off_a = (uint16_t)(sum_a / 256u);
    s_off_b = (uint16_t)(sum_b / 256u);

    /* 合理范围检查：中点 1.65V 对应码 2048，允许 ±200 mV = ±248 码 */
    int32_t da = (int32_t)s_off_a - 2048;
    int32_t db = (int32_t)s_off_b - 2048;
    if (da < 0) da = -da;
    if (db < 0) db = -db;
    if (da > 248 || db > 248) return -1;

    return 0;
}

void CS_Read(float *ia, float *ib)
{
    uint16_t ra, rb;
    read_raw(&ra, &rb);
    *ia = code_to_amp(ra, s_off_a);
    *ib = code_to_amp(rb, s_off_b);
}

float CS_ReadVbus(void)
{
#if (CFG_CS_SIMULATE)
    return 12.0f;
#else
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 2) == HAL_OK) {
            sum += HAL_ADC_GetValue(&hadc1);
        }
    }
    HAL_ADC_Stop(&hadc1);
    float v = ((float)(sum / 8u) * (CFG_ADC_VREF_V / CFG_ADC_FULL_SCALE)) * CFG_VBUS_DIV;
    return v;
#endif
}

uint16_t CS_GetOffsetA(void) { return s_off_a; }
uint16_t CS_GetOffsetB(void) { return s_off_b; }
