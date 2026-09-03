#include "voice.h"
#include "my_uart.h"
#include "ws2812.h"
#include "ws2812_2.h"
#include "fan.h"
#include "DHT11.h"
#include "PM25.h"
#include "BH1750.h"
#include "smoke.h"
#include "lcd.h"
#include <string.h>
#include <stdlib.h>

/* ========== 语音指令解析 ========== */

/* 语音指令关键字（与 ASRPRO 协议一一对应）*/
#define VOICE_CAT_LIGHT   "LIGHT"
#define VOICE_CAT_LIGHT2  "LIGHT2"
#define VOICE_CAT_FAN     "FAN"
#define VOICE_CAT_LED     "LED"
#define VOICE_CAT_QUERY   "QUERY"

/* ========== 语音片段 ID（与 ASRPRO setup() 中定义一致）========== */
#define VID_ZERO     100   /* 零 */
#define VID_ONE      101   /* 一 */
#define VID_TWO      102   /* 二 */
#define VID_THREE    103   /* 三 */
#define VID_FOUR     104   /* 四 */
#define VID_FIVE     105   /* 五 */
#define VID_SIX      106   /* 六 */
#define VID_SEVEN    107   /* 七 */
#define VID_EIGHT    108   /* 八 */
#define VID_NINE     109   /* 九 */
#define VID_TEN      110   /* 十 */
#define VID_HUNDRED  111   /* 百 */
#define VID_THOUSAND 112   /* 千 */
#define VID_POINT    113   /* 点 */
#define VID_DEGREE   114   /* 度 */
#define VID_PERCENT  115   /* 百分之 */
#define VID_UGPM3    116   /* 微克每立方米 */
#define VID_LUX      117   /* 勒克斯 */
#define VID_PRE_TEMP 118   /* 当前温度 */
#define VID_PRE_HUMI 119   /* 当前湿度 */
#define VID_PRE_PM25 120   /* PM2.5浓度 */
#define VID_PRE_LUX  121   /* 当前光照 */
#define VID_NEG      122   /* 零下 */
#define VID_TEMP_HIGH   123   /* 温度偏高 */
#define VID_TEMP_NORMAL 124   /* 温度正常 */

/* ── 温度告警阈值 ── */
#define TEMP_HIGH_THRESHOLD  30   /* 超过此值播报"温度偏高" */

/* 数字-中文片段映射 */
static const uint8_t digit_vid[10] = {
    VID_ZERO, VID_ONE, VID_TWO, VID_THREE, VID_FOUR,
    VID_FIVE, VID_SIX, VID_SEVEN, VID_EIGHT, VID_NINE
};

/**
  * @brief  将一个整数分解为中文语音片段，追加到 PLAYS 命令
  * @note   如 31 → ",103,110,101" (三,十,一)
  */
