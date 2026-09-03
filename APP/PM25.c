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

static uint16_t g_adc = 0U;        /* 最近一次ADC值 */
static uint8_t  g_ready = 0U;      /* 最近一次采样是否有效 */

static void delay_timer_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us) {
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        delay_timer_init();
    }
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);  // SystemCoreClock 是 72M
    while ((DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}
void PM25_init(void)
{
    delay_timer_init();

    /* 初始化变量 */
    g_adc = 0U;
    g_ready = 0U;

    /* 点亮传感器LED，开始工作 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_SET);  
    uart_printf(&huart1, "[PM25] inited\r\n");
}

void PM25_deinit(void)
{
    g_adc = 0U;
    g_ready = 0U;
    uart_printf(&huart1, "[PM25] deinit\r\n");
}


void PM25_proc(void)
{
    /* LED 关闭时先切到 PA4 并丢弃一次，降低 ADC 通道切换残留影响。 */
    (void)my_adc_read_channel(PM25_ADC_CHANNEL);

    /* ① LED拉低，开始采样周期 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_RESET);
    delay_us(280);

    /* ② 在280μs时读取ADC值（此时信号最稳定）*/
    uint16_t val = my_adc_read_channel(PM25_ADC_CHANNEL);
    delay_us(40);

    /* ③ LED拉高，结束采样 */
    HAL_GPIO_WritePin(LED_GPIO, LED_GPIO_PIN, GPIO_PIN_SET);

    /* ④ 直接使用本次采样值，避免低频读取下响应滞后。 */
    g_adc = val;
    g_ready = 1U;

//uart_printf(&huart1, "[PM25] adc=%d\r\n", g_adc);

}


uint16_t PM25_get_adc(void) { return g_adc; }

/**
  * @brief  获取 PM2.5 浓度（µg/m³）
  * @note   基于 Sharp GP2Y1014AU0F 传感器特性曲线
  *         当前硬件先按 ADC 空气基线 PM25_CLEAN_AIR_ADC 做零点扣除，
  *         后续实测清洁空气稳定值后只需要调整该宏。
  * @return PM2.5 浓度（µg/m³），无效时返回 0
  */
float PM25_get_ugm3(void)
{
    uint16_t adc = g_adc;
    if (adc == 0) return 0.0f;

    /* 当前硬件空气基线按 ADC counts 配置。 */
    float signal_adc = (float)adc - PM25_CLEAN_AIR_ADC;
    float ugm3 = (signal_adc * PM25_ADC_TO_MV) / PM25_SENSITIVITY_MV_PER_UGM3;

    /* 负值截断为 0 */
    if (ugm3 < 0.0f) ugm3 = 0.0f;

    return ugm3;
}


