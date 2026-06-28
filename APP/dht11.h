/* dht11.h */
#ifndef __DHT11_H
#define __DHT11_H
#include "dht11.h"
#include "headfile.h"
#include "stm32f1xx_hal.h"   
#include <stdint.h>


uint8_t DHT11_ReadData(float *temp, float *humi);
void DHT11_proc(void);
#endif