static void voice_append_number(UART_HandleTypeDef *huart, int num)
{
    if (num < 0) {
        uart_printf(huart, ",%d", VID_NEG);
        num = -num;
    }
    if (num == 0) {
        uart_printf(huart, ",%d", VID_ZERO);
        return;
    }

    int th = num / 1000;
    int hu = (num % 1000) / 100;
    int te = (num % 100) / 10;
    int on = num % 10;

    if (th > 0) {
        uart_printf(huart, ",%d,%d", digit_vid[th], VID_THOUSAND);
        if (hu == 0 && (te > 0 || on > 0))
            uart_printf(huart, ",%d", VID_ZERO);          /* 二千"零"五 */
    }
    if (hu > 0) {
        uart_printf(huart, ",%d,%d", digit_vid[hu], VID_HUNDRED);
        if (te == 0 && on > 0)
            uart_printf(huart, ",%d", VID_ZERO);          /* 一百"零"五 */
    }
    if (te >= 2) {
        uart_printf(huart, ",%d,%d", digit_vid[te], VID_TEN);
    } else if (te == 1) {
        uart_printf(huart, ",%d", VID_TEN);               /* 十(不加一) */
    }
    if (th == 0 && hu == 0 && te == 0) {
        uart_printf(huart, ",%d", digit_vid[on]);         /* 单个数字 0~9 */
    } else if (on > 0) {
        uart_printf(huart, ",%d", digit_vid[on]);
    }
}
#define COLOR_WARM_R    255
#define COLOR_WARM_G    200
#define COLOR_WARM_B    100
#define COLOR_WHITE_R   255
#define COLOR_WHITE_G   255
#define COLOR_WHITE_B   255
#define COLOR_READ_R    178   /* 暖光 70%: 255*70/100 */
#define COLOR_READ_G    140   /* 200*70/100 */
#define COLOR_READ_B    70    /* 100*70/100 */
#define COLOR_NIGHT_R   12    /* 暖光 5%: 255*5/100 */
#define COLOR_NIGHT_G   10    /* 200*5/100 */
#define COLOR_NIGHT_B   5     /* 100*5/100 */


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
            ws2812_set_all(125, 125, 125);
        }
        /* --- LIGHT:OFF --- */
        else if (strcmp(act, "OFF") == 0) {
            ws2812_set_all(0, 0, 0);
        }
        /* --- LIGHT:COLOR:WARM / WHITE / RED / GREEN / BLUE --- */
        else if (strcmp(act, "COLOR") == 0) {
            if (val != NULL) {
                if (strcmp(val, "WARM") == 0)
                    ws2812_set_all(COLOR_WARM_R, COLOR_WARM_G, COLOR_WARM_B);
                else if (strcmp(val, "WHITE") == 0)
                    ws2812_set_all(COLOR_WHITE_R, COLOR_WHITE_G, COLOR_WHITE_B);
                else if (strcmp(val, "RED") == 0)
                    ws2812_set_all(255, 0, 0);
                else if (strcmp(val, "GREEN") == 0)
                    ws2812_set_all(0, 255, 0);
                else if (strcmp(val, "BLUE") == 0)
                    ws2812_set_all(0, 0, 255);
            }
        }
        /* --- LIGHT:MODE:READ / SLEEP / NIGHT --- */
        else if (strcmp(act, "MODE") == 0) {
            if (val != NULL) {
                if (strcmp(val, "READ") == 0)
                    ws2812_set_all(COLOR_READ_R, COLOR_READ_G, COLOR_READ_B);
                else if (strcmp(val, "SLEEP") == 0)
                    ws2812_set_all(0, 0, 0);
                else if (strcmp(val, "NIGHT") == 0)
                    ws2812_set_all(COLOR_NIGHT_R, COLOR_NIGHT_G, COLOR_NIGHT_B);
            }
        }
    }

    /* ================================================ */
    /*  二、灯带2控制  LIGHT2（192 灯珠，PD13）            */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_LIGHT2) == 0) {

        /* --- LIGHT2:ON --- */
        if (strcmp(act, "ON") == 0) {
            ws2812_2_set_all(125, 125, 125);
        }
        /* --- LIGHT2:OFF --- */
        else if (strcmp(act, "OFF") == 0) {
            ws2812_2_set_all(0, 0, 0);
        }
        /* --- LIGHT2:COLOR --- */
        else if (strcmp(act, "COLOR") == 0) {
            if (val != NULL) {
                if (strcmp(val, "WARM") == 0)
                    ws2812_2_set_all(COLOR_WARM_R, COLOR_WARM_G, COLOR_WARM_B);
                else if (strcmp(val, "WHITE") == 0)
                    ws2812_2_set_all(COLOR_WHITE_R, COLOR_WHITE_G, COLOR_WHITE_B);
                else if (strcmp(val, "RED") == 0)
                    ws2812_2_set_all(255, 0, 0);
                else if (strcmp(val, "GREEN") == 0)
                    ws2812_2_set_all(0, 255, 0);
                else if (strcmp(val, "BLUE") == 0)
                    ws2812_2_set_all(0, 0, 255);
            }
        }
        /* --- LIGHT2:MODE --- */
        else if (strcmp(act, "MODE") == 0) {
            if (val != NULL) {
                if (strcmp(val, "READ") == 0)
                    ws2812_2_set_all(COLOR_READ_R, COLOR_READ_G, COLOR_READ_B);
                else if (strcmp(val, "SLEEP") == 0)
                    ws2812_2_set_all(0, 0, 0);
                else if (strcmp(val, "NIGHT") == 0)
                    ws2812_2_set_all(COLOR_NIGHT_R, COLOR_NIGHT_G, COLOR_NIGHT_B);
            }
        }
    }

    /* ================================================ */
    /*  三、风扇控制  FAN                                */
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
            int v = (int)(DHT11_get_temp() + 0.5f);
            if (v < -10) v = -10;
            if (v > 50) v = 50;
            uart_printf(&huart3, "PLAYS:%d", VID_PRE_TEMP);
            voice_append_number(&huart3, v);
            uart_printf(&huart3, ",%d", VID_DEGREE);

            /* 温度范围告警：超过阈值自动播报"温度偏高" */
            if (v > TEMP_HIGH_THRESHOLD)
                uart_printf(&huart3, ",%d", VID_TEMP_HIGH);

            uart_printf(&huart3, "\r\n");
            uart_printf(&huart1, "[QUERY] temp=%.1f => 语音拼接%s\r\n",
                        DHT11_get_temp(),
                        v > TEMP_HIGH_THRESHOLD ? " [偏高]" : "");
            lcd_send();
        }
        /* --- QUERY:HUMI --- */
        else if (strcmp(act, "HUMI") == 0) {
            int v = (int)(DHT11_get_humi() + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            uart_printf(&huart3, "PLAYS:%d,%d", VID_PRE_HUMI, VID_PERCENT);
            voice_append_number(&huart3, v);
            uart_printf(&huart3, "\r\n");
            uart_printf(&huart1, "[QUERY] humi=%.1f => 语音拼接\r\n", DHT11_get_humi());
            lcd_send();
        }
        /* --- QUERY:PM25 --- */
        else if (strcmp(act, "PM25") == 0) {
            int v = (int)(PM25_get_ugm3() + 0.5f);
            if (v < 0) v = 0;
            uart_printf(&huart3, "PLAYS:%d", VID_PRE_PM25);
            voice_append_number(&huart3, v);
            uart_printf(&huart3, ",%d\r\n", VID_UGPM3);
            uart_printf(&huart1, "[QUERY] pm25=%.1f ug/m3 => 语音拼接\r\n", PM25_get_ugm3());
            lcd_send();
        }
        /* --- QUERY:LIGHT --- */
        else if (strcmp(act, "LIGHT") == 0) {
            int v = (int)(bh1750_get_lux() + 0.5f);
            if (v < 0) v = 0;
            uart_printf(&huart3, "PLAYS:%d", VID_PRE_LUX);
            voice_append_number(&huart3, v);
            uart_printf(&huart3, ",%d\r\n", VID_LUX);
            uart_printf(&huart1, "[QUERY] lux=%.0f => 语音拼接\r\n", bh1750_get_lux());
            lcd_send();
        }
        /* --- QUERY:ALL --- */
        else if (strcmp(act, "ALL") == 0) {
            int tv = (int)(DHT11_get_temp() + 0.5f);
            int hv = (int)(DHT11_get_humi() + 0.5f);
            int pv = (int)(PM25_get_ugm3() + 0.5f);
            int lv = (int)(bh1750_get_lux() + 0.5f);

            uart_printf(&huart3, "PLAYS:%d", VID_PRE_TEMP);
            voice_append_number(&huart3, tv);
            uart_printf(&huart3, ",%d", VID_DEGREE);
            if (tv > TEMP_HIGH_THRESHOLD)
                uart_printf(&huart3, ",%d", VID_TEMP_HIGH);
            uart_printf(&huart3, ",%d,%d,%d", VID_PRE_HUMI, VID_PERCENT);
            voice_append_number(&huart3, hv);
            uart_printf(&huart3, ",%d", VID_PRE_PM25);
            voice_append_number(&huart3, pv);
            uart_printf(&huart3, ",%d,%d", VID_UGPM3, VID_PRE_LUX);
            voice_append_number(&huart3, lv);
            uart_printf(&huart3, ",%d\r\n", VID_LUX);
            uart_printf(&huart1, "[QUERY] all t=%d h=%d pm25=%d lux=%d%s\r\n",
                        tv, hv, pv, lv,
                        tv > TEMP_HIGH_THRESHOLD ? " [偏高]" : "");
            lcd_send();
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
