#ifndef __SMOKE_H__
#define __SMOKE_H__

#include "headfile.h"

/*
 * 烟雾软件报警阈值与回差（ADC 原始值，0~4095）。
 *
 *   adc >= SMOKE_ALARM_ADC_THRESHOLD   -> smoke_is_alarmed() = 1（触发）
 *   adc <= SMOKE_ALARM_ADC_CLEAR       -> 视为环境已恢复，语音那边才会重新武装
 *
 * 两个值之间是回差带：读数在阈值附近抖动时不会反复触发播报。
 * CLEAR 必须严格小于 THRESHOLD。
 *
 * 现场定标方法：把 APP/voice.c 里的 VOICE_ALARM_DEBUG 打开，看 USART1 上每秒
 * 一条的 [ALARM] 日志，把 THRESHOLD 设成"干净环境读数 + 一截余量"。
 *
 * 注意：原值是 300，在 MQ-2 干净空气的读数范围内，环境稍差就会一直报警。
 */
#define SMOKE_ALARM_ADC_THRESHOLD 800U
#define SMOKE_ALARM_ADC_CLEAR     650U

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


