#include "my_adc.h"
#include "my_uart.h"
#include "stm32f1xx_hal_adc.h"
#include "usart.h"

extern ADC_HandleTypeDef hadc1;

#define ADC_POLL_TIMEOUT_MS 10U
#define ADC_INVALID_CHANNEL 0xFFFFFFFFU

static uint32_t selected_channel = ADC_INVALID_CHANNEL;

static HAL_StatusTypeDef adc_select_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

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
    HAL_ADC_Stop(&hadc1);
    HAL_ADCEx_Calibration_Start(&hadc1);
    selected_channel = ADC_INVALID_CHANNEL;
}

uint16_t my_adc_read_channel(uint32_t channel)
{
    uint16_t value = 0;
    uint16_t discard = 0;
    uint8_t channel_changed = (selected_channel != channel);

    if (adc_select_channel(channel) != HAL_OK) {
        return 0;
    }

    if (channel_changed && adc_sample_once(&discard) != HAL_OK) {
        return 0;
    }

    return (adc_sample_once(&value) == HAL_OK) ? value : 0;
}


