#include "voice.h"
#include "board_led.h"
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
#define VOICE_RX_LINE_SIZE 125U
#define VOICE_ALARM_REPEAT_MS 10000U
#define VOICE_PM25_ALARM_THRESHOLD_UGM3 35.0f
#define VOICE_PM25_ALARM_CLEAR_UGM3 25.0f

/* 调试时改为 1，正常使用保持 0 */
#define VOICE_DEBUG 1
#define VOICE_DEBUG_INTERVAL_MS 1000U

#if VOICE_DEBUG
#define VOICE_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define VOICE_DEBUG_PRINTF(...) ((void)0)
#endif

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
#define VID_PRE_DUST 120   /* 当前粉尘浓度 */
#define VID_PRE_LUX  121   /* 当前光照 */
#define VID_NEG      122   /* 零下 */
#define VID_TEMP_HIGH   123   /* 温度偏高 */
#define VID_TEMP_NORMAL 124   /* 温度正常 */
#define VID_PRE_SMOKE   125   /* 当前烟雾浓度 */
#define VID_PPM         126   /* 皮皮艾姆 */
#define VID_ALARM_SMOKE 127   /* 警告，烟雾浓度过高 */
#define VID_ALARM_DUST  128   /* 警告，粉尘浓度过高 */
#define VID_FACE_KNOWN  129   /* 识别到人脸 */
#define VID_FACE_UNKNOWN 130  /* 未识别人员 */

/* ── 温度告警阈值 ── */
#define TEMP_HIGH_THRESHOLD  30   /* 超过此值播报"温度偏高" */

/* 数字-中文片段映射 */
static const uint8_t digit_vid[10] = {
    VID_ZERO, VID_ONE, VID_TWO, VID_THREE, VID_FOUR,
    VID_FIVE, VID_SIX, VID_SEVEN, VID_EIGHT, VID_NINE
};

static uint8_t smoke_alarm_latched = 0U;
static uint32_t smoke_alarm_last_tick = 0U;
static uint8_t dust_alarm_latched = 0U;
static uint32_t dust_alarm_last_tick = 0U;

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

static int voice_float_to_fixed1(float value)
{
    if (value >= 0.0f) {
        return (int)(value * 10.0f + 0.5f);
    }
    return (int)(value * 10.0f - 0.5f);
}

