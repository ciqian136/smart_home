#ifndef __MY_ADC_H__
#define __MY_ADC_H__
#include "headfile.h"

/** @brief 用户 ADC 初始化（校准 + 启动DMA采集）*/
void my_adc_init(void);

/**
 * @brief ADC DMA转换结果数组
 *   adc_val[0] = ADC1_IN0 (PA0) - 烟雾传感器
 *   adc_val[1] = ADC1_IN4 (PA4) - PM2.5传感器
 */
extern volatile uint16_t adc_val[2];

#endif




