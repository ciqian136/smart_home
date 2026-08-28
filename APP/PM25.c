/* PM2.5 传感器配置 */
#include "PM25.h"
#include "my_adc.h"

#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_gpio.h"
#include <stdint.h>
#include <stdlib.h>

#define LED_GPIO GPIOA
#define LED_GPIO_PIN GPIO_PIN_6
#define PM25_ADC_CHANNEL ADC_CHANNEL_4
#define PM25_CLEAN_AIR_ADC 200.0f
#define PM25_ADC_TO_MV (3300.0f / 4096.0f)
#define PM25_SENSITIVITY_MV_PER_UGM3 5.0f
#define WINDOW_SIZE 5

static uint16_t *buf = NULL;
static uint8_t  *buf_index = NULL; /* 当前窗口索引 */
static uint16_t *g_adc = NULL;     /* 滤波后的ADC值 */

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

    /* 动态分配滑动窗口缓冲区 */
    buf = (uint16_t *)malloc(WINDOW_SIZE * sizeof(uint16_t));
    buf_index = (uint8_t *)malloc(sizeof(uint8_t));
    g_adc = (uint16_t *)malloc(sizeof(uint16_t));

    /* 内存分配失败检查 */
    if (!buf || !buf_index || !g_adc) {
        uart_printf(&huart1, "[PM25] 内存分配失败!\r\n");
        while (1);
    }

    /* 初始化变量 */
    *g_adc = 0;
    *buf_index = 0;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) buf[i] = 0;

    /* 点亮传感器LED，开始工作 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_SET);  
    uart_printf(&huart1, "[PM25] inited\r\n");
}

void PM25_deinit(void)
{
    if (buf)       { free(buf);       buf = NULL;       }
    if (buf_index) { free(buf_index); buf_index = NULL; }
    if (g_adc)     { free(g_adc);     g_adc = NULL;     }
    uart_printf(&huart1, "[PM25] deinit\r\n");
}


void PM25_proc(void)
{
    /* ① LED拉低，开始采样周期 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_RESET);
    delay_us(280);

    /* ② 在280us采样点读取 ADC1_IN4 / PA4，避免连续 DMA 缓冲错相 */
    uint16_t val = my_adc_read_channel(PM25_ADC_CHANNEL);

    /* ③ LED拉高，结束采样 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_SET);
    delay_us(9685);

    /* ④ 滑动平均滤波（窗口大小=5）*/
    buf[*buf_index] = val;
    (*buf_index)++;
    if (*buf_index >= WINDOW_SIZE) *buf_index = 0;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) sum += buf[i];
    *g_adc = (uint16_t)(sum / WINDOW_SIZE);

//uart_printf(&huart1, "[PM25] avg=%d\r\n", *g_adc);

}


uint16_t PM25_get_adc(void) { return g_adc ? *g_adc : 0; }

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
    if (!g_adc) return 0.0f;

    uint16_t adc = *g_adc;
    if (adc == 0) return 0.0f;

    float signal_adc = (float)adc - PM25_CLEAN_AIR_ADC;
    if (signal_adc <= 0.0f) return 0.0f;

    return (signal_adc * PM25_ADC_TO_MV) / PM25_SENSITIVITY_MV_PER_UGM3;
}


