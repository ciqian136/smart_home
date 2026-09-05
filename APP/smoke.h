#ifndef __SMOKE_H__
#define __SMOKE_H__

#include "headfile.h"

/** @brief 烟雾传感器初始化（静态状态 + 预热计时）*/
void smoke_init(void);
/** @brief 烟雾传感器数据处理（ADC采样 + 报警判断）*/
void smoke_proc(void);
/** @brief 烟雾传感器反初始化 */
void smoke_deinit(void);
/** @brief 获取烟雾传感器最近一次ADC值 */
uint16_t smoke_get_adc(void);
/** @brief 获取烟雾报警状态（1=报警, 0=正常）*/
uint8_t smoke_is_alarmed(void);
/** @brief 检查传感器是否完成预热（1=就绪, 0=预热中）*/
uint8_t smoke_is_ready(void);

#endif


