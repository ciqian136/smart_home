#include "DHT11.h"
#include "my_uart.h"
#include <stdint.h>

/* DHT11 引脚与采样参数 */
#define DHT11_PIN    GPIO_PIN_7
#define DHT11_PORT   GPIOA
#define DHT11_SAMPLE_INTERVAL_MS 2000U
#define DHT11_STARTUP_WAIT_MS    2000U
#define DHT11_DQ_LOW()     HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET)
#define DHT11_DQ_HIGH()    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET)
#define DHT11_DQ_READ()    HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN)

static float g_temp = 0.0f;
static float g_humi = 0.0f;
static uint8_t g_ready = 0U;
static uint32_t g_start_tick = 0U;
static uint32_t g_last_sample_tick = 0U;

volatile uint8_t dht11_uart1_log_enabled = DHT11_UART1_LOG_DEFAULT;

#define DHT11_LOG(...)                                      \
    do {                                                    \
        if (dht11_uart1_log_enabled) uart_printf(&huart1, __VA_ARGS__); \
    } while (0)

static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);  // SystemCoreClock 是 72M
    while ((DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}
static void DHT11_Mode_Out(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

static void DHT11_Mode_In(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

static uint8_t DHT11_ReadByte(void) {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        value <<= 1;
        // 等待低电平结束（DHT11 会保持 50us 低电平）
        uint32_t timeout = 1000;
        while (DHT11_DQ_READ() == GPIO_PIN_RESET) {
            if (--timeout == 0) {
                DHT11_LOG("[DHT11] byte bit low timeout\r\n");
                return 0;
            }
        }
        delay_us(40);
        if (DHT11_DQ_READ() == GPIO_PIN_SET) {
            value |= 0x01;
        }
        timeout = 1000;
        while (DHT11_DQ_READ() == GPIO_PIN_SET) {
            if (--timeout == 0) {
                DHT11_LOG("[DHT11] byte bit high timeout\r\n");
                return 0;
            }
        }
    }
    return value;
}

void DHT11_init(void)
{
    g_temp = 0.0f;
    g_humi = 0.0f;
    g_ready = 0U;
    g_start_tick = HAL_GetTick();
    g_last_sample_tick = 0U;
}

uint8_t DHT11_ReadData(float *temp, float *humi) {
    uint8_t buf[5] = {0};

    DHT11_Mode_Out();
    DHT11_DQ_LOW();
    HAL_Delay(20);
    DHT11_DQ_HIGH();
    delay_us(30);
    DHT11_Mode_In();

    uint32_t timeout = 1000;
    while (DHT11_DQ_READ() == GPIO_PIN_SET) {
        if (--timeout == 0) { return 1; }
    }
    timeout = 1000;
    while (DHT11_DQ_READ() == GPIO_PIN_RESET) {
        if (--timeout == 0) { return 1; }
    }
    timeout = 1000;
    while (DHT11_DQ_READ() == GPIO_PIN_SET) {
        if (--timeout == 0) { return 1; }
    }

    for (int i = 0; i < 5; i++) {
        buf[i] = DHT11_ReadByte();
    }

    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        DHT11_LOG("[DHT11] checksum fail\r\n");
        return 1;
    }

    *humi = buf[0] + (buf[1] & 0x7F) * 0.1f;
    float temp_val = buf[2] + (buf[3] & 0x7F) * 0.1f;
    if (buf[3] & 0x80) temp_val = -temp_val;
    *temp = temp_val;

    return 0;
}

void DHT11_proc(void)
{
    uint32_t now_time = HAL_GetTick();

    if (!g_ready) {
        if (now_time - g_start_tick < DHT11_STARTUP_WAIT_MS) {
            return;
        }
    } else if (now_time - g_last_sample_tick < DHT11_SAMPLE_INTERVAL_MS) {
        return;
    }

    g_last_sample_tick = now_time;
    if (DHT11_ReadData(&g_temp, &g_humi) == 0) {
        g_ready = 1U;
        // DHT11_LOG("[DHT11] temp=%.1f humi=%.1f\r\n", (double)g_temp, (double)g_humi);
    } else {
        /* Do not expose the previous sample as current data after a failure. */
        g_ready = 0U;
    }
}

uint8_t DHT11_is_ready(void)
{
    return g_ready;
}

float DHT11_get_temp(void)
{
    return g_temp;
}

float DHT11_get_humi(void)
{
    return g_humi;
}


