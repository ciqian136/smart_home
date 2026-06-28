#ifndef __DHT11_H__
#define __DHT11_H__
#include "headfile.h"

void dht11_init(void);
void dht11_Mode(uint8_t cmd);
uint8_t dht11_Start(void);
uint8_t dht11_Read(uint8_t *temp);

void dht11_proc(void);
#endif

