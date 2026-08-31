/* BH1750 相关配置 */

#include "BH1750.h"
#include "my_uart.h"
#include "stm32f1xx_hal_i2c.h"
#include <stdint.h>
#include <string.h>

/* 引用 I2C 句柄 */
extern I2C_HandleTypeDef hi2c1;

/* 引脚和参数宏定义 */
#define WINDOW_SIZE 5                /* 滑动平均滤波窗口大小 */

/* 静态内存，避免传感器任务运行期依赖堆分配。 */
static float buf[WINDOW_SIZE];       /* 滑动窗口缓冲区 */
static uint8_t buf_index = 0U;       /* 当前窗口索引 */
static float g_lux = 0.0f;           /* 滤波后的光照平均值 */
static float g_raw_lux = 0.0f;       /* 原始光照值 */
static uint8_t g_ready = 0U;

volatile uint8_t bh1750_uart1_log_enabled = BH1750_UART1_LOG_DEFAULT;

#define BH1750_LOG(...)                                      \
    do {                                                     \
        if (bh1750_uart1_log_enabled) uart_printf(&huart1, __VA_ARGS__); \
    } while (0)

static void I2C_Reset(I2C_HandleTypeDef *hi2c)
{
    hi2c->Instance->CR1 |= I2C_CR1_SWRST;
    __NOP();
    hi2c->Instance->CR1 &= ~I2C_CR1_SWRST;

    HAL_I2C_DeInit(hi2c);
    HAL_I2C_Init(hi2c);
}

static HAL_StatusTypeDef BH1750_SendCommand(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
    HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(hi2c, BH1750_ADDR, &cmd, 1, 100);
    if (ret != HAL_OK) {
        I2C_Reset(hi2c);
    }
    return ret;
}

static float bh1750_read_raw(void)
{
    uint8_t buf[2] = {0};

    HAL_StatusTypeDef ret = HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, buf, 2, 100);
    if (ret != HAL_OK) {
        I2C_Reset(&hi2c1);
        BH1750_SendCommand(&hi2c1, BH1750_CONT_H_MODE);
        HAL_Delay(180);
        return -1.00f;
    }

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    return raw / 1.20f;
}

void bh1750_init(void)
{
    /* 初始化变量 */
    g_lux = 0.00f;
    g_raw_lux = 0.00f;
    buf_index = 0U;
    g_ready = 0U;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
        buf[i] = 0.00f;
    }

    /* 配置传感器 */
    BH1750_SendCommand(&hi2c1, BH1750_POWER_ON);
    HAL_Delay(10);
    BH1750_SendCommand(&hi2c1, BH1750_RESET);
    HAL_Delay(10);
    BH1750_SendCommand(&hi2c1, BH1750_CONT_H_MODE);
    HAL_Delay(180);

    BH1750_LOG("[BH1750] init, wait 180ms\r\n");
}

void bh1750_deinit(void)
{
    g_ready = 0U;

    BH1750_LOG("[BH1750] deinit\r\n");
}

void bh1750_proc(void)
{
    /* 读取原始光照值 */
    float val = bh1750_read_raw();
    g_raw_lux = val;

    /* 如果读取失败，跳过本次滤波 */
    if (val < 0.00f) {
        BH1750_LOG("[BH1750] read failed\r\n");
        g_ready = 0U;
        return;
    }

    /* 滑动平均滤波（窗口大小=5）*/
    buf[buf_index] = val;
    buf_index++;
    if (buf_index >= WINDOW_SIZE) {
        buf_index = 0U;
    }

    float sum = 0.00f;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
        sum += buf[i];
    }
    g_lux = sum / WINDOW_SIZE;
    g_ready = 1U;

    /* Per-sample logging is controlled by bh1750_uart1_log_enabled. */
}

/**
  * @brief  获取滤波后的光照值
  * @return 光照平均值（lux），未初始化时返回 0.0f
  */
float bh1750_get_lux(void)
{
    return g_lux;
}

/**
  * @brief  获取原始光照值（未经滤波）
  * @return 原始光照值（lux），未初始化时返回 0.0f
  */
float bh1750_get_raw_lux(void)
{
    return g_raw_lux;
}

uint8_t bh1750_is_ready(void) { return g_ready; }
