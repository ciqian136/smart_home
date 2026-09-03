#ifndef __PM25_H__
#define __PM25_H__
#include "headfile.h"

/** @brief PM2.5传感器初始化（静态状态 + 启动LED）*/
void PM25_init(void);
/** @brief PM2.5传感器反初始化 */
void PM25_deinit(void);
/** @brief PM2.5传感器数据处理（LED脉冲 + ADC采样）*/
void PM25_proc(void);
/** @brief 获取PM2.5传感器最近一次ADC值 */
uint16_t PM25_get_adc(void);
/** @brief 获取PM2.5浓度（µg/m³），基于 Sharp GP2Y1014AU0F 特性曲线转换 */
float PM25_get_ugm3(void);


#endif


