#ifndef __MY_UART_H__
#define __MY_UART_H__
#include "headfile.h"

/** @brief 用户串口初始化（使能空闲中断接收）*/
void my_uart_init(void);
/** @brief 串口格式化打印（类printf，支持可变参数）*/
void uart_printf(UART_HandleTypeDef *huart, const char *format, ...);

/* ====== 串口2（与ESP32通信）全局变量 ====== */
//extern volatile uint8_t uart2_rx_byte;  /* 逐字节接收缓存（未使用） */
extern char uart2_rx_buf[512];          /* 接收数据缓冲区 */
extern volatile uint8_t uart2_rx_len;   /* 已接收数据长度 */

/* ====== 串口1（调试串口）全局变量 ====== */
//extern volatile uint8_t uart_rx_byte;  /* 逐字节接收缓存（未使用）*/
extern char uart_rx_buf[512];           /* 接收数据缓冲区 */
extern volatile uint8_t uart_rx_len;    /* 已接收数据长度 */

#endif


