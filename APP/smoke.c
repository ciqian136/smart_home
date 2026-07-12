/* 烟雾传感器配置 */
#include "smoke.h"
#include "my_adc.h"
#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_def.h"
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

extern ADC_HandleTypeDef hadc1;
extern volatile uint16_t adc_val[2];
#define DO_GPIO GPIOC               /* 数字报警输出引脚端口 */
#define DO_GPIO_PIN GPIO_PIN_2       /* 数字报警输出引脚号 */
#define ALARM_THRESHOLD 1000         /* 软件报警阈值 */
#define PREHEAT_TIME 20              /* 传感器预热时间（秒）*/
#define WINDOW_SIZE 5                /* 滑动平均滤波窗口大小 */

/* ── MQ2 PPM 转换校准参数 ────────────────────────── */
#define MQ2_RL_KOHM       10.0f      /* 模块负载电阻（kΩ），典型值 10kΩ */
#define MQ2_VREF_V         3.3f      /* ADC 参考电压（V）*/
#define MQ2_ADC_MAX        4096.0f   /* 12-bit ADC 最大值 */
#define MQ2_RO_CLEAN_AIR   10.0f     /* 清洁空气中传感器电阻（kΩ），需实际校准 */
/* MQ2 烟雾灵敏度曲线: PPM = a * (Rs/Ro)^b              */
/* b = -2.5 为常见烟雾/可燃气体灵敏度斜率                 */
#define MQ2_PPM_A          100.0f    /* 灵敏度系数 a */
#define MQ2_PPM_B          -2.5f     /* 灵敏度指数 b */

static uint16_t *buf = NULL;
static uint8_t *buf_index = NULL;   /* 当前窗口索引 */
static uint16_t *g_adc = NULL;      /* 滤波后的ADC平均值 */
static uint8_t *g_alarm = NULL;     /* 报警状态（0=正常, 1=报警）*/
static uint8_t *g_ready = NULL;     /* 传感器预热完成标志 */
static uint32_t *g_start = NULL;    /* 初始化时刻时间戳（ms）*/

void smoke_init(void) {
  /* 动态分配所有需要的内存 */
  buf = (uint16_t *)malloc(WINDOW_SIZE * sizeof(uint16_t));
  buf_index = (uint8_t *)malloc(sizeof(uint8_t));
  g_adc = (uint16_t *)malloc(sizeof(uint16_t));
  g_alarm = (uint8_t *)malloc(sizeof(uint8_t));
  g_ready = (uint8_t *)malloc(sizeof(uint8_t));
  g_start = (uint32_t *)malloc(sizeof(uint32_t));

  /* 检查内存分配是否成功 */
  if (!buf || !buf_index || !g_adc || !g_alarm || !g_ready || !g_start) {
    uart_printf(&huart1, "[SMOKE] malloc failed!\r\n");
    while (1)
      ;
  }

  /* 记录初始化时间并初始化所有变量 */
  *g_start = HAL_GetTick();   /* 记录当前时间作为预热起始 */
  *g_ready = 0;               /* 预热未完成 */
  *g_adc = 0;                 /* ADC初始值 */
  *g_alarm = 0;               /* 初始无报警 */
  *buf_index = 0;             /* 窗口索引初始为0 */
  for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
    buf[i] = 0;               /* 清空滑动窗口 */
  }

  uart_printf(&huart1, "[SMOKE] init (heap), wait %ds\r\n", PREHEAT_TIME);
}

void smoke_deinit(void) {
  if (buf) {
    free(buf);
    buf = NULL;
  }
  if (buf_index) {
    free(buf_index);
    buf_index = NULL;
  }
  if (g_adc) {
    free(g_adc);
    g_adc = NULL;
  }
  if (g_alarm) {
    free(g_alarm);
    g_alarm = NULL;
  }
  if (g_ready) {
    free(g_ready);
    g_ready = NULL;
  }
  if (g_start) {
    free(g_start);
    g_start = NULL;
  }

  uart_printf(&huart1, "[SMOKE] deinit, memory released\r\n");
}

void smoke_proc(void) {

  /* 预热阶段：等待传感器稳定 */
  if (!(*g_ready)) {
    if (HAL_GetTick() - *g_start < (uint32_t)PREHEAT_TIME * 1000)
      return;
    *g_ready = 1;
    uart_printf(&huart1, "[SMOKE] ready\r\n");
  }

  /* 从 DMA 缓冲区读取 ADC 值（通道0 = 烟雾传感器）*/
  uint16_t val = adc_val[0];
  /* 轮询方式读取（备选，当前未使用）*/
  // uint16_t val=0;
  // HAL_ADC_Start(&hadc1);
  // HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
  //   val=HAL_ADC_GetValue(&hadc1);
  // HAL_ADC_Stop(&hadc1);
  
  /* 滑动平均滤波（窗口大小=5），消除采样噪声 */
  buf[*buf_index] = val;
  (*buf_index)++;
  if (*buf_index >= WINDOW_SIZE)
    *buf_index = 0;  /* 循环索引 */

  uint32_t sum = 0;
  for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
    sum += buf[i];
  }
  *g_adc = (uint16_t)(sum / WINDOW_SIZE);  /* 计算平均值 */

  /* 报警判断：数字IO低电平触发 或 ADC值超过阈值 */
  *g_alarm = (HAL_GPIO_ReadPin(DO_GPIO, DO_GPIO_PIN) == GPIO_PIN_RESET) ? 1 : 0;
  if (*g_adc > ALARM_THRESHOLD)
    *g_alarm = 1;

 //uart_printf(&huart1,"[SMOKE] avg=%d alarm=%d\r\n", *g_adc, *g_alarm);
}

/**
  * @brief  检查烟雾传感器是否已完成预热
  * @return 1=已就绪, 0=预热中
  */
uint8_t smoke_is_ready(void) { return g_ready ? (*g_ready) : 0; }

/**
  * @brief  获取烟雾传感器滤波后的 ADC 值
  * @return ADC平均值（未初始化时返回0）
  */
uint16_t smoke_get_adc(void) { return g_adc ? *g_adc : 0; }

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
    if (!g_adc) return 0.0f;

    uint16_t adc = *g_adc;
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
uint8_t smoke_is_alarmed(void) { return g_alarm ? *g_alarm : 0; }





