/* dht11.h */
#ifndef __DHT11_H
#define __DHT11_H
#include "headfile.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

#define DHT11_READ_OK    0U
#define DHT11_READ_ERROR 1U
#define DHT11_READ_BUSY  2U

void DHT11_init(void);
/** @brief 非阻塞起始信号读取；返回 DHT11_READ_BUSY 时需稍后再次调用 */
uint8_t DHT11_ReadData(float *temp, float *humi);
void DHT11_proc(void);
float DHT11_get_temp(void);
float DHT11_get_humi(void);
#endif


