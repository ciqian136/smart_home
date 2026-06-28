#include "DHT11.h"
#include "my_uart.h"
#include <stdint.h>

/* 用户定义的引脚宏，根据实际情况调整 */
#define DHT11_PIN    GPIO_PIN_7
#define DHT11_PORT   GPIOA
/* 引脚操作宏 */
#define DHT11_DQ_LOW()     HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET)
#define DHT11_DQ_HIGH()    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET)
#define DHT11_DQ_READ()    HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN)
/* 使用 DWT 的微秒延时，无需额外外设，适用于 Cortex-M3/M4/M7 */
static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);  // SystemCoreClock 是 72M
    while ((DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}
/* GPIO模式切换函数 */
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

/**
  * @brief  读取一个字节（8位）
  * @retval 读到的字节
  */
static uint8_t DHT11_ReadByte(void) {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        value <<= 1;
        // 等待低电平结束（DHT11 会保持 50us 低电平）
    uint32_t timeout = 1000;
    timeout = 1000;
    while (DHT11_DQ_READ() == GPIO_PIN_RESET) {
        if (--timeout == 0) {
            uart_printf(&huart1, "Error: Byte bit low timeout\r\n");
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
            uart_printf(&huart1, "Error: Byte bit high timeout\r\n");
            return 0;
        }
    }
    }
    return value;
}

/**
  * @brief  读取 DHT11 温湿度
  * @param  temp: 温度值存储指针
  * @param  humi: 湿度值存储指针
  * @retval 0: 成功，1: 失败
  */
uint8_t DHT11_ReadData(float *temp, float *humi) {
    uint8_t buf[5] = {0};

    DHT11_Mode_Out();
    DHT11_DQ_LOW();
    HAL_Delay(20);
    DHT11_DQ_HIGH();
    delay_us(30);
    DHT11_Mode_In();

    __disable_irq();

    // 应答低电平
    uint32_t timeout = 1000;
    while (DHT11_DQ_READ() == GPIO_PIN_SET) {
        if (--timeout == 0) { __enable_irq(); return 1; }
    }
    timeout = 1000;
    while (DHT11_DQ_READ() == GPIO_PIN_RESET) {
        if (--timeout == 0) { __enable_irq(); return 1; }
    }
    // 应答高电平结束
    timeout = 1000;
    while (DHT11_DQ_READ() == GPIO_PIN_SET) {
        if (--timeout == 0) { __enable_irq(); return 1; }
    }

    // 只读一次 40 位
    for (int i = 0; i < 5; i++) {
        buf[i] = DHT11_ReadByte();
    }
    __enable_irq();

    // // 调试打印
    // for (int i = 0; i < 5; i++) {
    //     uart_printf(&huart1, "buf[%d] = %d\r\n", i, buf[i]);
    // }

    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        uart_printf(&huart1, "Error: Checksum fail\r\n");
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
    static float temp=0,humi=0;
    if (DHT11_ReadData(&temp, &humi) == 0) {
        uart_printf(&huart1, "temp = %.2f humi = %.2f\r\n", temp, humi);
    } else {
        uart_printf(&huart1, "DHT11 Error!\r\n");
    }
}