static void voice_append_fixed1(UART_HandleTypeDef *huart, float value)
{
    int fixed = voice_float_to_fixed1(value);
    uint8_t negative = 0U;

    if (fixed < 0) {
        negative = 1U;
        fixed = -fixed;
    }

    if (negative) {
        uart_printf(huart, ",%d", VID_NEG);
    }

    voice_append_number(huart, fixed / 10);
    if ((fixed % 10) != 0) {
        uart_printf(huart, ",%d,%d", VID_POINT, digit_vid[fixed % 10]);
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
    char line[VOICE_RX_LINE_SIZE];
    uint16_t line_len = my_uart_read_line(&huart3, line, sizeof(line));
    if (line_len == 0U) return;

    char *buf = line;
    VOICE_DEBUG_PRINTF("[VOICE] %s\r\n", buf);

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
            board_led_on();
        }
        else if (strcmp(act, "OFF") == 0) {
            board_led_off();
        }
        else if (strcmp(act, "TOGGLE") == 0) {
            board_led_toggle();
        }
    }

    /* ================================================ */
    /*  四、环境查询  QUERY                              */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_QUERY) == 0) {

        /* --- QUERY:TEMP --- */
        if (strcmp(act, "TEMP") == 0) {
            float temp = DHT11_get_temp();
            uart_printf(&huart3, "PLAYS:%d", VID_PRE_TEMP);
            voice_append_fixed1(&huart3, temp);
            uart_printf(&huart3, ",%d", VID_DEGREE);

            /* 温度范围告警：超过阈值自动播报"温度偏高" */
            if (temp > TEMP_HIGH_THRESHOLD)
                uart_printf(&huart3, ",%d", VID_TEMP_HIGH);

            uart_printf(&huart3, "\r\n");
            VOICE_DEBUG_PRINTF("[QUERY] temp=%.1f => voice%s\r\n",
                               (double)temp,
                               temp > TEMP_HIGH_THRESHOLD ? " [high]" : "");
            lcd_send();
        }
        /* --- QUERY:HUMI --- */
        else if (strcmp(act, "HUMI") == 0) {
            float humi = DHT11_get_humi();
            if (humi < 0.0f) humi = 0.0f;
            if (humi > 100.0f) humi = 100.0f;
            uart_printf(&huart3, "PLAYS:%d,%d", VID_PRE_HUMI, VID_PERCENT);
            voice_append_fixed1(&huart3, humi);
            uart_printf(&huart3, "\r\n");
            VOICE_DEBUG_PRINTF("[QUERY] humi=%.1f => voice\r\n", (double)humi);
            lcd_send();
        }
        /* --- QUERY:DUST / QUERY:PM25 --- */
        else if (strcmp(act, "DUST") == 0 || strcmp(act, "PM25") == 0) {
            float dust = PM25_get_ugm3();
            if (dust < 0.0f) dust = 0.0f;
            uart_printf(&huart3, "PLAYS:%d", VID_PRE_DUST);
            voice_append_fixed1(&huart3, dust);
            uart_printf(&huart3, ",%d\r\n", VID_UGPM3);
            VOICE_DEBUG_PRINTF("[QUERY] dust=%.1f ug/m3 => voice\r\n", (double)dust);
            lcd_send();
        }
        /* --- QUERY:SMOKE --- */
        else if (strcmp(act, "SMOKE") == 0) {
            float smoke = smoke_get_ppm();
            if (smoke < 0.0f) smoke = 0.0f;
            uart_printf(&huart3, "PLAYS:%d", VID_PRE_SMOKE);
            voice_append_fixed1(&huart3, smoke);
            uart_printf(&huart3, ",%d\r\n", VID_PPM);
            VOICE_DEBUG_PRINTF("[QUERY] smoke=%.1f ppm => voice\r\n", (double)smoke);
            lcd_send();
        }
        /* --- QUERY:LIGHT --- */
        else if (strcmp(act, "LIGHT") == 0) {
            float lux = bh1750_get_lux();
            if (lux < 0.0f) lux = 0.0f;
            uart_printf(&huart3, "PLAYS:%d", VID_PRE_LUX);
            voice_append_fixed1(&huart3, lux);
            uart_printf(&huart3, ",%d\r\n", VID_LUX);
            VOICE_DEBUG_PRINTF("[QUERY] lux=%.1f => voice\r\n", (double)lux);
            lcd_send();
        }
        /* --- QUERY:ALL --- */
        else if (strcmp(act, "ALL") == 0) {
            float tv = DHT11_get_temp();
            float hv = DHT11_get_humi();
            float dv = PM25_get_ugm3();
            float sv = smoke_get_ppm();
            float lv = bh1750_get_lux();

            uart_printf(&huart3, "PLAYS:%d", VID_PRE_TEMP);
            voice_append_fixed1(&huart3, tv);
            uart_printf(&huart3, ",%d", VID_DEGREE);
            if (tv > TEMP_HIGH_THRESHOLD)
                uart_printf(&huart3, ",%d", VID_TEMP_HIGH);
            uart_printf(&huart3, ",%d,%d", VID_PRE_HUMI, VID_PERCENT);
            voice_append_fixed1(&huart3, hv);
            uart_printf(&huart3, ",%d", VID_PRE_DUST);
            voice_append_fixed1(&huart3, dv);
            uart_printf(&huart3, ",%d,%d", VID_UGPM3, VID_PRE_SMOKE);
            voice_append_fixed1(&huart3, sv);
            uart_printf(&huart3, ",%d,%d", VID_PPM, VID_PRE_LUX);
            voice_append_fixed1(&huart3, lv);
            uart_printf(&huart3, ",%d\r\n", VID_LUX);
            VOICE_DEBUG_PRINTF("[QUERY] all t=%.1f h=%.1f dust=%.1f smoke=%.1f lux=%.1f%s\r\n",
                               (double)tv, (double)hv, (double)dv, (double)sv, (double)lv,
                               tv > TEMP_HIGH_THRESHOLD ? " [high]" : "");
            lcd_send();
        }
    }

