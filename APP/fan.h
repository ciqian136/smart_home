#ifndef __FAN_H__
#define __FAN_H__

#include "headfile.h"

void fan_init(void);
void fan_set(uint16_t val);
void fan_speed_up(void);
void fan_speed_down(void);
void fan_set_gear(uint8_t gear);

uint8_t  fan_is_open(void);
uint16_t fan_get_speed(void);

#endif
