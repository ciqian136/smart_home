/* 烟雾传感器配置 */
#include "smoke.h"
#include "my_adc.h"
#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_def.h"
#include <stdint.h>
#include <math.h>

#define DO_GPIO GPIOC               /* 数字报警输出引脚端口 */
#define DO_GPIO_PIN GPIO_PIN_2       /* 数字报警输出引脚号 */
#define ALARM_THRESHOLD 1000         /* 软件报警阈值 */
#define PREHEAT_TIME 20              /* 传感器预热时间（秒）*/
#define SMOKE_ADC_CHANNEL ADC_CHANNEL_1

/* ── MQ2 PPM 转换校准参数 ────────────────────────── */
#define MQ2_RL_KOHM       10.0f      /* 模块负载电阻（kΩ），典型值 10kΩ */
#define MQ2_VREF_V         3.3f      /* ADC 参考电压（V）*/
#define MQ2_ADC_MAX        4096.0f   /* 12-bit ADC 最大值 */
#define MQ2_RO_CLEAN_AIR   10.0f     /* 清洁空气中传感器电阻（kΩ），需实际校准 */
/* MQ2 烟雾灵敏度曲线: PPM = a * (Rs/Ro)^b              */
/* b = -2.5 为常见烟雾/可燃气体灵敏度斜率                 */
#define MQ2_PPM_A          100.0f    /* 灵敏度系数 a */
#define MQ2_PPM_B          -2.5f     /* 灵敏度指数 b */

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

  uart_printf(&huart1, "[SMOKE] init, wait %ds\r\n", PREHEAT_TIME);
}

void smoke_deinit(void) {
  g_adc = 0U;
  g_alarm = 0U;
  g_ready = 0U;

  uart_printf(&huart1, "[SMOKE] deinit\r\n");
}

void smoke_proc(void) {

  /* 预热阶段：等待传感器稳定 */
  if (!g_ready) {
    if (HAL_GetTick() - g_start < (uint32_t)PREHEAT_TIME * 1000U)
      return;
    g_ready = 1U;
    uart_printf(&huart1, "[SMOKE] ready\r\n");
  }

  /* 烟雾传感器 AO 接 ADC1_IN1 / PA1，按通道同步采样。 */
  uint16_t val = my_adc_read_channel(SMOKE_ADC_CHANNEL);
  g_adc = val;

  /* 报警判断：数字IO低电平触发 或 ADC值超过阈值 */
  g_alarm = (HAL_GPIO_ReadPin(DO_GPIO, DO_GPIO_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
  if (g_adc > ALARM_THRESHOLD)
    g_alarm = 1U;

 //uart_printf(&huart1,"[SMOKE] adc=%d alarm=%d\r\n", g_adc, g_alarm);
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
  * @brief  获取烟雾浓度（ppm），基于 MQ2 Rs/Ro 特性曲线
  * @note   转换原理：
  *         1. ADC → 电压: Vout = adc * Vref / 4096
  *         2. 电压 → 传感器电阻: Rs = RL * (Vref - Vout) / Vout
  *         3. Rs/Ro 比值: ratio = Rs / Ro
  *         4. 灵敏度曲线: PPM = a * ratio^b  (a=100, b=-2.5)
  *         Ro 为清洁空气中传感器电阻，需在清洁空气中实际校准
  * @return 烟雾浓度（ppm），无效时返回 0
  */
float smoke_get_ppm(void)
{
    uint16_t adc = g_adc;
    if (adc == 0 || adc >= 4095) return 0.0f;

    /* 1. ADC → 电压 */
    float vout = adc * MQ2_VREF_V / MQ2_ADC_MAX;

    /* 2. 分压公式反推传感器电阻 Rs (kΩ) */
    float rs = MQ2_RL_KOHM * (MQ2_VREF_V - vout) / vout;

    /* 3. Rs/Ro 比值 */
    float ratio = rs / MQ2_RO_CLEAN_AIR;

    /* 4. 灵敏度曲线: PPM = a * ratio^b */
    float ppm = MQ2_PPM_A * powf(ratio, MQ2_PPM_B);

    if (ppm < 0.0f) ppm = 0.0f;

    return ppm;
}

/**
  * @brief  获取烟雾报警状态
  * @return 1=报警中, 0=正常
  */
uint8_t smoke_is_alarmed(void) { return g_alarm; }





