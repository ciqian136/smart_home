#include "my_adc.h"
#include "my_uart.h"
#include "stm32f1xx_hal_adc.h"
#include "usart.h"

/* 引用 CubeMX 生成的 ADC1 句柄 */
extern ADC_HandleTypeDef hadc1;
/* ADC 转换结果数组，由 DMA 自动填充 [0]=通道0(烟雾), [1]=通道4(PM2.5) */
volatile uint16_t adc_val[2]={0};

/**
  * @brief  用户 ADC 初始化 - 校准并启动 DMA 方式的 ADC 转换
  *         使用 DMA 连续采集两个通道的数据到 adc_val 数组
  */
void my_adc_init(void)
{
    adc_val[0]=0;  /* 通道0（烟雾传感器）初始值 */
    adc_val[1]=0;  /* 通道4（PM2.5传感器）初始值 */
    HAL_ADCEx_Calibration_Start(&hadc1);                                        /* ADC 自动校准 */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_val, sizeof(adc_val)/sizeof(adc_val[0])); /* 启动 DMA 传输 */
}



/* ADC 转换完成回调函数（当前未使用，保留以备扩展）*/
// void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
// {
//     if (hadc->Instance == ADC1) {

//     }
// }




