/* PM2.5 传感器配置 */
#include "PM25.h"
#include "my_adc.h"

#include "stm32f1xx_hal_gpio.h"
#include <stdint.h>
#include <stdlib.h>

#define LED_GPIO GPIOA
#define LED_GPIO_PIN GPIO_PIN_6
#define WINDOW_SIZE 5

extern ADC_HandleTypeDef hadc1;
extern volatile uint16_t adc_val[2];  /* [0]=烟雾, [1]=PM2.5 */

static uint16_t *buf = NULL;
static uint8_t  *buf_index = NULL; /* 当前窗口索引 */
static uint16_t *g_adc = NULL;     /* 滤波后的ADC值 */

static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);  // SystemCoreClock 是 72M
    while ((DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}
void PM25_init(void)
{
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
    delay_us(315);

    /* ② 在280μs时读取ADC值（此时信号最稳定）*/
    uint16_t val = adc_val[1];            
    //delay_us(39);

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


