#include "lcd.h"
#include "my_uart.h"
#include "ws2812.h"
#include <stdint.h>
#include <string.h>
#include "fan.h"
extern TIM_HandleTypeDef htim4;

/*
 * 通信协议：
 *   单片机→屏幕:  t0.txt="25.5"\xFF\xFF\xFF
 *   屏幕→单片机 RGB:  55 AA 04 R G B 0D 0A   (8 字节)
 *   屏幕→单片机 风扇: 55 AA 05 H L 0D 0A       (7 字节, H=高字节 L=低字节)
 */

static uint8_t cur_r = 0, cur_g = 0, cur_b = 0;

static void parse_uart4_data(void) {
    if (uart4_rx_len < 7) return;

    uint8_t *buf = (uint8_t *)uart4_rx_buf;
    uint16_t idx = 0;

    while (idx + 7 <= uart4_rx_len) {   /* 至少需要 7 字节才可能是一帧 */
        /* 查找帧头 55 AA */
        if (buf[idx] != 0x55 || buf[idx+1] != 0xAA) {
            idx++;
            continue;
        }

        uint8_t cmd = buf[idx+2];

        /* ---- RGB: 55 AA 04 R G B 0D 0A (8 字节) ---- */
        if (cmd == 0x04) {
            if (idx + 8 > uart4_rx_len) break;  /* 数据不完整，等下次 */
            if (buf[idx+6] != 0x0D || buf[idx+7] != 0x0A) {
                idx++;  /* 帧尾不对，跳过 */
                continue;
            }
            cur_r = buf[idx+3];
            cur_g = buf[idx+4];
            cur_b = buf[idx+5];
            ws2812_set_all(cur_r, cur_g, cur_b);
            uart_printf(&huart1, "[LCD] RGB=%d,%d,%d\r\n", cur_r, cur_g, cur_b);
            idx += 8;  /* 跳过整帧 */
            continue;
        }

        /* ---- 风扇: 两种格式 ---- */
        if (cmd == 0x05) {
            uint16_t speed = 0;
            uint8_t  parsed = 0;

            /* 格式A: 按钮 (printh) — 55 AA 05 H L 0D 0A, 7字节大端 */
            if (idx + 7 <= uart4_rx_len
                && buf[idx+5] == 0x0D && buf[idx+6] == 0x0A) {
                speed = ((uint16_t)buf[idx+3] << 8) | buf[idx+4];
                idx += 7;
                parsed = 1;
            }
            /* 格式B: 滑块 (prints) — 55 AA 05 XX XX 00 00 0D 0A, 9字节小端 */
            else if (idx + 9 <= uart4_rx_len
                     && buf[idx+7] == 0x0D && buf[idx+8] == 0x0A) {
                speed = buf[idx+3] | ((uint16_t)buf[idx+4] << 8);
                idx += 9;
                parsed = 1;
            }

            if (parsed) {
                if (speed > 1000) speed = 1000;
                fan_set(speed);
                uart_printf(&huart1, "[LCD] fan speed=%d\r\n", speed);
                continue;
            } else {
                break;  /* 数据不完整 */
            }
        }

        /* 未知命令，跳过 */
        idx++;
    }
}


void lcd_recv(void) {
    if (uart4_rx_len == 0) return;

    /* 诊断 hex 打印 */
    uart_printf(&huart1, "[lcd] len=%d hex=", uart4_rx_len);
    for (uint8_t i = 0; i < uart4_rx_len && i < 32; i++) {
        uart_printf(&huart1, "%02X ", (uint8_t)uart4_rx_buf[i]);
    }
    uart_printf(&huart1, "\r\n");

    parse_uart4_data();
    memset(uart4_rx_buf, 0, sizeof(uart4_rx_buf));
    uart4_rx_len = 0;
}


static void send_end(void)
{
    uart_printf(&huart4, "%c%c%c", 0xFF,0XFF,0XFF);
}

void lcd_send(void)
{
    uart_printf(&huart4, "t0.txt=\"%.2f\"",DHT11_get_temp());
    send_end();
    uart_printf(&huart4, "t1.txt=\"%.2f\"",DHT11_get_humi());
    send_end();
    uart_printf(&huart4, "t2.txt=\"%.u\"",PM25_get_adc());
    send_end();
    if(smoke_is_ready())
    {
        uart_printf(&huart4, "t12.txt=\"%u\"",smoke_get_adc());
        send_end();
    }
    uart_printf(&huart4, "t13.txt=\"%.2f\"",bh1750_get_lux());
    send_end();
    uart_printf(&huart4, "t14.txt=\"%s\"",ws2812_is_open()?"true":"false");
    send_end();
}

