#ifndef __PM25_H__
#define __PM25_H__
#include "headfile.h"

/** @brief PM2.5传感器初始化（分配内存 + 启动LED）*/
void PM25_init(void);
/** @brief PM2.5传感器反初始化（释放内存）*/
void PM25_deinit(void);
/** @brief PM2.5传感器数据处理（ADC采集 + 滑动平均滤波）*/
void PM25_proc(void);
/** @brief 获取PM2.5传感器滤波后的ADC值（原始值） */
uint16_t PM25_get_adc(void);
/** @brief 获取PM2.5浓度（µg/m³），基于 Sharp GP2Y1014AU0F 特性曲线转换 */
float PM25_get_ugm3(void);

#endif


