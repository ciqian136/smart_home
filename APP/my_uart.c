#include "my_uart.h"
#include <stdint.h>

/* ========== 串口中断所需的全局变量 ========== */

/* ---- 串口1（调试串口）相关变量 ---- */

volatile uint8_t uart_rx_byte = 0;   /* 串口1逐字节接收缓存（当前未使用）*/
char uart_rx_buf[256] = {0};         /* 串口1接收数据缓冲区 */
volatile uint8_t uart_rx_len = 0;    /* 串口1已接收数据长度 */

/* ---- 串口2（与 ESP32 通信）相关变量 ---- */
volatile uint8_t uart2_rx_byte = 0;  /* 串口2逐字节接收缓存（当前未使用）*/
char uart2_rx_buf[256] = {0};        /* 串口2接收数据缓冲区 */
volatile uint8_t uart2_rx_len = 0;   /* 串口2已接收数据长度 */

/* ---- 串口3（语音）相关变量 ---- */
volatile uint8_t uart3_rx_byte = 0;   /* 串口3逐字节接收缓存（当前未使用）*/
char uart3_rx_buf[125] = {0};         /* 串口3接收数据缓冲区 */
volatile uint8_t uart3_rx_len = 0;    /* 串口3已接收数据长度 */

/* ---- 串口4（lcd）相关变量 ---- */
volatile uint8_t uart4_rx_byte = 0;   /* 串口4逐字节接收缓存（当前未使用）*/
char uart4_rx_buf[125] = {0};         /* 串口4接收数据缓冲区 */
volatile uint8_t uart4_rx_len = 0;    /* 串口4已接收数据长度 */


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

/**
  * @brief  用户串口初始化 - 使能串口空闲中断接收
  *         串口1用于调试输出，串口2用于与ESP32通信
  */
void my_uart_init(void)
{
 /* 逐字节中断接收方式（旧方案，已注释）*/
 // HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_buf, 1);
 // HAL_UART_Receive_IT(&huart2, (uint8_t *)&uart2_rx_buf, 1);

  /* 空闲中断方式接收（推荐）：收到空闲信号时一次性获取整帧数据 */
  HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)uart_rx_buf, sizeof(uart_rx_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart2, (uint8_t *)uart2_rx_buf, sizeof(uart2_rx_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart3,(uint8_t *)uart3_rx_buf, sizeof(uart3_rx_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart4,(uint8_t *)uart4_rx_buf, sizeof(uart4_rx_buf));
}

/**
  * @brief  串口空闲中断接收回调函数
  *         当串口接收到一帧数据并检测到空闲总线时自动调用
  * @param huart 触发回调的串口句柄
  * @param Size  本次接收到的字节数
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == &huart1) {                    /* 串口1（调试串口）*/
        uart_rx_len = Size;                    /* 记录接收长度 */
        if (Size < sizeof(uart_rx_buf)) {
            uart_rx_buf[Size] = '\0';          /* 字符串结束符 */
        }
        /* 重新开启空闲中断，准备下一次接收 */
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)uart_rx_buf, sizeof(uart_rx_buf));
        
    } else if (huart == &huart2) {             /* 串口2（ESP32通信）*/
        uart2_rx_len = Size;                   /* 记录接收长度 */
        if (Size < sizeof(uart2_rx_buf)) {
            uart2_rx_buf[Size] = '\0';         /* 字符串结束符 */
        }
        /* 重新开启空闲中断 */
        HAL_UARTEx_ReceiveToIdle_IT(&huart2, (uint8_t *)uart2_rx_buf, sizeof(uart2_rx_buf));
    } else if (huart == &huart3) {             /* 串口2（ESP32通信）*/
        uart3_rx_len = Size;                   /* 记录接收长度 */
        // if (Size < sizeof(uart3_rx_buf)) {
        //     uart3_rx_buf[Size] = '\0';         /* 字符串结束符 */
        // }
        /* 重新开启空闲中断 */
        HAL_UARTEx_ReceiveToIdle_IT(&huart3, (uint8_t *)uart3_rx_buf, sizeof(uart3_rx_buf));
    } else if (huart == &huart4) {
        uart4_rx_len = Size;                   /* 记录接收长度 */
        // if (Size < sizeof(uart4_rx_buf)) {
        //     uart4_rx_buf[Size] = '\0';         /* 字符串结束符 */
        // }
        /* 重新开启空闲中断 */
        HAL_UARTEx_ReceiveToIdle_IT(&huart4, (uint8_t *)uart4_rx_buf, sizeof(uart4_rx_buf));    
    }


}

/* 逐字节接收中断回调（旧方案，已注释保留参考）*/
// void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
//   if (huart->Instance == USART1) {
//     if (uart_rx_len < sizeof(uart_rx_buf) - 1) {
//       uart_rx_buf[uart_rx_len++] = uart_rx_byte;
//       uart_rx_buf[uart_rx_len] = '\0';
//     }
//     HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_byte, 1);
//   } else if (huart->Instance == USART2) {
//     if (uart2_rx_len < sizeof(uart2_rx_buf) - 1) {
//       uart2_rx_buf[uart2_rx_len++] = uart2_rx_byte;
//       uart2_rx_buf[uart2_rx_len] = '\0';
//     }
//     HAL_UART_Receive_IT(&huart2, (uint8_t *)&uart2_rx_byte, 1);
//   }
// }


