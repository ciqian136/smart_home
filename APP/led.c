/*
TIM4_PMW模式，PWM模式2
引脚定义：
TIM4_PMW_CHANNLE1--PD12
TIM4_PMW_CHANNLE2--PD13
*/

#include "led.h"
#include "stm32_hal_legacy.h"
#include "stm32f1xx_hal_tim.h"

#define LED1_CHANNEL TIM_CHANNEL_1
#define LED2_CHANNEL TIM_CHANNEL_2
#define LED_TIM htim4
extern TIM_HandleTypeDef htim4;

void led_init(void)
{
    __HAL_TIM_SET_COMPARE(&LED_TIM,LED1_CHANNEL,0);
    __HAL_TIM_SET_COMPARE(&LED_TIM,LED2_CHANNEL,0);
    HAL_TIM_PWM_Start(&LED_TIM, LED1_CHANNEL);
    HAL_TIM_PWM_Start(&LED_TIM, LED2_CHANNEL);
}

void led1_set(uint16_t val)
{
    if(val>1000) val=1000;
    __HAL_TIM_SET_COMPARE(&LED_TIM,LED1_CHANNEL,val);
}

void led2_set(uint16_t val)
{
    if(val>1000) val=1000;
    __HAL_TIM_SET_COMPARE(&LED_TIM,LED2_CHANNEL,val);
}
void led_set(uint16_t val1, uint16_t val2)
{
    if(val1>1000) val1=1000;
    if(val2>1000) val2=1000;
    __HAL_TIM_SET_COMPARE(&LED_TIM,LED1_CHANNEL,val1); 
    __HAL_TIM_SET_COMPARE(&LED_TIM,LED2_CHANNEL,val2);
}

