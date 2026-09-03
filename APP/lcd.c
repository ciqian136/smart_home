#include "lcd.h"
#include "my_uart.h"
#include "ws2812.h"
#include <stdint.h>
#include <string.h>
#include "fan.h"
extern TIM_HandleTypeDef htim4;

/* 调试时改为 1，正常使用保持 0 */
#define LCD_DEBUG 0
#define LCD_DEBUG_INTERVAL_MS 1000U
#define LCD_SENSOR_FLOAT_FMT "%.1f"

#if LCD_DEBUG
#define LCD_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define LCD_DEBUG_PRINTF(...) ((void)0)
#endif

/*
 * 通信协议：
 *   单片机→屏幕:  t0.txt="25.5"\xFF\xFF\xFF
 *   屏幕→单片机 RGB:  55 AA 04 R G B 0D 0A   (8 字节)
 *   屏幕→单片机 风扇: 55 AA 05 H L 0D 0A       (7 字节, H=高字节 L=低字节)
 */

static uint8_t cur_r = 0, cur_g = 0, cur_b = 0;

/* 累积缓冲区：串口屏 printh 逐字节发送，需累积到完整帧再解析 */
static uint8_t lcd_buf[128];
static uint16_t lcd_buf_len = 0;

static void parse_lcd_buf(void)
{
    uint8_t *buf = lcd_buf;
    uint16_t idx = 0;

    while (idx + 7 <= lcd_buf_len) {
        /* 查找帧头 55 AA */
        if (buf[idx] != 0x55 || buf[idx+1] != 0xAA) {
            idx++;
            continue;
        }

        uint8_t cmd = buf[idx+2];

        /* ---- RGB: 55 AA 04 R G B 0D 0A (8 字节) ---- */
        if (cmd == 0x04) {
            if (idx + 8 > lcd_buf_len) break;
            if (buf[idx+6] != 0x0D || buf[idx+7] != 0x0A) {
                idx++;
                continue;
            }
            cur_r = buf[idx+3];
            cur_g = buf[idx+4];
            cur_b = buf[idx+5];
            LCD_DEBUG_PRINTF("[LCD] RGB1 R=%d G=%d B=%d\r\n", cur_r, cur_g, cur_b);
            ws2812_set_all(cur_r, cur_g, cur_b);
            idx += 8;
            continue;
        }
        /* ---- 灯带二 RGB: 55 AA 06 R G B 0D 0A (8 字节) ---- */
        if (cmd == 0x06) {
            if (idx + 8 > lcd_buf_len) break;
            if (buf[idx+6] != 0x0D || buf[idx+7] != 0x0A) {
                idx++;
                continue;
            }
            LCD_DEBUG_PRINTF("[LCD] RGB2 R=%d G=%d B=%d\r\n",
                             buf[idx+3], buf[idx+4], buf[idx+5]);
            // 调用灯带二 API
            ws2812_2_set_all(buf[idx+3], buf[idx+4], buf[idx+5]);
            idx += 8;
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
    uint8_t chunk[32];
    uint16_t read_len;
    uint8_t received = 0U;

    while ((read_len = my_uart_read(&huart4, chunk, sizeof(chunk))) > 0U) {
        received = 1U;
        if (lcd_buf_len + read_len <= sizeof(lcd_buf)) {
            memcpy(lcd_buf + lcd_buf_len, chunk, read_len);
            lcd_buf_len += read_len;
        } else {
            /* 缓冲区溢出，清空重新开始 */
#if LCD_DEBUG
            LCD_DEBUG_PRINTF("[LCD] rx overflow clear read=%u buf=%u\r\n",
                             (unsigned int)read_len,
                             (unsigned int)lcd_buf_len);
#endif
            lcd_buf_len = 0;
            break;
        }
    }

    if (!received) return;

    /* 解析累积缓冲区中的完整帧 */
    parse_lcd_buf();
}


static void send_end(void)
{
    uart_printf(&huart4, "%c%c%c", 0xFF,0XFF,0XFF);
}

void lcd_send(void)
{
#if LCD_DEBUG
    static uint32_t debug_last_tick = 0U;
#endif

    uart_printf(&huart4, "t0.txt=\"" LCD_SENSOR_FLOAT_FMT "\"", (double)DHT11_get_temp());
    send_end();
    uart_printf(&huart4, "t1.txt=\"" LCD_SENSOR_FLOAT_FMT "\"", (double)DHT11_get_humi());
    send_end();
    uart_printf(&huart4, "t2.txt=\"" LCD_SENSOR_FLOAT_FMT "\"", (double)PM25_get_ugm3());
    send_end();
    if(smoke_is_ready())
    {
        uart_printf(&huart4, "t12.txt=\"" LCD_SENSOR_FLOAT_FMT "\"", (double)smoke_get_ppm());
        send_end();
    }
    uart_printf(&huart4, "t13.txt=\"" LCD_SENSOR_FLOAT_FMT "\"", (double)bh1750_get_lux());
    send_end();
    uart_printf(&huart4, "t14.txt=\"%s\"",ws2812_is_open()?"true":"false");
    send_end();

#if LCD_DEBUG
    uint32_t now = HAL_GetTick();
    if (now - debug_last_tick >= LCD_DEBUG_INTERVAL_MS) {
        debug_last_tick = now;
        LCD_DEBUG_PRINTF("[LCD] send temp=%.1f humi=%.1f pm25=%.1f smoke=%.1f lux=%.1f tx4=%u free4=%u\r\n",
                         (double)DHT11_get_temp(),
                         (double)DHT11_get_humi(),
                         (double)PM25_get_ugm3(),
                         (double)smoke_get_ppm(),
                         (double)bh1750_get_lux(),
                         (unsigned int)my_uart_tx_pending(&huart4),
                         (unsigned int)my_uart_tx_free(&huart4));
    }
#endif
}

