#include "lcd.h"
#include "my_uart.h"
#include "ws2812.h"
#include "ws2812_2.h"
#include <stdint.h>
#include <string.h>
#include "fan.h"
extern TIM_HandleTypeDef htim4;

/* 调试时改为 1，正常使用保持 0 */
#define LCD_DEBUG 0
#define LCD_DEBUG_INTERVAL_MS 1000U
#define LCD_DEBUG_RX_HEX_MAX 32U
#define LCD_SENSOR_FLOAT_FMT "%.1f"

#define LCD_HEAD_0 0x55U
#define LCD_HEAD_1 0xAAU
#define LCD_TAIL_0 0x0DU
#define LCD_TAIL_1 0x0AU

#define LCD_STRIP_DEFAULT_ID 1U
#define LCD_FAN_MAX_SPEED 1000U

#if LCD_DEBUG
#define LCD_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define LCD_DEBUG_PRINTF(...) ((void)0)
#endif

/*
 * 通信协议：
 *   单片机→屏幕:  t0.txt="25.5"\xFF\xFF\xFF
 *   屏幕→单片机 RGB旧: 55 AA 04 R G B 0D 0A       (8 字节, 默认灯带1)
 *   屏幕→单片机 RGB新: 55 AA 04 ID R G B 0D 0A    (9 字节, ID=1/0灯带1, ID=2/3灯带2)
 *   屏幕→单片机 RGB2旧:55 AA 06 R G B 0D 0A       (8 字节, 灯带2)
 *   屏幕→单片机 风扇: 55 AA 05 H L 0D 0A          (7 字节, 兼容 H/L 与 prints 小端)
 *   屏幕→单片机 查询: 55 AA 07 ID 0D 0A / 55 AA 08 0D 0A
 */

static uint8_t cur_strip_id = LCD_STRIP_DEFAULT_ID;
static const char lcd_text_on[] = "\xE5\xBC\x80";
static const char lcd_text_off[] = "\xE5\x85\xB3";

/* 累积缓冲区：串口屏 printh 逐字节发送，需累积到完整帧再解析 */
static uint8_t lcd_buf[128];
static uint16_t lcd_buf_len = 0;

static void send_end(void);
static void lcd_send_value(const char *name, uint16_t value);
static void lcd_send_text(const char *name, const char *value);
static void lcd_send_command(const char *command);

#if LCD_DEBUG
static void lcd_debug_print_rx(const uint8_t *data, uint16_t len)
{
    uint16_t print_len = len;

    if (data == NULL || len == 0U) return;
    if (print_len > LCD_DEBUG_RX_HEX_MAX) {
        print_len = LCD_DEBUG_RX_HEX_MAX;
    }

    LCD_DEBUG_PRINTF("[LCD] rx len=%u data=", (unsigned int)len);
    for (uint16_t i = 0U; i < print_len; i++) {
        LCD_DEBUG_PRINTF("%02X%s", data[i], (i + 1U < print_len) ? " " : "");
    }
    if (print_len < len) {
        LCD_DEBUG_PRINTF(" ...");
    }
    LCD_DEBUG_PRINTF(" buf=%u\r\n", (unsigned int)lcd_buf_len);
}

static void lcd_debug_note_hmi_status(uint8_t code)
{
    static uint32_t last_tick = 0U;
    static uint16_t count = 0U;
    static uint8_t last_code = 0U;
    uint32_t now = HAL_GetTick();

    count++;
    last_code = code;
    if (now - last_tick >= LCD_DEBUG_INTERVAL_MS) {
        LCD_DEBUG_PRINTF("[LCD] ignored HMI status count=%u last=0x%02X\r\n",
                         (unsigned int)count,
                         (unsigned int)last_code);
        count = 0U;
        last_tick = now;
    }
}

