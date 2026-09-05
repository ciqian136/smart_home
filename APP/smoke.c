/* 烟雾传感器配置 */
#include "smoke.h"
#include "my_adc.h"
#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_def.h"
#include <stdint.h>

#define DO_GPIO GPIOC               /* 数字报警输出引脚端口 */
#define DO_GPIO_PIN GPIO_PIN_2       /* 数字报警输出引脚号 */
#define ALARM_THRESHOLD 300          /* 软件报警阈值 */
#define PREHEAT_TIME 20              /* 传感器预热时间（秒）*/
#define SMOKE_ADC_CHANNEL ADC_CHANNEL_1

/* 调试时改为 1，正常使用保持 0 */
#define SMOKE_DEBUG 0
#define SMOKE_DEBUG_INTERVAL_MS 1000U

#if SMOKE_DEBUG
#define SMOKE_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define SMOKE_DEBUG_PRINTF(...) ((void)0)
#endif

static uint16_t g_adc = 0U;         /* 最近一次ADC值 */
static uint8_t g_alarm = 0U;        /* 报警状态（0=正常, 1=报警）*/
static uint8_t g_ready = 0U;        /* 传感器预热完成标志 */
static uint32_t g_start = 0U;       /* 初始化时刻时间戳（ms）*/

void smoke_init(void) {
  /* 记录初始化时间并初始化所有变量 */
  g_start = HAL_GetTick();    /* 记录当前时间作为预热起始 */
  g_ready = 0U;               /* 预热未完成 */
  g_adc = 0U;                 /* ADC初始值 */
  g_alarm = 0U;               /* 初始无报警 */

  SMOKE_DEBUG_PRINTF("[SMOKE] init, wait %ds\r\n", PREHEAT_TIME);
}

void smoke_deinit(void) {
  g_adc = 0U;
  g_alarm = 0U;
  g_ready = 0U;

  SMOKE_DEBUG_PRINTF("[SMOKE] deinit\r\n");
}

void smoke_proc(void) {
  uint32_t now = HAL_GetTick();
#if SMOKE_DEBUG
  static uint32_t debug_last_tick = 0U;
#endif

  /* 预热阶段：等待传感器稳定 */
  if (!g_ready) {
    if (now - g_start < (uint32_t)PREHEAT_TIME * 1000U) {
#if SMOKE_DEBUG
      if (now - debug_last_tick >= SMOKE_DEBUG_INTERVAL_MS) {
        uint32_t elapsed_s = (now - g_start) / 1000U;
        uint32_t remain_s = ((uint32_t)PREHEAT_TIME > elapsed_s) ? ((uint32_t)PREHEAT_TIME - elapsed_s) : 0U;
        debug_last_tick = now;
        SMOKE_DEBUG_PRINTF("[SMOKE] preheat remain=%lus\r\n", (unsigned long)remain_s);
      }
#endif
      return;
    }
    g_ready = 1U;
    SMOKE_DEBUG_PRINTF("[SMOKE] ready\r\n");
  }

  /* 烟雾传感器 AO 接 ADC1_IN1 / PA1，按通道同步采样。 */
  uint16_t val = my_adc_read_channel(SMOKE_ADC_CHANNEL);
  g_adc = val;

  /* 报警判断：数字IO低电平触发 或 ADC值超过阈值 */
  g_alarm = (HAL_GPIO_ReadPin(DO_GPIO, DO_GPIO_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
  if (g_adc > ALARM_THRESHOLD)
    g_alarm = 1U;

#if SMOKE_DEBUG
  if (now - debug_last_tick >= SMOKE_DEBUG_INTERVAL_MS) {
    debug_last_tick = now;
    SMOKE_DEBUG_PRINTF("[SMOKE] adc=%u alarm=%u ready=%u\r\n",
                       (unsigned int)g_adc,
                       (unsigned int)g_alarm,
                       (unsigned int)g_ready);
  }
#endif
}

/**
  * @brief  检查烟雾传感器是否已完成预热
  * @return 1=已就绪, 0=预热中
  */
uint8_t smoke_is_ready(void) { return g_ready; }

/**
  * @brief  获取烟雾传感器最近一次 ADC 值
  * @return 最近一次ADC值（未初始化时返回0）
  */
uint16_t smoke_get_adc(void) { return g_adc; }

/**
  * @brief  获取烟雾报警状态
  * @return 1=报警中, 0=正常
  */
uint8_t smoke_is_alarmed(void) { return g_alarm; }
