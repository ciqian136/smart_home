#include "lcd.h"

#include "device_state.h"
#include "esp32.h"
#include "face.h"
#include "my_uart.h"

#include <stdint.h>
#include <string.h>

/*
 * UART4 protocol:
 *   STM32 -> HMI text: command + FF FF FF
 *   HMI -> STM32 binary: 55 AA CMD DATA... 0D 0A
 *
 * Binary commands retained for compatibility:
 *   04 ID R G B       set one active strip (1 indoor, 3 outdoor)
 *   04 R G B          old format, defaults to strip 1 indoor
 *   05 H L            set fan speed, big endian, 0..1000
 *   07 ID             query one strip
 *   08                query fan state
 *   09 MODE           set fan mode (0 off, 1 manual, 2 auto)
 *   01 PAGE           page entry notification
 */

#define LCD_RX_BUFFER_SIZE 128U
#define LCD_PAGE_HOME      0U
#define LCD_PAGE_LIGHT     1U
#define LCD_PAGE_FAN       2U

static uint8_t lcd_buf[LCD_RX_BUFFER_SIZE];
static uint16_t lcd_buf_len = 0U;
static uint8_t cur_strip_id = 1U;
static uint8_t current_page = LCD_PAGE_HOME;
static uint8_t last_page_sent = 0xFFU;

static uint8_t strip_cache_valid = 0U;
static uint8_t strip_cache_id = 0U;
static uint8_t strip_cache_r = 0U;
static uint8_t strip_cache_g = 0U;
static uint8_t strip_cache_b = 0U;
static uint8_t strip_cache_open = 0U;
static uint16_t fan_cache_speed = 0xFFFFU;
static uint8_t fan_cache_mode = 0xFFU;
static uint8_t fan_cache_open = 0xFFU;
static uint8_t face_cache_online = 0xFFU;
static uint8_t face_cache_recognized = 0xFFU;
static uint8_t esp_cache_online = 0xFFU;
static uint8_t smoke_cache_valid = 0xFFU;
static uint8_t smoke_cache_alarm = 0xFFU;
static uint8_t pm25_cache_alarm = 0xFFU;
static uint8_t pm25_cache_valid = 0xFFU;
static char temp_cache[24];
static char humi_cache[24];
static char pm25_cache[24];
static char light_cache[24];
static char smoke_value_cache[24];
static char mode_cache[24];
static char auto_mode_cache[24];
static char light_state_cache[24];
static char selected_strip_state_cache[24];

/* UTF-8 is escaped so the source remains compatible with ARMCC V5. */
static const char text_on[] = "\xE5\xBC\x80";
static const char text_off[] = "\xE5\x85\xB3";
static const char text_online[] = "\xE5\x9C\xA8\xE7\xBA\xBF";
static const char text_offline[] = "\xE7\xA6\xBB\xE7\xBA\xBF";
static const char text_recognized[] = "\xE5\xB7\xB2\xE8\xAF\x86\xE5\x88\xAB";
static const char text_waiting[] = "\xE9\xA2\x84\xE7\x83\xAD\xE4\xB8\xAD";
static const char text_error[] = "\xE5\xBC\x82\xE5\xB8" "\xB8";
static const char text_normal[] = "\xE6\xAD\xA3\xE5\xB8" "\xB8";
static const char text_auto[] = "AUTO";
static const char text_manual[] = "MANUAL";
static const char text_stop[] = "STOP";

static void lcd_send_end(void)
{
    uint8_t end[3] = {0xFFU, 0xFFU, 0xFFU};
    HAL_UART_Transmit(&huart4, end, sizeof(end), 100U);
}

static void lcd_send_text(const char *name, const char *value)
{
    uart_printf(&huart4, "%s.txt=\"%s\"", name, value);
    lcd_send_end();
}

static void lcd_send_value(const char *name, uint16_t value)
{
    uart_printf(&huart4, "%s.val=%u", name, value);
    lcd_send_end();
}

static void lcd_send_command(const char *command)
{
    uart_printf(&huart4, "%s", command);
    lcd_send_end();
}

static void lcd_send_text_if_changed(const char *name, const char *value,
                                     char *cache, uint16_t cache_size)
{
    if (strncmp(cache, value, cache_size) == 0) return;
    strncpy(cache, value, cache_size - 1U);
    cache[cache_size - 1U] = '\0';
    lcd_send_text(name, value);
}

