#include "DHT11.h"
#include "my_uart.h"
#include <stdint.h>

/* DHT11 引脚与采样参数 */
#define DHT11_PIN    GPIO_PIN_7
#define DHT11_PORT   GPIOA
#define DHT11_SAMPLE_INTERVAL_MS 2000U
#define DHT11_STARTUP_WAIT_MS    2000U
#define DHT11_START_SIGNAL_MS    20U

/* 调试时改为 1，正常使用保持 0 */
#define DHT11_DEBUG 0
#define DHT11_DEBUG_INTERVAL_MS 1000U

#if DHT11_DEBUG
#define DHT11_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define DHT11_DEBUG_PRINTF(...) ((void)0)
#endif

#define DHT11_DQ_LOW()     HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET)
#define DHT11_DQ_HIGH()    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET)
#define DHT11_DQ_READ()    HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN)

typedef enum {
    DHT11_STATE_IDLE = 0,
    DHT11_STATE_START_LOW
} dht11_state_t;

static float g_temp = 0.0f;
static float g_humi = 0.0f;
static uint8_t g_ready = 0U;
static uint32_t g_start_tick = 0U;
static uint32_t g_last_sample_tick = 0U;
static dht11_state_t g_state = DHT11_STATE_IDLE;
static uint32_t g_start_low_tick = 0U;

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

static uint8_t DHT11_WaitWhile(GPIO_PinState state, uint32_t timeout_us)
{
    while (DHT11_DQ_READ() == state) {
        if (timeout_us == 0U) return 1U;
        timeout_us--;
        delay_us(1U);
    }
    return 0U;
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

static void DHT11_StartSignal(void)
{
    DHT11_Mode_Out();
    DHT11_DQ_LOW();
    g_start_low_tick = HAL_GetTick();
    g_state = DHT11_STATE_START_LOW;
}

static uint8_t DHT11_StartSignalElapsed(void)
{
    return ((int32_t)(HAL_GetTick() - g_start_low_tick) >= (int32_t)DHT11_START_SIGNAL_MS) ? 1U : 0U;
}

static uint8_t DHT11_ReadByte(uint8_t *value) {
    if (value == NULL) return 1U;
    *value = 0U;

    for (uint8_t i = 0; i < 8U; i++) {
        // 等待低电平结束（DHT11 会保持 50us 低电平）
        if (DHT11_WaitWhile(GPIO_PIN_RESET, 100U)) {
            DHT11_DEBUG_PRINTF("[DHT11] bit low timeout\r\n");
            return 1U;
        }

        delay_us(40);
        if (DHT11_DQ_READ() == GPIO_PIN_SET) {
            *value |= (uint8_t)(1U << (7U - i));
        }

        if (DHT11_WaitWhile(GPIO_PIN_SET, 100U)) {
            DHT11_DEBUG_PRINTF("[DHT11] bit high timeout\r\n");
            return 1U;
        }
    }
    return 0U;
}

void DHT11_init(void)
{
    delay_timer_init();
    g_temp = 0.0f;
    g_humi = 0.0f;
    g_ready = 0U;
    g_start_tick = HAL_GetTick();
    g_last_sample_tick = 0U;
    g_state = DHT11_STATE_IDLE;
    g_start_low_tick = 0U;
}

uint8_t DHT11_ReadData(float *temp, float *humi) {
    uint8_t buf[5] = {0};

    if (temp == NULL || humi == NULL) return DHT11_READ_ERROR;

    if (g_state == DHT11_STATE_IDLE) {
        DHT11_StartSignal();
        return DHT11_READ_BUSY;
    }

    if (!DHT11_StartSignalElapsed()) {
        return DHT11_READ_BUSY;
    }

    g_state = DHT11_STATE_IDLE;
    DHT11_DQ_HIGH();
    delay_us(30);
    DHT11_Mode_In();

    if (DHT11_WaitWhile(GPIO_PIN_SET, 120U)) return DHT11_READ_ERROR;
    if (DHT11_WaitWhile(GPIO_PIN_RESET, 120U)) return DHT11_READ_ERROR;
    if (DHT11_WaitWhile(GPIO_PIN_SET, 120U)) return DHT11_READ_ERROR;

    for (uint8_t i = 0; i < 5U; i++) {
        if (DHT11_ReadByte(&buf[i])) return DHT11_READ_ERROR;
    }

    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        DHT11_DEBUG_PRINTF("[DHT11] checksum fail\r\n");
        return DHT11_READ_ERROR;
    }

    *humi = buf[0] + (buf[1] & 0x7F) * 0.1f;
    float temp_val = buf[2] + (buf[3] & 0x7F) * 0.1f;
    if (buf[3] & 0x80) temp_val = -temp_val;
    *temp = temp_val;

    return DHT11_READ_OK;
}

void DHT11_proc(void)
{
    uint32_t now_time = HAL_GetTick();
    uint8_t ret;
#if DHT11_DEBUG
    static uint32_t debug_last_tick = 0U;
#endif

    if (now_time - g_start_tick < DHT11_STARTUP_WAIT_MS) {
        return;
    }
    if (g_state == DHT11_STATE_IDLE &&
        g_last_sample_tick != 0U &&
        now_time - g_last_sample_tick < DHT11_SAMPLE_INTERVAL_MS) {
        return;
    }

    ret = DHT11_ReadData(&g_temp, &g_humi);
    if (ret == DHT11_READ_BUSY) {
        return;
    }

    g_last_sample_tick = now_time;
    if (ret == DHT11_READ_OK) {
        g_ready = 1U;
    } else {
        g_ready = 0U;
    }

#if DHT11_DEBUG
    if (now_time - debug_last_tick >= DHT11_DEBUG_INTERVAL_MS) {
        debug_last_tick = now_time;
        DHT11_DEBUG_PRINTF("[DHT11] ret=%u temp=%.1f humi=%.1f ready=%u\r\n",
                           (unsigned int)ret,
                           (double)g_temp,
                           (double)g_humi,
                           (unsigned int)g_ready);
    }
#endif
}

float DHT11_get_temp(void)
{
    return g_temp;
}

float DHT11_get_humi(void)
{
    return g_humi;
}
