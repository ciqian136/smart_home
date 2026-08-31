/* PM2.5 传感器配置 */
#include "PM25.h"
#include "my_adc.h"

#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_gpio.h"
#include <stdint.h>

#define LED_GPIO GPIOA
#define LED_GPIO_PIN GPIO_PIN_6
#define PM25_ADC_CHANNEL ADC_CHANNEL_4
#define PM25_CLEAN_AIR_ADC 200.0f
#define PM25_ADC_TO_MV (3300.0f / 4096.0f)
#define PM25_SENSITIVITY_MV_PER_UGM3 5.0f
#define WINDOW_SIZE 5

static uint16_t buf[WINDOW_SIZE];
static uint8_t  buf_index = 0U;     /* 当前窗口索引 */
static uint16_t g_adc = 0U;         /* 滤波后的ADC值 */
static uint8_t  g_ready = 0U;

volatile uint8_t pm25_uart1_log_enabled = PM25_UART1_LOG_DEFAULT;

#define PM25_LOG(...)                                      \
    do {                                                   \
        if (pm25_uart1_log_enabled) uart_printf(&huart1, __VA_ARGS__); \
    } while (0)

static void pm25_delay_timer_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);  // SystemCoreClock 是 72M
    while ((DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}
void PM25_init(void)
{
    pm25_delay_timer_init();

    /* 初始化变量 */
    g_adc = 0U;
    buf_index = 0U;
    g_ready = 0U;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) buf[i] = 0U;

    /* 点亮传感器LED，开始工作 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_SET);  
    PM25_LOG("[PM25] inited\r\n");
}

void PM25_deinit(void)
{
    g_ready = 0U;
    PM25_LOG("[PM25] deinit\r\n");
}


void PM25_proc(void)
{
    /* Settle ADC mux on PA4 while the LED is off; discard avoids PA1/PA4 crosstalk. */
    (void)my_adc_read_channel(PM25_ADC_CHANNEL);

    /* ① LED拉低，开始采样周期 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_RESET);
    delay_us(280);

    /* ② 在280us采样点读取 ADC1_IN4 / PA4，避免连续 DMA 缓冲错相 */
    uint16_t val = my_adc_read_channel(PM25_ADC_CHANNEL);

    /* ③ LED拉高，结束采样 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_SET);
    delay_us(9685);

    /* ④ 滑动平均滤波（窗口大小=5）*/
    buf[buf_index] = val;
    buf_index++;
    if (buf_index >= WINDOW_SIZE) buf_index = 0U;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) sum += buf[i];
    g_adc = (uint16_t)(sum / WINDOW_SIZE);
    g_ready = 1U;

// PM25_LOG("[PM25] avg=%u\r\n", g_adc);

}


uint16_t PM25_get_adc(void) { return g_adc; }

/**
  * @brief  获取 PM2.5 浓度（µg/m³）
  * @note   当前硬件实测 PM2.5 ADC 空气基线约在 200 counts 附近，
  *         低于传感器数据手册常见的 600mV 偏置值。直接按 600mV
  *         换算会让 adc=236~278 的有效读数被截断为 0。
  *         后续实测清洁空气稳定值后，只需要调整 PM25_CLEAN_AIR_ADC。
  * @return PM2.5 浓度（µg/m³），无效时返回 0
  */
float PM25_get_ugm3(void)
{
    uint16_t adc = g_adc;
    if (adc == 0) return 0.0f;

    float signal_adc = (float)adc - PM25_CLEAN_AIR_ADC;
    if (signal_adc <= 0.0f) return 0.0f;

    return (signal_adc * PM25_ADC_TO_MV) / PM25_SENSITIVITY_MV_PER_UGM3;
}

uint8_t PM25_is_ready(void) { return g_ready; }