static void lcd_send_binary_frame(uint8_t command, const uint8_t *data,
                                  uint8_t data_len)
{
    uint8_t frame[16];
    uint8_t i;
    uint8_t length = (uint8_t)(data_len + 5U);

    if (length > sizeof(frame)) return;
    frame[0] = 0x55U;
    frame[1] = 0xAAU;
    frame[2] = command;
    for (i = 0U; i < data_len; i++) frame[3U + i] = data[i];
    frame[3U + data_len] = 0x0DU;
    frame[4U + data_len] = 0x0AU;
    HAL_UART_Transmit(&huart4, frame, length, 100U);
}

void lcd_send_page_enter(uint8_t page_id)
{
    lcd_send_binary_frame(0x01U, &page_id, 1U);
}

static const char *lcd_fan_mode_text(device_fan_mode_t mode)
{
    if (mode == DEVICE_FAN_OFF) return text_stop;
    if (mode == DEVICE_FAN_AUTO) return text_auto;
    return text_manual;
}

static void lcd_send_strip_controls(uint8_t force)
{
    uint8_t r, g, b;
    uint8_t is_open;

    if (!device_state_strip_is_active(cur_strip_id)) {
        cur_strip_id = DEVICE_STRIP_INDOOR_ID;
    }
    if (!device_state_get_strip_rgb(cur_strip_id, &r, &g, &b)) return;
    is_open = (r != 0U || g != 0U || b != 0U) ? 1U : 0U;
    if (!force && strip_cache_valid && strip_cache_id == cur_strip_id &&
        strip_cache_r == r && strip_cache_g == g && strip_cache_b == b &&
        strip_cache_open == is_open) {
        return;
    }

    strip_cache_valid = 1U;
    strip_cache_id = cur_strip_id;
    strip_cache_r = r;
    strip_cache_g = g;
    strip_cache_b = b;
    strip_cache_open = is_open;
    lcd_send_value("h0", r);
    lcd_send_value("h1", g);
    lcd_send_value("h2", b);
    lcd_send_value("n0", r);
    lcd_send_value("n1", g);
    lcd_send_value("n2", b);
    lcd_send_value("h_cur", cur_strip_id);
    lcd_send_text("b13", is_open ? text_on : text_off);
    lcd_send_command("sys0=0");
}

static void lcd_send_fan_controls(uint8_t force)
{
    uint16_t speed = device_state_get_fan_speed();
    device_fan_mode_t mode = device_state_get_fan_mode();
    uint8_t is_open = speed > 0U ? 1U : 0U;

    if (!force && fan_cache_speed == speed && fan_cache_mode == (uint8_t)mode &&
        fan_cache_open == is_open) return;

    fan_cache_speed = speed;
    fan_cache_mode = (uint8_t)mode;
    fan_cache_open = is_open;
    lcd_send_value("h0", speed);
    lcd_send_value("n0", speed);
    lcd_send_text("b7", is_open ? text_on : text_off);
    lcd_send_text_if_changed("t20", lcd_fan_mode_text(mode), mode_cache,
                             sizeof(mode_cache));
    lcd_send_command("sys1=0");
}

static void lcd_send_status(void)
{
    uint8_t face_online = face_get_online();
    uint8_t recognized = face_is_zeng_detected();
    uint8_t esp_online = esp32_get_online();
    uint8_t smoke_valid = device_state_smoke_valid();
    uint8_t smoke_alarm = device_state_smoke_alarm();
    uint8_t pm25_valid = device_state_pm25_valid();
    uint8_t pm25_alarm = device_state_pm25_alarm();

    if (face_online != face_cache_online || recognized != face_cache_recognized) {
        face_cache_online = face_online;
        face_cache_recognized = recognized;
        lcd_send_text("t16", recognized ? text_recognized :
                      (face_online ? text_online : text_offline));
    }
    if (esp_online != esp_cache_online) {
        esp_cache_online = esp_online;
        lcd_send_text("t17", esp_online ? text_online : text_offline);
    }
    if (smoke_valid != smoke_cache_valid || smoke_alarm != smoke_cache_alarm ||
        pm25_valid != pm25_cache_valid || pm25_alarm != pm25_cache_alarm) {
        smoke_cache_valid = smoke_valid;
        smoke_cache_alarm = smoke_alarm;
        pm25_cache_valid = pm25_valid;
        pm25_cache_alarm = pm25_alarm;
        lcd_send_text("t18", (!smoke_valid || !pm25_valid) ? text_waiting :
                      (smoke_alarm || pm25_alarm ? text_error : text_normal));
    }
    lcd_send_text_if_changed("t19", (device_state_get_auto_enabled() &&
                             !device_state_manual_override_active()) ?
                             text_auto : text_manual, auto_mode_cache,
                             sizeof(auto_mode_cache));
}

