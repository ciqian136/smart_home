#include "lcd.h"
#include "my_uart.h"

void lcd_recv(void)
{
    if(uart4_rx_len==0) return;
    HAL_UART_Transmit(&huart1,(uint8_t*)uart4_rx_buf,uart4_rx_len,HAL_MAX_DELAY);
    memset(uart4_rx_buf, 0, sizeof(uart4_rx_buf)/sizeof(uart4_rx_buf[0]));
    uart4_rx_len=0;
}







