/*
TIM4_PMW模式，PWM模式2
引脚定义：
TIM4_PMW_CHANNLE3--PD14
*/

#include "fan.h"

#define FAN_CHANNEL TIM_CHANNEL_3
#define FAN_TIM htim4
extern TIM_HandleTypeDef htim4;

void fan_init(void)
{
    __HAL_TIM_SET_COMPARE(&FAN_TIM,FAN_CHANNEL,0);
    HAL_TIM_PWM_Start(&FAN_TIM, FAN_CHANNEL);
    HAL_TIM_PWM_Start(&FAN_TIM, FAN_CHANNEL);
}

void fan_set(uint16_t val)
{
    if(val>1000) val=1000;
    __HAL_TIM_SET_COMPARE(&FAN_TIM,FAN_CHANNEL,val);
}



