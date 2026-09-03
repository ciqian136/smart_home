#include "my_adc.h"
#include "stm32f1xx_hal_adc.h"
#include "usart.h"

/* 引用 CubeMX 生成的 ADC1 句柄 */
extern ADC_HandleTypeDef hadc1;

#define ADC_POLL_TIMEOUT_MS 10U
#define ADC_INVALID_CHANNEL 0xFFFFFFFFU
#define ADC_SMOKE_CHANNEL   ADC_CHANNEL_1
#define ADC_PM25_CHANNEL    ADC_CHANNEL_4

/* 兼容旧接口：保存两个传感器最近一次的同步采样值。 */
volatile uint16_t adc_val[2] = {0};

static uint32_t selected_channel = ADC_INVALID_CHANNEL;
static uint8_t adc_ready = 0U;

static uint8_t adc_channel_index(uint32_t channel, uint8_t *index)
{
    if (index == NULL) return 0U;

    if (channel == ADC_SMOKE_CHANNEL) {
        *index = 0U;
        return 1U;
    }
    if (channel == ADC_PM25_CHANNEL) {
        *index = 1U;
        return 1U;
    }

    return 0U;
}

static HAL_StatusTypeDef adc_select_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    if (!adc_ready) return HAL_ERROR;
    if (selected_channel == channel) return HAL_OK;

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return HAL_ERROR;
    }

    selected_channel = channel;
    return HAL_OK;
}

static HAL_StatusTypeDef adc_sample_once(uint16_t *value)
{
    HAL_StatusTypeDef status;

    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return HAL_ERROR;
    }

    status = HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT_MS);
    if (status == HAL_OK && value != NULL) {
        *value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }

    HAL_ADC_Stop(&hadc1);
    return status;
}

void my_adc_init(void)
{
    adc_val[0] = 0;
    adc_val[1] = 0;

    HAL_ADC_Stop_DMA(&hadc1);
    HAL_ADC_Stop(&hadc1);

    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;

    adc_ready = (HAL_ADC_Init(&hadc1) == HAL_OK) ? 1U : 0U;
    if (adc_ready) {
        adc_ready = (HAL_ADCEx_Calibration_Start(&hadc1) == HAL_OK) ? 1U : 0U;
    }
    selected_channel = ADC_INVALID_CHANNEL;
}

uint16_t my_adc_read_channel(uint32_t channel)
{
    uint16_t value = 0U;
    uint16_t discard = 0U;
    uint8_t index = 0U;
    uint8_t channel_changed = (selected_channel != channel) ? 1U : 0U;

    if (adc_select_channel(channel) != HAL_OK) return 0U;

    if (channel_changed && adc_sample_once(&discard) != HAL_OK) {
        return 0U;
    }

    if (adc_sample_once(&value) != HAL_OK) return 0U;

    if (adc_channel_index(channel, &index)) {
        adc_val[index] = value;
    }

    return value;
}



