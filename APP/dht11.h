/* dht11.h */
#ifndef __DHT11_H
#define __DHT11_H
#include "headfile.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

#define DHT11_UART1_LOG_DEFAULT 0U

void DHT11_init(void);
uint8_t DHT11_ReadData(float *temp, float *humi);
void DHT11_proc(void);
uint8_t DHT11_is_ready(void);
float DHT11_get_temp(void);
float DHT11_get_humi(void);

extern volatile uint8_t dht11_uart1_log_enabled;
#endif


