#ifndef __MY_UART_H__
#define __MY_UART_H__

#include "headfile.h"
#include <stdint.h>

void my_uart_init(void);
void uart_printf(UART_HandleTypeDef *huart, const char *format, ...);

uint16_t my_uart_write(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len);
uint16_t my_uart_tx_pending(UART_HandleTypeDef *huart);
uint16_t my_uart_tx_free(UART_HandleTypeDef *huart);
uint32_t my_uart_get_tx_overflow(UART_HandleTypeDef *huart);
void my_uart_service_tx(void);

uint16_t my_uart_available(UART_HandleTypeDef *huart);
uint16_t my_uart_read(UART_HandleTypeDef *huart, uint8_t *dst, uint16_t capacity);
uint16_t my_uart_read_line(UART_HandleTypeDef *huart, char *dst, uint16_t capacity);
void my_uart_clear_rx(UART_HandleTypeDef *huart);
uint32_t my_uart_get_rx_overflow(UART_HandleTypeDef *huart);

#endif
