/* dht11.h */
#ifndef __DHT11_H
#define __DHT11_H
#include "headfile.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

void DHT11_init(void);
uint8_t DHT11_ReadData(float *temp, float *humi);
void DHT11_proc(void);
uint8_t DHT11_is_ready(void);
float DHT11_get_temp(void);
float DHT11_get_humi(void);
#endif


