/* BH1750 相关配置 */

#include "BH1750.h"
#include "my_uart.h"
#include "stm32f1xx_hal_i2c.h"
#include <stdint.h>

/* 引用 I2C 句柄 */
extern I2C_HandleTypeDef hi2c1;

#define BH1750_I2C_TIMEOUT_MS      20U
#define BH1750_MEASURE_WAIT_MS    180U

typedef enum {
    BH1750_STATE_READY = 0,
    BH1750_STATE_WAIT_MEASURE
} bh1750_state_t;

static float g_lux = 0.0f;                 /* 最近一次光照值 */
static bh1750_state_t g_state = BH1750_STATE_READY;
static uint32_t g_next_read_tick = 0U;     /* 重新进入测量模式后的最早读取时间 */

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
    HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(hi2c, BH1750_ADDR, &cmd, 1, BH1750_I2C_TIMEOUT_MS);
    if (ret != HAL_OK) {
        I2C_Reset(hi2c);
    }
    return ret;
}

static void bh1750_start_measure_wait(void)
{
    g_state = BH1750_STATE_WAIT_MEASURE;
    g_next_read_tick = HAL_GetTick() + BH1750_MEASURE_WAIT_MS;
}

static void bh1750_recover_i2c(void)
{
    I2C_Reset(&hi2c1);
    (void)BH1750_SendCommand(&hi2c1, BH1750_CONT_H_MODE);
    bh1750_start_measure_wait();
}

static uint8_t bh1750_wait_elapsed(void)
{
    return ((int32_t)(HAL_GetTick() - g_next_read_tick) >= 0) ? 1U : 0U;
}

static float bh1750_read_raw(void)
{
    uint8_t buf[2] = {0};

    HAL_StatusTypeDef ret = HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, buf, 2, BH1750_I2C_TIMEOUT_MS);
    if (ret != HAL_OK) {
        bh1750_recover_i2c();
        return -1.00f;
    }

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    return raw / 1.20f;
}

void bh1750_init(void)
{
    /* 初始化变量 */
    g_lux = 0.00f;
    g_state = BH1750_STATE_READY;
    g_next_read_tick = 0U;

    /* 配置传感器 */
    BH1750_SendCommand(&hi2c1, BH1750_POWER_ON);
    HAL_Delay(10);
    BH1750_SendCommand(&hi2c1, BH1750_RESET);
    HAL_Delay(10);
    BH1750_SendCommand(&hi2c1, BH1750_CONT_H_MODE);
    bh1750_start_measure_wait();

    uart_printf(&huart1, "[BH1750] init, measure wait nonblock\r\n");
}

void bh1750_deinit(void)
{
    g_lux = 0.00f;
    g_state = BH1750_STATE_READY;
    g_next_read_tick = 0U;

    uart_printf(&huart1, "[BH1750] deinit\r\n");
}

void bh1750_proc(void)
{
    if (g_state == BH1750_STATE_WAIT_MEASURE) {
        if (!bh1750_wait_elapsed()) {
            return;
        }
        g_state = BH1750_STATE_READY;
    }

    /* 读取原始光照值 */
    float val = bh1750_read_raw();

    /* 如果读取失败，保留上一次有效值并标记无效。 */
    if (val < 0.00f) {
        uart_printf(&huart1, "[BH1750] read failed\r\n");
        return;
    }

    g_lux = val;

    //uart_printf(&huart1,"[BH1750] lux=%.2f\r\n", g_lux);
}

/**
  * @brief  获取最近一次光照值
  * @return 光照值（lux），未初始化时返回 0.0f
  */
float bh1750_get_lux(void)
{
    return g_lux;
}
