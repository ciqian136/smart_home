#ifndef __SMOKE_H__
#define __SMOKE_H__

#include "headfile.h"

#define SMOKE_UART1_LOG_DEFAULT 0U
#define SMOKE_ANOMALY_ADC_LOW         0x01U
#define SMOKE_ANOMALY_ADC_HIGH        0x02U
#define SMOKE_ANOMALY_ADC_STUCK       0x04U
#define SMOKE_ANOMALY_DO_ADC_MISMATCH 0x08U

/* 测试阶段：不读取 MQ2 ADC，使用模拟 ppm；正式使用时改为 0。 */
#define SMOKE_TEST_MODE 1U
#define SMOKE_TEST_NORMAL_MIN_PPM 45.0f
#define SMOKE_TEST_NORMAL_MAX_PPM 65.0f
#define SMOKE_TEST_ALARM_PPM      150.0f
#define SMOKE_TEST_ALARM_HOLD_MS  10000U

/** @brief 烟雾传感器初始化（静态内存 + 预热计时）*/
void smoke_init(void);
/** @brief 烟雾传感器数据处理（ADC采集 + 滤波 + 报警判断）*/
void smoke_proc(void);
/** @brief 烟雾传感器反初始化（释放内存）*/
void smoke_deinit(void);
/** @brief 获取烟雾传感器滤波后的ADC值（原始值）*/
uint16_t smoke_get_adc(void);
/** @brief 获取烟雾浓度（ppm），基于 MQ2 Rs/Ro 特性曲线转换 */
float smoke_get_ppm(void);
/** @brief 获取烟雾报警状态（1=报警, 0=正常）*/
uint8_t smoke_is_alarmed(void);
/** @brief 检查传感器是否完成预热（1=就绪, 0=预热中）*/
uint8_t smoke_is_ready(void);
/** @brief 获取 MQ2 异常标志位，见 SMOKE_ANOMALY_* */
uint8_t smoke_get_anomaly_flags(void);
#if SMOKE_TEST_MODE
/** @brief 测试模式：将烟雾浓度临时置为超标值。 */
void smoke_test_trigger_alarm(void);
#endif

extern volatile uint8_t smoke_uart1_log_enabled;

#endif


