#include "lcd.h"
#include "my_uart.h"
#include "ws2812.h"
#include <stdint.h>
#include <string.h>
#include "fan.h"
extern TIM_HandleTypeDef htim4;

/*
 * 通信协议：
 *   单片机→屏幕:  t0.txt="25.5"\xFF\xFF\xFF  （文本指令 + 3字节结束符）
 *   屏幕→单片机:  55 AA CMD DATA... 0D 0A   （二进制帧）
 *
 *   命令码:
 *     04  设置灯带 55 AA 04 <strip_id> R G B 0D 0A  (9 字节, strip_id=1~4)
 *     07  查询灯带 55 AA 07 <strip_id> 0D 0A        (6 字节)
 *     05  风扇转速 55 AA 05 H L 0D 0A              (7 字节大端)
 *
 *   兼容旧版 04 不含 strip_id (8 字节)，自动默认为灯带1
 */

static uint8_t cur_r = 0, cur_g = 0, cur_b = 0;  /* 最近一次收到的 RGB（调试用）*/
static uint8_t cur_strip_id = 0;                /* 最近一次收到的 strip_id */

/* 累积缓冲区：串口屏 printh 逐字节发送，需累积到完整帧再解析 */
static uint8_t lcd_buf[128];
static uint16_t lcd_buf_len = 0;

static void parse_lcd_buf(void)
{
    uint8_t *buf = lcd_buf;
    uint16_t idx = 0;

    while (idx + 5 <= lcd_buf_len) {  /* 最小帧 6 字节: 55 AA 07 ID 0D 0A */
        /* 查找帧头 55 AA */
        if (buf[idx] != 0x55 || buf[idx+1] != 0xAA) {
            idx++;
            continue;
        }

        uint8_t cmd = buf[idx+2];

        /* ---- 设置灯带 RGB: 55 AA 04 [strip_id] R G B 0D 0A ---- */
        if (cmd == 0x04) {
            /* 尝试新格式: 9 字节 (含 strip_id) */
            if (idx + 9 <= lcd_buf_len
                && buf[idx+7] == 0x0D && buf[idx+8] == 0x0A) {
                uint8_t strip_id = buf[idx+3];
                if (strip_id < 1 || strip_id > MAX_STRIPS) { idx++; continue; }
                cur_strip_id = strip_id;
                cur_r = buf[idx+4];
                cur_g = buf[idx+5];
                cur_b = buf[idx+6];
                uart_printf(&huart1, "[LCD] Strip=%d R=%d G=%d B=%d\r\n",
                            cur_strip_id, cur_r, cur_g, cur_b);
                ws2812_strip_set_all(cur_strip_id, cur_r, cur_g, cur_b);
                idx += 9;
                continue;
            }
            /* 兼容旧格式: 8 字节 (无 strip_id，默认为灯带1) */
            if (idx + 8 <= lcd_buf_len
                && buf[idx+6] == 0x0D && buf[idx+7] == 0x0A) {
                cur_strip_id = 1;
                cur_r = buf[idx+3];
                cur_g = buf[idx+4];
                cur_b = buf[idx+5];
                uart_printf(&huart1, "[LCD] Strip=1(old) R=%d G=%d B=%d\r\n",
                            cur_r, cur_g, cur_b);
                ws2812_strip_set_all(1, cur_r, cur_g, cur_b);
                idx += 8;
                continue;
            }
            /* 帧不完整，等待更多数据 */
            break;
        }

        /* ---- 查询灯带状态: 55 AA 07 <strip_id> 0D 0A (6 字节) ---- */
        if (cmd == 0x07) {
            if (idx + 6 > lcd_buf_len) break;
            if (buf[idx+4] != 0x0D || buf[idx+5] != 0x0A) { idx++; continue; }
            uint8_t strip_id = buf[idx+3];
            if (strip_id >= 1 && strip_id <= MAX_STRIPS) {
                cur_strip_id = strip_id;   /* 查询也更新当前选中灯带 */
                uart_printf(&huart1, "[LCD] 查询灯带%d 状态\r\n", strip_id);
                lcd_send_strip_state(strip_id);
            }
            idx += 6;
            continue;
        }

        /* ---- 风扇: 55 AA 05 H L 0D 0A (7 字节大端) ---- */
        if (cmd == 0x05) {
            uint16_t speed = 0;
            uint8_t  parsed = 0;

            if (idx + 7 <= lcd_buf_len
                && buf[idx+5] == 0x0D && buf[idx+6] == 0x0A) {
                speed = ((uint16_t)buf[idx+3] << 8) | buf[idx+4];
                idx += 7;
                parsed = 1;
            }
            else if (idx + 9 <= lcd_buf_len
                     && buf[idx+7] == 0x0D && buf[idx+8] == 0x0A) {
                speed = buf[idx+3] | ((uint16_t)buf[idx+4] << 8);
                idx += 9;
                parsed = 1;
            }

            if (parsed) {
                if (speed > 1000) speed = 1000;
                fan_set(speed);
                continue;
            } else {
                break;
            }
        }

        /* 未知命令，跳过 */
        idx++;
    }

    /* 移除已处理数据，保留未处理的尾部 */
    if (idx > 0 && idx <= lcd_buf_len) {
        uint16_t remaining = lcd_buf_len - idx;
        if (remaining > 0) {
            memmove(lcd_buf, lcd_buf + idx, remaining);
        }
        lcd_buf_len = remaining;
    }
}