cleanup:
    (void)line_len;
}

void voice_alert_smoke_over_limit(float ppm)
{
    if (ppm < 0.0f) ppm = 0.0f;
    uart_printf(&huart3, "PLAYS:%d,%d", VID_ALARM_SMOKE, VID_PRE_SMOKE);
    voice_append_fixed1(&huart3, ppm);
    uart_printf(&huart3, ",%d\r\n", VID_PPM);
}

void voice_alert_dust_over_limit(float ugm3)
{
    if (ugm3 < 0.0f) ugm3 = 0.0f;
    uart_printf(&huart3, "PLAYS:%d,%d", VID_ALARM_DUST, VID_PRE_DUST);
    voice_append_fixed1(&huart3, ugm3);
    uart_printf(&huart3, ",%d\r\n", VID_UGPM3);
}

void voice_face_link_event(uint8_t event, uint16_t face_id)
{
    (void)face_id;

    if (event == VOICE_FACE_EVENT_KNOWN) {
        uart_printf(&huart3, "PLAY:%d\r\n", VID_FACE_KNOWN);
    } else if (event == VOICE_FACE_EVENT_UNKNOWN) {
        uart_printf(&huart3, "PLAY:%d\r\n", VID_FACE_UNKNOWN);
    }
}

static void voice_alarm_service(void)
{
    uint32_t now = HAL_GetTick();

    if (smoke_is_ready() && smoke_is_alarmed()) {
        if (!smoke_alarm_latched ||
            now - smoke_alarm_last_tick >= VOICE_ALARM_REPEAT_MS) {
            smoke_alarm_latched = 1U;
            smoke_alarm_last_tick = now;
            voice_alert_smoke_over_limit(smoke_get_ppm());
        }
    } else {
        smoke_alarm_latched = 0U;
        smoke_alarm_last_tick = 0U;
    }

    if (PM25_get_adc() != 0U) {
        float dust = PM25_get_ugm3();
        uint8_t dust_alarm = dust_alarm_latched ?
            (dust > VOICE_PM25_ALARM_CLEAR_UGM3) :
            (dust >= VOICE_PM25_ALARM_THRESHOLD_UGM3);

        if (dust_alarm) {
            if (!dust_alarm_latched ||
                now - dust_alarm_last_tick >= VOICE_ALARM_REPEAT_MS) {
                dust_alarm_latched = 1U;
                dust_alarm_last_tick = now;
                voice_alert_dust_over_limit(dust);
            }
        } else {
            dust_alarm_latched = 0U;
            dust_alarm_last_tick = 0U;
        }
    }
}

/**
  * @brief  语音串口数据透传（调试用）
  * @note   将 ASRPRO 原始数据转发到 UART1 调试串口
  */
void voice_run_send(void)
{
    voice_parse();
    voice_alarm_service();

#if VOICE_DEBUG
    static uint32_t debug_last_tick = 0U;
    uint32_t now = HAL_GetTick();
    if (now - debug_last_tick >= VOICE_DEBUG_INTERVAL_MS) {
        debug_last_tick = now;
        VOICE_DEBUG_PRINTF("[VOICE] rx3=%u tx3=%u free3=%u rxov3=%lu txov3=%lu\r\n",
                           (unsigned int)my_uart_available(&huart3),
                           (unsigned int)my_uart_tx_pending(&huart3),
                           (unsigned int)my_uart_tx_free(&huart3),
                           (unsigned long)my_uart_get_rx_overflow(&huart3),
                           (unsigned long)my_uart_get_tx_overflow(&huart3));
    }
#endif
}
