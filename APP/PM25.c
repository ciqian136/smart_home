/* PM2.5 传感器配置 */
#include "PM25.h"
#include "my_adc.h"
#include "my_uart.h"

#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_gpio.h"
#include <stdint.h>

#define LED_GPIO GPIOA
#define LED_GPIO_PIN GPIO_PIN_6
#define PM25_ADC_CHANNEL ADC_CHANNEL_4
#define PM25_ADC_TO_MV (3300.0f / 4096.0f)

/* 调试时改为 1，正常使用保持 0 */
#define PM25_DEBUG 0
#define PM25_DEBUG_INTERVAL_MS 1000U

#if PM25_DEBUG
#define PM25_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define PM25_DEBUG_PRINTF(...) ((void)0)
#endif

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
    PM25_DEBUG_PRINTF("[PM25] inited\r\n");
}

void PM25_deinit(void)
{
    g_adc = 0U;
    g_ready = 0U;
    PM25_DEBUG_PRINTF("[PM25] deinit\r\n");
}


void PM25_proc(void)
{
#if PM25_DEBUG
    static uint32_t debug_last_tick = 0U;
#endif

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

#if PM25_DEBUG
    uint32_t now = HAL_GetTick();
    if (now - debug_last_tick >= PM25_DEBUG_INTERVAL_MS) {
        float mv = (float)g_adc * PM25_ADC_TO_MV;
        debug_last_tick = now;
        PM25_DEBUG_PRINTF("[PM25] adc=%u mv=%.1f ready=%u\r\n",
                          (unsigned int)g_adc,
                          (double)mv,
                          (unsigned int)g_ready);
    }
#endif

}


uint16_t PM25_get_adc(void) { return g_adc; }
