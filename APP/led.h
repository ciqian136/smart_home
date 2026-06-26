#ifndef __LED_H__
#define __LED_H__
#include "headfile.h"
#include <stdint.h>

void led_init(void);
void led1_set(uint8_t val);
void led2_set(uint8_t val);
void led_set(uint8_t val1,uint8_t val2);



#endif