void lcd_recv(void) {
    if (uart4_rx_len == 0) return;

    /* 追加到累积缓冲区 */
    if (lcd_buf_len + uart4_rx_len < sizeof(lcd_buf)) {
        memcpy(lcd_buf + lcd_buf_len, uart4_rx_buf, uart4_rx_len);
        lcd_buf_len += uart4_rx_len;
    } else {
        /* 缓冲区溢出，清空重新开始 */
        lcd_buf_len = 0;
    }
    memset(uart4_rx_buf, 0, sizeof(uart4_rx_buf));
    uart4_rx_len = 0;

    /* 解析累积缓冲区中的完整帧 */
    parse_lcd_buf();
}


static void send_end(void)
{
    uart_printf(&huart4, "%c%c%c", 0xFF,0XFF,0XFF);
}

/**
  * @brief  响应查询命令 07：将指定灯带的当前 RGB 状态推送到 HMI
  * @note   STM32 直接设置 HMI 控件值（h0.val=..., n0.val=..., b13.txt=...）
  *         需实测验证：HMI 被外部设置值时是否会触发滑动事件
  */
void lcd_send_strip_state(uint8_t strip_id)
{
    uint8_t r = ws2812_strip_get_r(strip_id);
    uint8_t g = ws2812_strip_get_g(strip_id);
    uint8_t b = ws2812_strip_get_b(strip_id);
    uint8_t is_on = (r > 0 || g > 0 || b > 0);

    uart_printf(&huart1, "[LCD] ->HMI strip%d: R=%d G=%d B=%d %s\r\n",
                strip_id, r, g, b, is_on ? "ON" : "OFF");

    /* 更新 HMI 滑块位置 */
    uart_printf(&huart4, "h0.val=%d", r); send_end();
    uart_printf(&huart4, "h1.val=%d", g); send_end();
    uart_printf(&huart4, "h2.val=%d", b); send_end();

    /* 更新数值显示 */
    uart_printf(&huart4, "n0.val=%d", r); send_end();
    uart_printf(&huart4, "n1.val=%d", g); send_end();
    uart_printf(&huart4, "n2.val=%d", b); send_end();

    /* 更新开关按钮文字: b13.txt="开" or b13.txt="关" */
    /* UTF-8: 开=E5 BC 80, 关=E5 85 B3 */
    uart_printf(&huart4, "b13.txt=\"%s\"",
                is_on ? "\xE5\xBC\x80" : "\xE5\x85\xB3");
    send_end();
}

void lcd_send(void)
{
    uart_printf(&huart4, "t0.txt=\"%.2f\"",DHT11_get_temp());
    send_end();
    uart_printf(&huart4, "t1.txt=\"%.2f\"",DHT11_get_humi());
    send_end();
    uart_printf(&huart4, "t2.txt=\"%.1f\"",(double)PM25_get_ugm3());
    send_end();
    if(smoke_is_ready())
    {
        uart_printf(&huart4, "t12.txt=\"%.0f\"",(double)smoke_get_ppm());
        send_end();
    }
    uart_printf(&huart4, "t13.txt=\"%.2f\"",(double)bh1750_get_lux());
    send_end();
    uart_printf(&huart4, "t14.txt=\"%s\"",ws2812_strip_is_open(cur_strip_id)? "true":"false");
    send_end();
}

