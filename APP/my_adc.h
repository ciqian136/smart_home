#ifndef __MY_ADC_H__
#define __MY_ADC_H__
#include "headfile.h"

/** @brief User ADC init: calibration only. Sampling is single-shot per channel. */
void my_adc_init(void);

/** @brief Read one ADC channel by polling to avoid shared DMA timing conflicts. */
uint16_t my_adc_read_channel(uint32_t channel);

#endif




