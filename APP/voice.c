#include "voice.h"
#include "my_uart.h"
#include "ws2812.h"
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
#define VOICE_CAT_FAN     "FAN"
#define VOICE_CAT_LED     "LED"
#define VOICE_CAT_QUERY   "QUERY"

#define VOICE_PLAY_CMD_SIZE 256
#define VOICE_REPLY_DELAY_MS 1500U
#define VOICE_MAX_SPOKEN_VALUE 9999

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
#define VID_PRE_SMOKE  125   /* 烟雾浓度 */

/* ── 温度告警阈值 ── */
#define TEMP_HIGH_THRESHOLD  30   /* 超过此值播报"温度偏高" */

/* 数字-中文片段映射 */
static const uint8_t digit_vid[10] = {
    VID_ZERO, VID_ONE, VID_TWO, VID_THREE, VID_FOUR,
    VID_FIVE, VID_SIX, VID_SEVEN, VID_EIGHT, VID_NINE
};

static char voice_pending_cmd[VOICE_PLAY_CMD_SIZE];
static uint32_t voice_pending_tick = 0;
static uint8_t voice_pending_valid = 0;

/**
  * @brief  将一个整数分解为中文语音片段，追加到字符串缓冲区
  * @note   如 31 → ",103,110,101" (三,十,一)
  * @return 写入的字符数（不含 '\0'）
  */
static int voice_append_number_buf(char *buf, int buf_size, int num)
{
    int offset = 0;
    if (buf_size <= 1) return 0;

    if (num < 0) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d", VID_NEG);
        num = -num;
    }
    if (num > VOICE_MAX_SPOKEN_VALUE) num = VOICE_MAX_SPOKEN_VALUE;
    if (num == 0) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d", VID_ZERO);
        return offset;
    }

    int th = num / 1000;
    int hu = (num % 1000) / 100;
    int te = (num % 100) / 10;
    int on = num % 10;

    if (th > 0) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d,%d", digit_vid[th], VID_THOUSAND);
        if (hu == 0 && (te > 0 || on > 0))
            offset += snprintf(buf + offset, buf_size - offset, ",%d", VID_ZERO);
    }
    if (hu > 0) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d,%d", digit_vid[hu], VID_HUNDRED);
        if (te == 0 && on > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",%d", VID_ZERO);
    }
    if (te >= 2) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d,%d", digit_vid[te], VID_TEN);
    } else if (te == 1) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d", VID_TEN);
    }
    if (th == 0 && hu == 0 && te == 0) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d", digit_vid[on]);
    } else if (on > 0) {
        offset += snprintf(buf + offset, buf_size - offset, ",%d", digit_vid[on]);
    }
    return offset;
}

/**
  * @brief  缓存一条完整的 PLAYS 指令，延迟发送到语音模块
  * @note   ASRPRO 先播报命令自带提示音，延迟发送可避免提示音占用播放通道时丢播。
  */
static void voice_plays_send(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(voice_pending_cmd, sizeof(voice_pending_cmd), fmt, ap);
    va_end(ap);

    if (len <= 0 || len >= (int)sizeof(voice_pending_cmd)) {
        voice_pending_valid = 0;
        uart_printf(&huart1, "[VOICE] PLAYS too long, drop\r\n");
        return;
    }

    voice_pending_tick = HAL_GetTick() + VOICE_REPLY_DELAY_MS;
    voice_pending_valid = 1;
}

static void voice_plays_service(void)
{
    if (!voice_pending_valid) return;
    if ((int32_t)(HAL_GetTick() - voice_pending_tick) < 0) return;

    voice_pending_valid = 0;
    uart_printf(&huart3, "%s\r\n", voice_pending_cmd);
}

