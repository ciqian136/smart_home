#include "voice.h"
#include "my_uart.h"
#include "ws2812.h"
#include "fan.h"
#include "DHT11.h"
#include "PM25.h"
#include "BH1750.h"
#include "smoke.h"
#include <string.h>
#include <stdlib.h>

/* ========== 语音指令解析 ========== */

/* 语音指令关键字（与 ASRPRO 协议一一对应）*/
#define VOICE_CAT_LIGHT   "LIGHT"
#define VOICE_CAT_FAN     "FAN"
#define VOICE_CAT_LED     "LED"
#define VOICE_CAT_QUERY   "QUERY"

/* 默认颜色定义 */
#define COLOR_WARM_R   255
#define COLOR_WARM_G   200
#define COLOR_WARM_B   100
#define COLOR_WHITE_R  255
#define COLOR_WHITE_G  255
#define COLOR_WHITE_B  255

/* 亮度步进 */
#define BRIGHT_STEP  10

/**
  * @brief  解析语音指令并执行
  * @note   协议格式: CATEGORY:ACTION[:VALUE]\r\n
  *         由 schedule 每 10ms 调用一次
  */
void voice_parse(void)
{
    if (uart3_rx_len == 0) return;

    char *buf = (char *)uart3_rx_buf;
    uart_printf(&huart1, "[VOICE] %s\r\n", buf);

    /* ---- 按 ':' 分割字段 ---- */
    char *cat  = strtok(buf,  ":\r\n");   /* CATEGORY */
    char *act  = strtok(NULL, ":\r\n");   /* ACTION   */
    char *val  = strtok(NULL, ":\r\n");   /* VALUE（可选）*/

    if (cat == NULL || act == NULL) {
        goto cleanup;
    }

    /* ================================================ */
    /*  一、灯带控制  LIGHT                              */
    /* ================================================ */
    if (strcmp(cat, VOICE_CAT_LIGHT) == 0) {

        /* --- LIGHT:ON --- */
        if (strcmp(act, "ON") == 0) {
            if (!ws2812_is_open()) {
                /* 恢复基础颜色，默认亮度 50% */
                ws2812_set_all_brightness_level(
                    ws2812_get_base_r(),
                    ws2812_get_base_g(),
                    ws2812_get_base_b(), 50);
            }
        }
        /* --- LIGHT:OFF --- */
        else if (strcmp(act, "OFF") == 0) {
            ws2812_set_all(0, 0, 0);
        }
        /* --- LIGHT:BRIGHT:UP / DOWN / 数值 --- */
        else if (strcmp(act, "BRIGHT") == 0) {
            if (val != NULL) {
                if (strcmp(val, "UP") == 0) {
                    uint8_t cur = ws2812_get_brightness();
                    if (cur == 0) cur = 10;  /* 关机状态下按调亮 → 自动开机 */
                    uint8_t new = (cur + BRIGHT_STEP > 100) ? 100 : cur + BRIGHT_STEP;
                    ws2812_set_brightness(new);
                }
                else if (strcmp(val, "DOWN") == 0) {
                    uint8_t cur = ws2812_get_brightness();
                    uint8_t new = (cur < BRIGHT_STEP) ? 0 : cur - BRIGHT_STEP;
                    ws2812_set_brightness(new);
                }
                else {
                    /* 直接数值，如 LIGHT:BRIGHT:50 */
                    uint8_t level = (uint8_t)atoi(val);
                    ws2812_set_brightness(level);
                }
            }
        }
        /* --- LIGHT:COLOR:WARM / WHITE / RED / GREEN / BLUE --- */
        else if (strcmp(act, "COLOR") == 0) {
            if (val != NULL) {
                uint8_t bright = ws2812_get_brightness();
                if (bright == 0) bright = 50;  /* 关机切色 → 自动开机 */

                if (strcmp(val, "WARM") == 0)
                    ws2812_set_all_brightness_level(COLOR_WARM_R, COLOR_WARM_G, COLOR_WARM_B, bright);
                else if (strcmp(val, "WHITE") == 0)
                    ws2812_set_all_brightness_level(COLOR_WHITE_R, COLOR_WHITE_G, COLOR_WHITE_B, bright);
                else if (strcmp(val, "RED") == 0)
                    ws2812_set_all_brightness_level(255, 0, 0, bright);
                else if (strcmp(val, "GREEN") == 0)
                    ws2812_set_all_brightness_level(0, 255, 0, bright);
                else if (strcmp(val, "BLUE") == 0)
                    ws2812_set_all_brightness_level(0, 0, 255, bright);
            }
        }
        /* --- LIGHT:MODE:READ / SLEEP / NIGHT --- */
        else if (strcmp(act, "MODE") == 0) {
            if (val != NULL) {
                if (strcmp(val, "READ") == 0)
                    ws2812_set_all_brightness_level(COLOR_WARM_R, COLOR_WARM_G, COLOR_WARM_B, 70);
                else if (strcmp(val, "SLEEP") == 0)
                    ws2812_set_all(0, 0, 0);
                else if (strcmp(val, "NIGHT") == 0)
                    ws2812_set_all_brightness_level(COLOR_WARM_R, COLOR_WARM_G, COLOR_WARM_B, 5);
            }
        }
    }

    /* ================================================ */
    /*  二、风扇控制  FAN                                */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_FAN) == 0) {

        /* --- FAN:ON --- */
        if (strcmp(act, "ON") == 0) {
            if (!fan_is_open()) {
                fan_set(500);  /* 默认转速 */
            }
        }
        /* --- FAN:OFF --- */
        else if (strcmp(act, "OFF") == 0) {
            fan_set(0);
        }
        /* --- FAN:SPEED:UP / DOWN / 档位 --- */
        else if (strcmp(act, "SPEED") == 0) {
            if (val != NULL) {
                if (strcmp(val, "UP") == 0) {
                    fan_speed_up();
                }
                else if (strcmp(val, "DOWN") == 0) {
                    fan_speed_down();
                }
                else {
                    /* 数值档位 1~4 */
                    uint8_t gear = (uint8_t)atoi(val);
                    fan_set_gear(gear);
                }
            }
        }
    }

    /* ================================================ */
    /*  三、测试 LED  LED                                */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_LED) == 0) {
        if (strcmp(act, "ON") == 0) {
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_RESET);
        }
        else if (strcmp(act, "OFF") == 0) {
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);
        }
    }

    /* ================================================ */
    /*  四、环境查询  QUERY                              */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_QUERY) == 0) {

        /* --- QUERY:TEMP --- */
        if (strcmp(act, "TEMP") == 0) {
            uart_printf(&huart3, "温度%.1f度\r\n", DHT11_get_temp());
            uart_printf(&huart1, "[QUERY] temp=%.1f\r\n", DHT11_get_temp());
        }
        /* --- QUERY:HUMI --- */
        else if (strcmp(act, "HUMI") == 0) {
            uart_printf(&huart3, "湿度%.1f%%\r\n", DHT11_get_humi());
            uart_printf(&huart1, "[QUERY] humi=%.1f\r\n", DHT11_get_humi());
        }
        /* --- QUERY:PM25 --- */
        else if (strcmp(act, "PM25") == 0) {
            uart_printf(&huart3, "PM2.5浓度%d\r\n", PM25_get_adc());
            uart_printf(&huart1, "[QUERY] pm25=%d\r\n", PM25_get_adc());
        }
        /* --- QUERY:LIGHT --- */
        else if (strcmp(act, "LIGHT") == 0) {
            uart_printf(&huart3, "光照强度%.1f勒克斯\r\n", bh1750_get_lux());
            uart_printf(&huart1, "[QUERY] lux=%.1f\r\n", bh1750_get_lux());
        }
        /* --- QUERY:ALL --- */
        else if (strcmp(act, "ALL") == 0) {
            uart_printf(&huart3, "温度%.1f度 湿度%.1f%% PM2.5浓度%d 光照%.0f勒克斯\r\n",
                        DHT11_get_temp(), DHT11_get_humi(),
                        PM25_get_adc(), bh1750_get_lux());
            uart_printf(&huart1, "[QUERY] all\r\n");
        }
    }

cleanup:
    /* 清除接收缓冲，准备下一次接收 */
    memset(uart3_rx_buf, 0, sizeof(uart3_rx_buf));
    uart3_rx_len = 0;
}

/**
  * @brief  语音串口数据透传（调试用）
  * @note   将 ASRPRO 原始数据转发到 UART1 调试串口
  */
void voice_run_send(void)
{
    if (uart3_rx_len == 0) return;
    voice_parse();
}
