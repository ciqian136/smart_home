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
#if SMOKE_TEST_MODE
#define PREHEAT_TIME 0               /* 测试模式不等待 MQ2 预热 */
#else
#define PREHEAT_TIME 20              /* 传感器预热时间（秒）*/
#endif
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

#define SMOKE_LOG_INTERVAL_MS       1000U
#define SMOKE_ADC_RAIL_LOW          5U
#define SMOKE_ADC_RAIL_HIGH         4090U
#define SMOKE_STUCK_DELTA           2U
#define SMOKE_STUCK_SAMPLES         20U
#define SMOKE_MISMATCH_MARGIN_ADC   100U

static uint16_t buf[WINDOW_SIZE];
static uint8_t buf_index = 0U;      /* 当前窗口索引 */
static uint16_t g_adc = 0U;         /* 滤波后的ADC平均值 */
static uint8_t g_alarm = 0U;        /* 报警状态（0=正常, 1=报警）*/
static uint8_t g_ready = 0U;        /* 传感器预热完成标志 */
static uint32_t g_start = 0U;       /* 初始化时刻时间戳（ms）*/
static uint8_t g_anomaly_flags = 0U;
static uint16_t g_last_raw_adc = 0U;
static uint8_t g_stuck_samples = 0U;
static uint32_t g_last_log_tick = 0U;
#if SMOKE_TEST_MODE
static float g_test_ppm = SMOKE_TEST_NORMAL_MIN_PPM;
static uint32_t g_test_alarm_until = 0U;
#endif

volatile uint8_t smoke_uart1_log_enabled = SMOKE_UART1_LOG_DEFAULT;

#define SMOKE_LOG(...)                                      \
  do {                                                      \
    if (smoke_uart1_log_enabled) uart_printf(&huart1, __VA_ARGS__); \
  } while (0)

#if !SMOKE_TEST_MODE
static uint16_t smoke_absdiff_u16(uint16_t a, uint16_t b)
{
  return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}
#endif

static const char *smoke_anomaly_hint(uint8_t flags)
{
  if (flags & (SMOKE_ANOMALY_ADC_LOW | SMOKE_ANOMALY_ADC_HIGH)) {
    return "check power/wiring/ADC pin";
  }
  if (flags & SMOKE_ANOMALY_ADC_STUCK) {
    return "check sensor power or ADC mux";
  }
  if (flags & SMOKE_ANOMALY_DO_ADC_MISMATCH) {
    return "adjust MQ2 comparator pot/threshold";
  }
  return "ok";
}

void smoke_init(void) {
  /* 记录初始化时间并初始化所有变量 */
  g_start = HAL_GetTick();    /* 记录当前时间作为预热起始 */
  g_ready = 0U;               /* 预热未完成 */
  g_adc = 0U;                 /* ADC初始值 */
  g_alarm = 0U;               /* 初始无报警 */
  g_anomaly_flags = 0U;
  g_last_raw_adc = 0U;
  g_stuck_samples = 0U;
  g_last_log_tick = 0U;
#if SMOKE_TEST_MODE
  g_ready = 1U;
  g_adc = 300U;
  g_test_ppm = SMOKE_TEST_NORMAL_MIN_PPM;
  g_test_alarm_until = 0U;
#endif
  buf_index = 0U;             /* 窗口索引初始为0 */
  for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
    buf[i] = 0;               /* 清空滑动窗口 */
  }

  SMOKE_LOG("[MQ2] init, wait %ds\r\n", PREHEAT_TIME);
}

void smoke_deinit(void) {
  g_ready = 0U;
  SMOKE_LOG("[MQ2] deinit\r\n");
}