static void lcd_debug_note_drop(uint8_t byte)
{
    static uint32_t last_tick = 0U;
    static uint16_t count = 0U;
    static uint8_t last_byte = 0U;
    uint32_t now = HAL_GetTick();

    count++;
    last_byte = byte;
    if (now - last_tick >= LCD_DEBUG_INTERVAL_MS) {
        LCD_DEBUG_PRINTF("[LCD] dropped noise count=%u last=0x%02X\r\n",
                         (unsigned int)count,
                         (unsigned int)last_byte);
        count = 0U;
        last_tick = now;
    }
}
#endif

static uint8_t lcd_has_tail(uint16_t idx)
{
    return (idx + 1U < lcd_buf_len &&
            lcd_buf[idx] == LCD_TAIL_0 && lcd_buf[idx + 1U] == LCD_TAIL_1) ? 1U : 0U;
}

static uint8_t lcd_is_hmi_status(uint16_t idx)
{
    return (idx + 3U < lcd_buf_len &&
            lcd_buf[idx + 1U] == 0xFFU && lcd_buf[idx + 2U] == 0xFFU &&
            lcd_buf[idx + 3U] == 0xFFU) ? 1U : 0U;
}

static uint8_t lcd_strip_is_supported(uint8_t strip_id)
{
    return (strip_id == 0U || strip_id == 1U || strip_id == 2U || strip_id == 3U) ? 1U : 0U;
}

static uint8_t lcd_strip_is_first(uint8_t strip_id)
{
    return (strip_id == 0U || strip_id == 1U) ? 1U : 0U;
}

static uint8_t lcd_strip_hmi_id(uint8_t strip_id)
{
    return (strip_id == 0U) ? LCD_STRIP_DEFAULT_ID : strip_id;
}

static void lcd_get_strip_rgb(uint8_t strip_id, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (lcd_strip_is_first(strip_id)) {
        *r = ws2812_get_base_r();
        *g = ws2812_get_base_g();
        *b = ws2812_get_base_b();
    } else {
        *r = ws2812_2_get_base_r();
        *g = ws2812_2_get_base_g();
        *b = ws2812_2_get_base_b();
    }
}

static void lcd_apply_strip_rgb(uint8_t strip_id, uint8_t r, uint8_t g, uint8_t b)
{
    if (!lcd_strip_is_supported(strip_id)) {
        LCD_DEBUG_PRINTF("[LCD] unsupported RGB strip id=%u\r\n", (unsigned int)strip_id);
        return;
    }

    cur_strip_id = lcd_strip_hmi_id(strip_id);
    if (lcd_strip_is_first(strip_id)) {
        ws2812_set_all(r, g, b);
    } else {
        ws2812_2_set_all(r, g, b);
    }

    LCD_DEBUG_PRINTF("[LCD] rx RGB%u ok R=%u G=%u B=%u\r\n",
                     (unsigned int)cur_strip_id,
                     (unsigned int)r,
                     (unsigned int)g,
                     (unsigned int)b);
}

static void lcd_send_strip_state(uint8_t strip_id)
{
    uint8_t r, g, b;
    uint8_t hmi_id;

    if (!lcd_strip_is_supported(strip_id)) {
        LCD_DEBUG_PRINTF("[LCD] query unsupported RGB strip id=%u\r\n", (unsigned int)strip_id);
        return;
    }

    hmi_id = lcd_strip_hmi_id(strip_id);
    cur_strip_id = hmi_id;
    lcd_get_strip_rgb(strip_id, &r, &g, &b);
    lcd_send_value("h_cur", hmi_id);
    lcd_send_value("h0", r);
    lcd_send_value("h1", g);
    lcd_send_value("h2", b);
    lcd_send_value("n0", r);
    lcd_send_value("n1", g);
    lcd_send_value("n2", b);
    lcd_send_text("b13", (r != 0U || g != 0U || b != 0U) ? lcd_text_on : lcd_text_off);
    lcd_send_command("sys0=0");
    LCD_DEBUG_PRINTF("[LCD] query RGB%u R=%u G=%u B=%u\r\n",
                     (unsigned int)hmi_id,
                     (unsigned int)r,
                     (unsigned int)g,
                     (unsigned int)b);
}

