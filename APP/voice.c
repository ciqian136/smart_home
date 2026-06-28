#include "voice.h"
#include "my_uart.h"
#include <string.h>
#include "main.h"
void voice_run_send(void)
{
    if(uart3_rx_len==0) return;
    uart_printf(&huart1, "[voice]=%s\r\n",uart3_rx_buf);
    memset(uart3_rx_buf, 0, sizeof(uart3_rx_buf));
    uart3_rx_len=0;
}

void voice_parse(void)
{
	    if(uart3_rx_len==0) return;
				


}








