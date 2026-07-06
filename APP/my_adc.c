#include "my_adc.h"
#include "my_uart.h"
#include "stm32f1xx_hal_adc.h"
#include "usart.h"

/* 引用 CubeMX 生成的 ADC1 句柄 */
extern ADC_HandleTypeDef hadc1;
/* adc_val[0] = ADC1_IN1 (PA1) - 烟雾传感器
adc_val[1] = ADC1_IN4 (PA4) - PM2.5传感器 */
volatile uint16_t adc_val[2]={0};

void my_adc_init(void)
{
    adc_val[0] = 0;
    adc_val[1] = 0;
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_val, 2); // 长度固定为2
}