void smoke_proc(void) {

  /* 预热阶段：等待传感器稳定 */
  if (!g_ready) {
    if (HAL_GetTick() - g_start < (uint32_t)PREHEAT_TIME * 1000U)
      return;
    g_ready = 1U;
    SMOKE_LOG("[MQ2] ready\r\n");
  }

#if SMOKE_TEST_MODE
  uint32_t now = HAL_GetTick();

  if (g_test_alarm_until != 0U && (int32_t)(now - g_test_alarm_until) < 0) {
    g_test_ppm = SMOKE_TEST_ALARM_PPM;
    g_alarm = 1U;
  } else {
    uint32_t range = (uint32_t)(SMOKE_TEST_NORMAL_MAX_PPM - SMOKE_TEST_NORMAL_MIN_PPM);
    uint32_t phase = (now / 500U) % ((range * 2U) + 1U);

    if (phase > range) {
      phase = (range * 2U) - phase;
    }
    g_test_alarm_until = 0U;
    g_test_ppm = SMOKE_TEST_NORMAL_MIN_PPM + (float)phase;
    g_alarm = 0U;
  }

  g_adc = 300U;
  g_anomaly_flags = 0U;

  if (smoke_uart1_log_enabled && now - g_last_log_tick >= SMOKE_LOG_INTERVAL_MS) {
    g_last_log_tick = now;
    uart_printf(&huart1,
                "[MQ2_TEST] ppm=%.1f alarm=%u hint=%s\r\n",
                (double)g_test_ppm, g_alarm, smoke_anomaly_hint(g_anomaly_flags));
  }
  return;
#else

  /* Smoke sensor: ADC1_IN1 / PA1, sampled on demand. */
  uint16_t val = my_adc_read_channel(ADC_CHANNEL_1);
  uint8_t digital_alarm;
  uint8_t analog_alarm;
  
  /* 滑动平均滤波（窗口大小=5），消除采样噪声 */
  buf[buf_index] = val;
  buf_index++;
  if (buf_index >= WINDOW_SIZE)
    buf_index = 0U;  /* 循环索引 */

  uint32_t sum = 0;
  for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
    sum += buf[i];
  }
  g_adc = (uint16_t)(sum / WINDOW_SIZE);  /* 计算平均值 */

  /* 报警判断：数字IO低电平触发 或 ADC值超过阈值 */
  digital_alarm = (HAL_GPIO_ReadPin(DO_GPIO, DO_GPIO_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
  analog_alarm = (g_adc > ALARM_THRESHOLD) ? 1U : 0U;
  g_alarm = (digital_alarm || analog_alarm) ? 1U : 0U;

  g_anomaly_flags = 0U;
  if (val <= SMOKE_ADC_RAIL_LOW) g_anomaly_flags |= SMOKE_ANOMALY_ADC_LOW;
  if (val >= SMOKE_ADC_RAIL_HIGH) g_anomaly_flags |= SMOKE_ANOMALY_ADC_HIGH;
  if (smoke_absdiff_u16(val, g_last_raw_adc) <= SMOKE_STUCK_DELTA &&
      val > SMOKE_ADC_RAIL_LOW && val < SMOKE_ADC_RAIL_HIGH) {
    if (g_stuck_samples < 255U) g_stuck_samples++;
  } else {
    g_stuck_samples = 0U;
  }
  g_last_raw_adc = val;
  if (g_stuck_samples >= SMOKE_STUCK_SAMPLES) {
    g_anomaly_flags |= SMOKE_ANOMALY_ADC_STUCK;
  }
  if ((digital_alarm && g_adc + SMOKE_MISMATCH_MARGIN_ADC < ALARM_THRESHOLD) ||
      (!digital_alarm && g_adc > ALARM_THRESHOLD + SMOKE_MISMATCH_MARGIN_ADC)) {
    g_anomaly_flags |= SMOKE_ANOMALY_DO_ADC_MISMATCH;
  }

  if (smoke_uart1_log_enabled && HAL_GetTick() - g_last_log_tick >= SMOKE_LOG_INTERVAL_MS) {
    g_last_log_tick = HAL_GetTick();
    uart_printf(&huart1,
                "[MQ2] raw=%u avg=%u ppm=%.1f do_alarm=%u alarm=%u anomaly=0x%02X hint=%s\r\n",
                val, g_adc, (double)smoke_get_ppm(), digital_alarm, g_alarm,
                g_anomaly_flags, smoke_anomaly_hint(g_anomaly_flags));
  }

 // SMOKE_LOG("[MQ2] avg=%u alarm=%u\r\n", g_adc, g_alarm);
#endif
}

/**
  * @brief  检查烟雾传感器是否已完成预热
  * @return 1=已就绪, 0=预热中
  */
uint8_t smoke_is_ready(void) { return g_ready; }

/**
  * @brief  获取烟雾传感器滤波后的 ADC 值
  * @return ADC平均值（未初始化时返回0）
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
#if SMOKE_TEST_MODE
    return g_test_ppm;
#else
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
#endif
}

/**
  * @brief  获取烟雾报警状态
  * @return 1=报警中, 0=正常
  */
uint8_t smoke_is_alarmed(void) { return g_alarm; }

uint8_t smoke_get_anomaly_flags(void) { return g_anomaly_flags; }

#if SMOKE_TEST_MODE
void smoke_test_trigger_alarm(void)
{
  g_ready = 1U;
  g_test_ppm = SMOKE_TEST_ALARM_PPM;
  g_test_alarm_until = HAL_GetTick() + SMOKE_TEST_ALARM_HOLD_MS;
  g_alarm = 1U;
  g_adc = 300U;
  g_anomaly_flags = 0U;
}
#endif





