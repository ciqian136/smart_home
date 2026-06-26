/*
BH1750 GY-302 光照传感器
I2C1: SDA--PB7, SCL--PB6
连续高分辨率模式，分辨率 1 lux，测量时间 120ms
滑动平均窗口大小 = 5
*/

#include "BH1750.h"
#include "my_uart.h"
#include "stm32f1xx_hal_i2c.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 引用 I2C 句柄 */
extern I2C_HandleTypeDef hi2c1;

/* 引脚和参数宏定义 */
#define WINDOW_SIZE 5                /* 滑动平均滤波窗口大小 */

/* 所有数据均通过堆内存指针访问 */
static float *buf = NULL;            /* 滑动窗口缓冲区 */
static uint8_t *buf_index = NULL;    /* 当前窗口索引 */
static float *g_lux = NULL;          /* 滤波后的光照平均值 */
static float *g_raw_lux = NULL;      /* 原始光照值 */

/**
  * @brief  I2C 软件复位
  */
static void I2C_Reset(I2C_HandleTypeDef *hi2c)
{
    hi2c->Instance->CR1 |= I2C_CR1_SWRST;
    __NOP();
    hi2c->Instance->CR1 &= ~I2C_CR1_SWRST;

    HAL_I2C_DeInit(hi2c);
    HAL_I2C_Init(hi2c);
}

/**
  * @brief  向 BH1750 发送命令
  */
static HAL_StatusTypeDef BH1750_SendCommand(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
    HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(hi2c, BH1750_ADDR, &cmd, 1, 100);
    if (ret != HAL_OK) {
        I2C_Reset(hi2c);
    }
    return ret;
}

/**
  * @brief  读取 BH1750 原始光照值（lux）
  * @return 光照值（lux），读取失败返回 -1.0f
  */
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

/**
  * @brief  光照传感器初始化
  *         动态分配内存、配置传感器为连续高分辨率模式
  */
void bh1750_init(void)
{
    /* 动态分配所有需要的内存 */
    buf = (float *)malloc(WINDOW_SIZE * sizeof(float));
    buf_index = (uint8_t *)malloc(sizeof(uint8_t));
    g_lux = (float *)malloc(sizeof(float));
    g_raw_lux = (float *)malloc(sizeof(float));

    if (!buf || !buf_index || !g_lux || !g_raw_lux) {
        uart_printf(&huart1, "[BH1750] malloc failed!\r\n");
        while (1);
    }

    /* 初始化变量 */
    *g_lux = 0.00f;
    *g_raw_lux = 0.00f;
    *buf_index = 0;
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

    uart_printf(&huart1, "[BH1750] init, wait 180ms\r\n");
}

/**
  * @brief  光照传感器反初始化 - 释放所有动态分配的内存
  */
void bh1750_deinit(void)
{
    if (buf)       { free(buf);       buf = NULL;       }
    if (buf_index) { free(buf_index); buf_index = NULL; }
    if (g_lux)     { free(g_lux);     g_lux = NULL;     }
    if (g_raw_lux) { free(g_raw_lux); g_raw_lux = NULL; }

    uart_printf(&huart1, "[BH1750] deinit, memory released\r\n");
}

/**
  * @brief  光照传感器数据处理函数（周期性调用）
  *         功能：读取原始光照值 → 滑动平均滤波 → 调试打印
  */
void bh1750_proc(void)
{
    /* 读取原始光照值 */
    float val = bh1750_read_raw();
    *g_raw_lux = val;

    /* 如果读取失败，跳过本次滤波 */
    if (val < 0.00f) {
        uart_printf(&huart1, "[BH1750] read failed\r\n");
        return;
    }

    /* 滑动平均滤波（窗口大小=5）*/
    buf[*buf_index] = val;
    (*buf_index)++;
    if (*buf_index >= WINDOW_SIZE) {
        *buf_index = 0;
    }

    float sum = 0.00f;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
        sum += buf[i];
    }
    *g_lux = sum / WINDOW_SIZE;

    /* 调试输出：显示最近5次原始值和滤波后的平均值 */
    //uart_printf(&huart1,"[BH1750] raw=%.2f buf[0]=%.2f [1]=%.2f [2]=%.2f [3]=%.2f [4]=%.2f avg=%.2f\r\n",val,buf[0], buf[1], buf[2], buf[3], buf[4], *g_lux);
}

/**
  * @brief  获取滤波后的光照值
  * @return 光照平均值（lux），未初始化时返回 0.0f
  */
float bh1750_get_lux(void)
{
    return g_lux ? *g_lux : 0.00f;
}

/**
  * @brief  获取原始光照值（未经滤波）
  * @return 原始光照值（lux），未初始化时返回 0.0f
  */
float bh1750_get_raw_lux(void)
{
    return g_raw_lux ? *g_raw_lux : 0.00f;
}