static void lcd_send_fan_state(void)
{
    uint16_t speed = fan_get_speed();

    lcd_send_value("h0", speed);
    lcd_send_value("n0", speed);
    lcd_send_text("b7", fan_is_open() ? lcd_text_on : lcd_text_off);
    lcd_send_command("sys1=0");
    LCD_DEBUG_PRINTF("[LCD] query FAN speed=%u open=%u\r\n",
                     (unsigned int)speed,
                     (unsigned int)fan_is_open());
}

static uint16_t lcd_parse_fan_speed(uint8_t byte0, uint8_t byte1,
                                    uint8_t *is_little_endian,
                                    uint8_t *is_clamped)
{
    uint16_t big_endian = ((uint16_t)byte0 << 8) | byte1;
    uint16_t little_endian = byte0 | ((uint16_t)byte1 << 8);

    *is_little_endian = 0U;
    *is_clamped = 0U;

    if (big_endian <= LCD_FAN_MAX_SPEED) {
        return big_endian;
    }

    if (little_endian <= LCD_FAN_MAX_SPEED) {
        *is_little_endian = 1U;
        return little_endian;
    }

    *is_clamped = 1U;
    return LCD_FAN_MAX_SPEED;
}

static void parse_lcd_buf(void)
{
    uint8_t *buf = lcd_buf;
    uint16_t idx = 0;

    while (idx + 3U <= lcd_buf_len) {
        /* 查找帧头 55 AA */
        if (buf[idx] != LCD_HEAD_0 || buf[idx + 1U] != LCD_HEAD_1) {
            if (lcd_is_hmi_status(idx)) {
#if LCD_DEBUG
                lcd_debug_note_hmi_status(buf[idx]);
#endif
                idx += 4U;
            } else {
#if LCD_DEBUG
                lcd_debug_note_drop(buf[idx]);
#endif
                idx++;
            }
            continue;
        }

        uint8_t cmd = buf[idx + 2U];

        /* ---- RGB: 55 AA 04 ID R G B 0D 0A / 55 AA 04 R G B 0D 0A ---- */
        if (cmd == 0x04U) {
            if (idx + 8U > lcd_buf_len) break;

            if (idx + 9U <= lcd_buf_len && lcd_has_tail(idx + 7U)) {
                lcd_apply_strip_rgb(buf[idx + 3U], buf[idx + 4U], buf[idx + 5U], buf[idx + 6U]);
                idx += 9U;
                continue;
            }
            if (lcd_has_tail(idx + 6U)) {
                lcd_apply_strip_rgb(LCD_STRIP_DEFAULT_ID, buf[idx + 3U], buf[idx + 4U], buf[idx + 5U]);
                idx += 8U;
                continue;
            }
            if (idx + 9U > lcd_buf_len && buf[idx + 7U] == LCD_TAIL_0) break;
            LCD_DEBUG_PRINTF("[LCD] bad RGB tail cmd=0x04 len=%u\r\n",
                             (unsigned int)(lcd_buf_len - idx));
            idx++;
            continue;
        }
        /* ---- 灯带二 RGB: 55 AA 06 R G B 0D 0A (8 字节) ---- */
        if (cmd == 0x06U) {
            if (idx + 8U > lcd_buf_len) break;
            if (!lcd_has_tail(idx + 6U)) {
                LCD_DEBUG_PRINTF("[LCD] bad RGB2 tail %02X %02X\r\n",
                                 buf[idx + 6U], buf[idx + 7U]);
                idx++;
                continue;
            }
            lcd_apply_strip_rgb(2U, buf[idx + 3U], buf[idx + 4U], buf[idx + 5U]);
            idx += 8U;
            continue;
        }
        /* ---- 风扇: 55 AA 05 H L 0D 0A (7 字节大端) ---- */
        if (cmd == 0x05U) {
            uint16_t speed = 0;
            uint8_t is_little_endian = 0U;
            uint8_t is_clamped = 0U;
            uint8_t  parsed = 0;

            if (idx + 7U > lcd_buf_len) break;
            if (idx + 7U <= lcd_buf_len && lcd_has_tail(idx + 5U)) {
                speed = lcd_parse_fan_speed(buf[idx + 3U], buf[idx + 4U],
                                            &is_little_endian, &is_clamped);
                idx += 7U;
                parsed = 1U;
            }
            else if (idx + 9U <= lcd_buf_len && lcd_has_tail(idx + 7U)) {
                speed = lcd_parse_fan_speed(buf[idx + 3U], buf[idx + 4U],
                                            &is_little_endian, &is_clamped);
                idx += 9U;
                parsed = 1U;
            }

            if (parsed) {
                LCD_DEBUG_PRINTF("[LCD] rx FAN ok speed=%u%s%s\r\n",
                                 (unsigned int)speed,
                                 is_little_endian ? " le" : " be",
                                 is_clamped ? " clamped" : "");
                fan_set(speed);
                continue;
            }

            LCD_DEBUG_PRINTF("[LCD] bad FAN frame len=%u\r\n", (unsigned int)(lcd_buf_len - idx));
            idx++;
            continue;
        }

        /* ---- 灯带查询: 55 AA 07 ID 0D 0A ---- */
        if (cmd == 0x07U) {
            if (idx + 6U > lcd_buf_len) break;
            if (!lcd_has_tail(idx + 4U)) {
                LCD_DEBUG_PRINTF("[LCD] bad RGB query tail %02X %02X\r\n",
                                 buf[idx + 4U], buf[idx + 5U]);
                idx++;
                continue;
            }
            lcd_send_strip_state(buf[idx + 3U]);
            idx += 6U;
            continue;
        }

        /* ---- 风扇查询: 55 AA 08 0D 0A ---- */
        if (cmd == 0x08U) {
            if (idx + 5U > lcd_buf_len) break;
            if (!lcd_has_tail(idx + 3U)) {
                LCD_DEBUG_PRINTF("[LCD] bad FAN query tail %02X %02X\r\n",
                                 buf[idx + 3U], buf[idx + 4U]);
                idx++;
                continue;
            }
            lcd_send_fan_state();
            idx += 5U;
            continue;
        }

        /* 未知命令，跳过 */
        LCD_DEBUG_PRINTF("[LCD] unknown cmd=0x%02X drop\r\n", cmd);
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
#if LCD_DEBUG
        lcd_debug_print_rx(chunk, read_len);
#endif
    }

    if (!received) return;

    /* 解析累积缓冲区中的完整帧 */
    parse_lcd_buf();
}


