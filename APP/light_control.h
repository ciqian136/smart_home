#ifndef __LIGHT_CONTROL_H__
#define __LIGHT_CONTROL_H__

#include <stdint.h>

#define LUX_LOW         250.0f   // 光照下限（低于此值增加 PWM）
#define LUX_HIGH        400.0f   // 光照上限（高于此值减少 PWM）
#define PWM_STEP        5        // 每次调节的步长（0~100）

   
void light_control_update(void);   

#endif


