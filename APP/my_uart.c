#include "my_uart.h"
#include "esp32.h"
#include <stdint.h>

volatile uint8_t uart_rx_byte = 0;
char uart_rx_buf[512] = {0};
volatile uint8_t uart_rx_len = 0;

volatile uint8_t uart2_rx_byte = 0;
char uart2_rx_buf[512] = {0};
volatile uint8_t uart2_rx_len = 0;

volatile uint8_t uart3_rx_byte = 0;
char uart3_rx_buf[125] = {0};
volatile uint8_t uart3_rx_len = 0;

volatile uint8_t uart4_rx_byte = 0;
char uart4_rx_buf[125] = {0};
volatile uint8_t uart4_rx_len = 0;


/**
  * @brief  串口格式化打印函数（类似 printf，支持可变参数）
  *         内部使用 vsnprintf 格式化字符串，再通过 HAL_UART_Transmit 发送
  * @param huart  目标串口句柄
  * @param format 格式化字符串
  * @param ...    可变参数列表
  */
void uart_printf(UART_HandleTypeDef *huart, const char *format, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, format);
  int len = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  if (len > 0)
    HAL_UART_Transmit(huart, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

void my_uart_init(void)
{
  HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)uart_rx_buf,  sizeof(uart_rx_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart2, (uint8_t *)uart2_rx_buf, sizeof(uart2_rx_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart3, (uint8_t *)uart3_rx_buf, sizeof(uart3_rx_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart4, (uint8_t *)uart4_rx_buf, sizeof(uart4_rx_buf));
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == &huart1) {
        uart_rx_len = Size;
        if (Size < sizeof(uart_rx_buf)) {
            uart_rx_buf[Size] = '\0';
        }
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)uart_rx_buf, sizeof(uart_rx_buf));
    } else if (huart == &huart2) {
        uart2_rx_len = Size;
        if (Size < sizeof(uart2_rx_buf)) {
            uart2_rx_buf[Size] = '\0';
        }
        esp32_rx_pending = 1;
        HAL_UARTEx_ReceiveToIdle_IT(&huart2, (uint8_t *)uart2_rx_buf, sizeof(uart2_rx_buf));
    } else if (huart == &huart3) {
        uart3_rx_len = Size;
        if (Size < sizeof(uart3_rx_buf)) {
            uart3_rx_buf[Size] = '\0';
        }
        HAL_UARTEx_ReceiveToIdle_IT(&huart3, (uint8_t *)uart3_rx_buf, sizeof(uart3_rx_buf));
    } else if (huart == &huart4) {
        uart4_rx_len = Size;
        if (Size < sizeof(uart4_rx_buf)) {
            uart4_rx_buf[Size] = '\0';
        }
        HAL_UARTEx_ReceiveToIdle_IT(&huart4, (uint8_t *)uart4_rx_buf, sizeof(uart4_rx_buf));
    }
}