static void lcd_send_sensor_values(void)
{
    char text[24];

    if (device_state_temperature_valid()) {
        snprintf(text, sizeof(text), "%.1f", (double)device_state_get_temperature());
        lcd_send_text_if_changed("t0", text, temp_cache, sizeof(temp_cache));
    } else {
        lcd_send_text_if_changed("t0", "--", temp_cache, sizeof(temp_cache));
    }
    if (device_state_humidity_valid()) {
        snprintf(text, sizeof(text), "%.1f", (double)device_state_get_humidity());
        lcd_send_text_if_changed("t1", text, humi_cache, sizeof(humi_cache));
    } else {
        lcd_send_text_if_changed("t1", "--", humi_cache, sizeof(humi_cache));
    }
    if (device_state_pm25_valid()) {
        snprintf(text, sizeof(text), "%.1f", (double)device_state_get_pm25());
        lcd_send_text_if_changed("t2", text, pm25_cache, sizeof(pm25_cache));
    } else {
        lcd_send_text_if_changed("t2", "--", pm25_cache, sizeof(pm25_cache));
    }
    if (device_state_light_valid()) {
        snprintf(text, sizeof(text), "%.1f", (double)device_state_get_light());
        lcd_send_text_if_changed("t13", text, light_cache, sizeof(light_cache));
    } else {
        lcd_send_text_if_changed("t13", "--", light_cache, sizeof(light_cache));
    }
    if (device_state_smoke_valid()) {
        snprintf(text, sizeof(text), "%.0f", (double)device_state_get_smoke());
        lcd_send_text_if_changed("t12", text, smoke_value_cache,
                                 sizeof(smoke_value_cache));
    } else {
        lcd_send_text_if_changed("t12", "--", smoke_value_cache,
                                 sizeof(smoke_value_cache));
    }
    lcd_send_text_if_changed("t14", device_state_any_strip_is_open() ?
                             text_on : text_off, light_state_cache,
                             sizeof(light_state_cache));
}

static void lcd_send_selected_strip_state(void)
{
    lcd_send_text_if_changed("t15", device_state_strip_is_open(cur_strip_id) ?
                             text_on : text_off, selected_strip_state_cache,
                             sizeof(selected_strip_state_cache));
}

