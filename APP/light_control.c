#include "light_control.h"
#include "BH1750.h"
#include "led.h"

static uint8_t current_pwm = 0;   // 保存当前 PWM 值（0~100）


void light_control_update(void)
{
    float lux = bh1750_get_lux(); // 获取滤波后的总光照

    /* 读取失败则跳过本次调节（保持原 PWM） */
    if (lux < 0.0f) {
        return;
    }

    /* 区间判断与步进调节 */
    if (lux < LUX_LOW) {
        // 光照不足，增加亮度
        if (current_pwm < 100) {
            current_pwm += PWM_STEP;
            if (current_pwm > 100) current_pwm = 100;
        }
    } else if (lux > LUX_HIGH) {
        // 光照过强，减小亮度
        if (current_pwm > 0) {
            if (current_pwm < PWM_STEP) current_pwm = 0;
            else current_pwm -= PWM_STEP;
        }
    } else {

    }
    led_set(current_pwm,current_pwm);

}