static int voice_clamp_value(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void voice_take_pending_frame(char *local_buf, uint16_t local_buf_size)
{
    uint16_t copy_len;

    if (local_buf_size == 0) return;

    __disable_irq();
    copy_len = uart3_msg_len;
    if (copy_len >= local_buf_size) {
        copy_len = local_buf_size - 1;
    }
    memcpy(local_buf, uart3_msg_buf, copy_len);
    local_buf[copy_len] = '\0';
    uart3_msg_len = 0;
    uart3_msg_pending = 0;
    __enable_irq();
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
    if (!uart3_msg_pending) return;

    /* ── 立即拷贝到局部缓冲区，防止 UART 接收竞争覆盖 ── */
    char local_buf[128];
    memset(local_buf, 0, sizeof(local_buf));
    voice_take_pending_frame(local_buf, sizeof(local_buf));

    uart_printf(&huart1, "[VOICE] %s\r\n", local_buf);

    /* ── 过滤 ASRPRO 模块的 echo/应答消息（[RX] 开头），不当作语音指令处理 ── */
    if (strncmp(local_buf, "[RX]", 4) == 0) {
        return;
    }

    /* ---- 在局部缓冲区上按 ':' 分割字段 ---- */
    char *cat  = strtok(local_buf, ":\r\n");   /* CATEGORY                     */
    char *tgt  = strtok(NULL,      ":\r\n");   /* TARGET（灯带: "1"~"4"/"ALL"）*/
    char *act  = strtok(NULL,      ":\r\n");   /* ACTION                       */
    char *val  = strtok(NULL,      ":\r\n");   /* VALUE（可选）                 */

    /* ── 调试：打印解析结果 ── */
    uart_printf(&huart1, "[VOICE] cat=%s tgt=%s act=%s val=%s\r\n",
                cat ? cat : "(null)",
                tgt ? tgt : "(null)",
                act ? act : "(null)",
                val ? val : "(null)");

    if (cat == NULL) {
        return;  /* 局部缓冲区，不需要 cleanup */
    }

    /* ================================================
     *  灯带控制  LIGHT
     *
     *  新格式: LIGHT:<target>:<action>[:value]
     *     target = "1"~"4" (单条) 或 "ALL" (全部)
     *     action = ON / OFF / COLOR / MODE
     *  兼容旧格式: LIGHT:<action>[:value]  (无 target，默认灯带1)
     * ================================================ */
    if (strcmp(cat, VOICE_CAT_LIGHT) == 0) {

        /* ---- 解析 target ---- */
        uint8_t strip_id = 0;       /* 0=非法, 255=ALL */

        if (tgt != NULL) {
            /* 判断 tgt 是 target 还是 action（兼容旧格式）*/
            if (strcmp(tgt, "ON") == 0 || strcmp(tgt, "OFF") == 0
                || strcmp(tgt, "COLOR") == 0 || strcmp(tgt, "MODE") == 0) {
                /* 旧格式: LIGHT:ON — tgt 字段实际是 action */
                act = tgt;
                tgt = NULL;
                strip_id = 1;       /* 默认灯带1 */
            } else if (strcmp(tgt, "ALL") == 0) {
                strip_id = 255;     /* ALL 标记 */
            } else {
                strip_id = (uint8_t)atoi(tgt);
                if (strip_id < 1 || strip_id > MAX_STRIPS) return;
            }
        } else {
            /* 无第二个字段，兼容极旧格式 */
            if (act != NULL) {
                strip_id = 1;       /* 默认灯带1 */
            } else {
                return;
            }
        }

        if (act == NULL) return;

        /* ---- 辅助宏：对目标灯带执行操作 ---- */
        #define FOR_EACH_STRIP(id, all) \
            for (uint8_t _i = 0, _done = 0; _i < MAX_STRIPS && !_done; _i++) { \
                uint8_t id; \
                if (all) { id = _i + 1; } \
                else     { id = strip_id; _done = 1; } \
                if (id < 1 || id > MAX_STRIPS) continue;

        #define END_FOR }

        /* --- LIGHT:<t>:ON --- */
        if (strcmp(act, "ON") == 0) {
            FOR_EACH_STRIP(sid, (strip_id == 255))
                ws2812_strip_set_all(sid, 125, 125, 125);
            END_FOR
        }
        /* --- LIGHT:<t>:OFF --- */
        else if (strcmp(act, "OFF") == 0) {
            FOR_EACH_STRIP(sid, (strip_id == 255))
                ws2812_strip_set_all(sid, 0, 0, 0);
            END_FOR
        }
        /* --- LIGHT:<t>:COLOR:<name> --- */
        else if (strcmp(act, "COLOR") == 0 && val != NULL) {
            uint8_t cr = 0, cg = 0, cb = 0;
            if      (strcmp(val, "WARM")  == 0) { cr=COLOR_WARM_R;  cg=COLOR_WARM_G;  cb=COLOR_WARM_B;  }
            else if (strcmp(val, "WHITE") == 0) { cr=COLOR_WHITE_R; cg=COLOR_WHITE_G; cb=COLOR_WHITE_B; }
            else if (strcmp(val, "RED")   == 0) { cr=255; cg=0;   cb=0;   }
            else if (strcmp(val, "GREEN") == 0) { cr=0;   cg=255; cb=0;   }
            else if (strcmp(val, "BLUE")  == 0) { cr=0;   cg=0;   cb=255; }
            else { return; }

            FOR_EACH_STRIP(sid, (strip_id == 255))
                ws2812_strip_set_all(sid, cr, cg, cb);
            END_FOR
        }
        /* --- LIGHT:<t>:MODE:<name> --- */
        else if (strcmp(act, "MODE") == 0 && val != NULL) {
            uint8_t cr = 0, cg = 0, cb = 0;
            if      (strcmp(val, "READ")  == 0) { cr=COLOR_READ_R;  cg=COLOR_READ_G;  cb=COLOR_READ_B;  }
            else if (strcmp(val, "SLEEP") == 0) { cr=0; cg=0; cb=0; }
            else if (strcmp(val, "NIGHT") == 0) { cr=COLOR_NIGHT_R; cg=COLOR_NIGHT_G; cb=COLOR_NIGHT_B; }
            else { return; }

            FOR_EACH_STRIP(sid, (strip_id == 255))
                ws2812_strip_set_all(sid, cr, cg, cb);
            END_FOR
        }

        #undef FOR_EACH_STRIP
        #undef END_FOR
    }

    /* ================================================ */
    /*  三、风扇控制  FAN                                */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_FAN) == 0) {

        /* FAN 没有 TARGET 字段，tgt 实际是 ACTION */
        if (act == NULL && tgt != NULL) {
            act = tgt;
        }

        /* --- FAN:ON --- */
        if (act != NULL && strcmp(act, "ON") == 0) {
            if (!fan_is_open()) {
                fan_set(500);  /* 默认转速 */
            }
        }
        /* --- FAN:OFF --- */
        else if (act != NULL && strcmp(act, "OFF") == 0) {
            fan_set(0);
        }
        /* --- FAN:SPEED:UP / DOWN / 档位 --- */
        else if (act != NULL && strcmp(act, "SPEED") == 0) {
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

        /* LED 没有 TARGET 字段，tgt 实际是 ACTION */
        if (act == NULL && tgt != NULL) {
            act = tgt;
        }

        if (act != NULL && strcmp(act, "ON") == 0) {
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_RESET);
        }
        else if (act != NULL && strcmp(act, "OFF") == 0) {
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);
        }
    }

    /* ================================================ */
    /*  四、环境查询  QUERY                              */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_QUERY) == 0) {

        /* QUERY 没有 TARGET 字段，tgt 实际是 ACTION */
        if (act == NULL && tgt != NULL) {
            act = tgt;
        }

        /* --- QUERY:TEMP --- */
        if (act != NULL && strcmp(act, "TEMP") == 0) {
            int v = (int)(DHT11_get_temp() + 0.5f);
            v = voice_clamp_value(v, -10, 50);

            /* 拼装完整 PLAYS 字符串，一次发送避免 ASRPRO 超时截断 */
            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_TEMP);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_DEGREE);
            if (v > TEMP_HIGH_THRESHOLD)
                off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_TEMP_HIGH);
            voice_plays_send("%s", seg);

            uart_printf(&huart1, "[QUERY] temp=%.1f => 语音拼接%s\r\n",
                        DHT11_get_temp(),
                        v > TEMP_HIGH_THRESHOLD ? " [偏高]" : "");
            lcd_send();
        }
        /* --- QUERY:HUMI --- */
        else if (act != NULL && strcmp(act, "HUMI") == 0) {
            int v = (int)(DHT11_get_humi() + 0.5f);
            v = voice_clamp_value(v, 0, 100);

            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d,%d", VID_PRE_HUMI, VID_PERCENT);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            voice_plays_send("%s", seg);

            uart_printf(&huart1, "[QUERY] humi=%.1f => 语音拼接\r\n", DHT11_get_humi());
            lcd_send();
        }
        /* --- QUERY:PM25 --- */
        else if (act != NULL && strcmp(act, "PM25") == 0) {
            int v = (int)(PM25_get_ugm3() + 0.5f);
            v = voice_clamp_value(v, 0, VOICE_MAX_SPOKEN_VALUE);

            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_PM25);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_UGPM3);
            voice_plays_send("%s", seg);

            uart_printf(&huart1, "[QUERY] pm25=%.1f ug/m3 => 语音拼接\r\n", PM25_get_ugm3());
            lcd_send();
        }
        /* --- QUERY:LIGHT --- */
        else if (act != NULL && strcmp(act, "LIGHT") == 0) {
            int v = (int)(bh1750_get_lux() + 0.5f);
            v = voice_clamp_value(v, 0, VOICE_MAX_SPOKEN_VALUE);

            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_LUX);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_LUX);
            voice_plays_send("%s", seg);

            uart_printf(&huart1, "[QUERY] lux=%.0f => 语音拼接\r\n", bh1750_get_lux());
            lcd_send();
        }
        /* --- QUERY:SMOKE --- */
        else if (act != NULL && strcmp(act, "SMOKE") == 0) {
            int v = (int)(smoke_get_ppm() + 0.5f);
            v = voice_clamp_value(v, 0, VOICE_MAX_SPOKEN_VALUE);

            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_SMOKE);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            voice_plays_send("%s", seg);

            uart_printf(&huart1, "[QUERY] smoke=%.1f ppm => 语音拼接\r\n", smoke_get_ppm());
            lcd_send();
        }
        /* --- QUERY:ALL --- */
        else if (act != NULL && strcmp(act, "ALL") == 0) {
            int tv = voice_clamp_value((int)(DHT11_get_temp() + 0.5f), -10, 50);
            int hv = voice_clamp_value((int)(DHT11_get_humi() + 0.5f), 0, 100);
            int pv = voice_clamp_value((int)(PM25_get_ugm3() + 0.5f), 0, VOICE_MAX_SPOKEN_VALUE);
            int lv = voice_clamp_value((int)(bh1750_get_lux() + 0.5f), 0, VOICE_MAX_SPOKEN_VALUE);
            int sv = voice_clamp_value((int)(smoke_get_ppm() + 0.5f), 0, VOICE_MAX_SPOKEN_VALUE);

            /* 把全部环境信息拼成一条 PLAYS 指令 */
            char seg[256];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_TEMP);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, tv);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_DEGREE);
            if (tv > TEMP_HIGH_THRESHOLD)
                off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_TEMP_HIGH);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d,%d", VID_PRE_HUMI, VID_PERCENT);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, hv);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_PRE_PM25);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, pv);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d,%d", VID_UGPM3, VID_PRE_LUX);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, lv);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_LUX);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_PRE_SMOKE);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, sv);
            voice_plays_send("%s", seg);

            uart_printf(&huart1, "[QUERY] all t=%d h=%d pm25=%d lux=%d smoke=%d%s\r\n",
                        tv, hv, pv, lv, sv,
                        tv > TEMP_HIGH_THRESHOLD ? " [偏高]" : "");
            lcd_send();
        }
    }

    /* 局部缓冲区，全局缓冲已在函数入口清理，直接返回即可 */
    return;
}

/**
  * @brief  语音串口数据透传（调试用）
  * @note   将 ASRPRO 原始数据转发到 UART1 调试串口
  */
void voice_run_send(void)
{
    if (uart3_msg_pending) {
        voice_parse();
    }
    voice_plays_service();
}