static void parse_lcd_buffer(void)
{
    uint16_t index = 0U;

    while (index + 5U <= lcd_buf_len) {
        uint8_t command;
        if (lcd_buf[index] != 0x55U || lcd_buf[index + 1U] != 0xAAU) {
            index++;
            continue;
        }

        command = lcd_buf[index + 2U];
        if (command == 0x01U) {
            if (index + 6U > lcd_buf_len) break;
            if (lcd_buf[index + 4U] != 0x0DU || lcd_buf[index + 5U] != 0x0AU) {
                index++;
                continue;
            }
            if (lcd_buf[index + 3U] <= LCD_PAGE_FAN) {
                current_page = lcd_buf[index + 3U];
                if (current_page == LCD_PAGE_LIGHT) {
                    lcd_send_strip_controls(1U);
                    lcd_send_selected_strip_state();
                }
                if (current_page == LCD_PAGE_FAN) lcd_send_fan_controls(1U);
            }
            index += 6U;
            continue;
        }

        if (command == 0x04U) {
            if (index + 9U <= lcd_buf_len && lcd_buf[index + 7U] == 0x0DU &&
                lcd_buf[index + 8U] == 0x0AU) {
                uint8_t strip_id = lcd_buf[index + 3U];
                if (device_state_strip_is_active(strip_id)) {
                    cur_strip_id = strip_id;
                    device_state_set_strip_rgb(strip_id, lcd_buf[index + 4U],
                                               lcd_buf[index + 5U], lcd_buf[index + 6U],
                                               DEVICE_SOURCE_LCD);
                    lcd_send_strip_controls(1U);
                } else {
                    cur_strip_id = DEVICE_STRIP_INDOOR_ID;
                    lcd_send_strip_controls(1U);
                }
                index += 9U;
                continue;
            }
            if (index + 8U <= lcd_buf_len && lcd_buf[index + 6U] == 0x0DU &&
                lcd_buf[index + 7U] == 0x0AU) {
                cur_strip_id = 1U;
                device_state_set_strip_rgb(1U, lcd_buf[index + 3U],
                                           lcd_buf[index + 4U], lcd_buf[index + 5U],
                                           DEVICE_SOURCE_LCD);
                lcd_send_strip_controls(1U);
                index += 8U;
                continue;
            }
            break;
        }

        if (command == 0x05U) {
            if (index + 7U > lcd_buf_len) break;
            if (lcd_buf[index + 5U] != 0x0DU || lcd_buf[index + 6U] != 0x0AU) {
                index++;
                continue;
            }
            device_state_set_fan((uint16_t)(((uint16_t)lcd_buf[index + 3U] << 8) |
                                            lcd_buf[index + 4U]),
                                 DEVICE_FAN_MANUAL, DEVICE_SOURCE_LCD);
            index += 7U;
            continue;
        }

        if (command == 0x07U) {
            if (index + 6U > lcd_buf_len) break;
            if (lcd_buf[index + 4U] != 0x0DU || lcd_buf[index + 5U] != 0x0AU) {
                index++;
                continue;
            }
            if (device_state_strip_is_active(lcd_buf[index + 3U])) {
                cur_strip_id = lcd_buf[index + 3U];
            } else {
                cur_strip_id = DEVICE_STRIP_INDOOR_ID;
            }
            if (device_state_strip_is_active(cur_strip_id)) {
                lcd_send_strip_controls(1U);
            }
            index += 6U;
            continue;
        }

        if (command == 0x08U) {
            if (index + 5U > lcd_buf_len) break;
            if (lcd_buf[index + 3U] != 0x0DU || lcd_buf[index + 4U] != 0x0AU) {
                index++;
                continue;
            }
            lcd_send_fan_controls(1U);
            index += 5U;
            continue;
        }

        if (command == 0x09U) {
            if (index + 6U > lcd_buf_len) break;
            if (lcd_buf[index + 4U] != 0x0DU || lcd_buf[index + 5U] != 0x0AU) {
                index++;
                continue;
            }
            if (lcd_buf[index + 3U] <= DEVICE_FAN_AUTO) {
                device_state_set_fan_mode((device_fan_mode_t)lcd_buf[index + 3U],
                                          DEVICE_SOURCE_LCD);
            }
            index += 6U;
            continue;
        }

        index++;
    }

    if (index > 0U && index <= lcd_buf_len) {
        uint16_t remaining = lcd_buf_len - index;
        if (remaining > 0U) memmove(lcd_buf, lcd_buf + index, remaining);
        lcd_buf_len = remaining;
    }
}

void lcd_recv(void)
{
    uint8_t rx_frame[sizeof(uart4_rx_buf)];
    uint16_t copy_len;

    copy_len = my_uart4_take_frame(rx_frame, sizeof(rx_frame));
    if (copy_len == 0U) return;
    if ((uint16_t)(lcd_buf_len + copy_len) > LCD_RX_BUFFER_SIZE) {
        lcd_buf_len = 0U;
        if (copy_len > LCD_RX_BUFFER_SIZE) copy_len = LCD_RX_BUFFER_SIZE;
    }
    memcpy(lcd_buf + lcd_buf_len, rx_frame, copy_len);
    lcd_buf_len = (uint16_t)(lcd_buf_len + copy_len);
    parse_lcd_buffer();
}

void lcd_send_strip_state(uint8_t strip_id)
{
    if (!device_state_strip_is_active(strip_id)) return;
    cur_strip_id = strip_id;
    lcd_send_strip_controls(1U);
}

void lcd_send(void)
{
    if (last_page_sent != current_page) {
        last_page_sent = current_page;
        lcd_send_page_enter(current_page);
    }

    lcd_send_sensor_values();
    lcd_send_status();
    if (current_page == LCD_PAGE_LIGHT) {
        lcd_send_strip_controls(0U);
        lcd_send_selected_strip_state();
    } else if (current_page == LCD_PAGE_FAN) {
        lcd_send_fan_controls(0U);
    }
}
