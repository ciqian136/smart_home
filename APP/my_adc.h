#ifndef __MY_ADC_H__
#define __MY_ADC_H__
#include "headfile.h"

/** @brief 用户 ADC 初始化（单次采样模式 + 校准）*/
void my_adc_init(void);

/** @brief 读取指定 ADC 通道；切换通道后自动丢弃首个样本 */
uint16_t my_adc_read_channel(uint32_t channel);

/**
 * @brief ADC DMA转换结果数组
 *   adc_val[0] = ADC1_IN1 (PA1) - 烟雾传感器最近一次采样
 *   adc_val[1] = ADC1_IN4 (PA4) - PM2.5传感器最近一次采样
 */
extern volatile uint16_t adc_val[2];

#endif




