#include "my_adc.h"
#include "my_uart.h"
#include "stm32f1xx_hal_adc.h"
#include "usart.h"

extern ADC_HandleTypeDef hadc1;

#define ADC_POLL_TIMEOUT_MS 10U

void my_adc_init(void)
{
    HAL_ADC_Stop(&hadc1);
    HAL_ADCEx_Calibration_Start(&hadc1);
}

uint16_t my_adc_read_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    uint16_t value = 0;

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return 0;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }

    if (HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT_MS) == HAL_OK) {
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }

    HAL_ADC_Stop(&hadc1);
    return value;
}