static void send_end(void)
{
    uart_printf(&huart4, "%c%c%c", 0xFF,0XFF,0XFF);
}

static void lcd_send_value(const char *name, uint16_t value)
{
    uart_printf(&huart4, "%s.val=%u", name, (unsigned int)value);
    send_end();
}

static void lcd_send_text(const char *name, const char *value)
{
    uart_printf(&huart4, "%s.txt=\"%s\"", name, value);
    send_end();
}

static void lcd_send_command(const char *command)
{
    uart_printf(&huart4, "%s", command);
    send_end();
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
    uart_printf(&huart4, "t2.txt=\"%u\"", (unsigned int)PM25_get_adc());
    send_end();
    if(smoke_is_ready())
    {
        uart_printf(&huart4, "t12.txt=\"%u\"", (unsigned int)smoke_get_adc());
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
        LCD_DEBUG_PRINTF("[LCD] send temp=%.1f humi=%.1f pm25_adc=%u smoke_adc=%u lux=%.1f tx4=%u free4=%u\r\n",
                         (double)DHT11_get_temp(),
                         (double)DHT11_get_humi(),
                         (unsigned int)PM25_get_adc(),
                         (unsigned int)smoke_get_adc(),
                         (double)bh1750_get_lux(),
                         (unsigned int)my_uart_tx_pending(&huart4),
                         (unsigned int)my_uart_tx_free(&huart4));
    }
#endif
}
