#include "voice.h"
#include "my_uart.h"
#include "device_state.h"
#include "lcd.h"
#include "face.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

/* ========== 语音指令解析 ========== */

/* 语音指令关键字（与 ASRPRO 协议一一对应）*/
#define VOICE_CAT_LIGHT   "LIGHT"
#define VOICE_CAT_FAN     "FAN"
#define VOICE_CAT_LED     "LED"
#define VOICE_CAT_QUERY   "QUERY"
#define VOICE_CAT_AUTO    "AUTO"

#define VOICE_PLAY_CMD_SIZE 256
#define VOICE_REPLY_DELAY_MS 1500U
#define VOICE_MAX_SPOKEN_VALUE 9999
#define VOICE_MAX_FRAMES_PER_SERVICE 4U
#define FACE_WELCOME_PLAY_ID 10003U
#define FACE_WELCOME_COOLDOWN_MS 5000U

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
#define VID_PRE_PM25 120   /* 粉尘浓度 */
#define VID_PRE_LUX  121   /* 当前光照 */
#define VID_NEG      122   /* 零下 */
#define VID_TEMP_HIGH   123   /* 温度偏高 */
#define VID_TEMP_NORMAL 124   /* 温度正常 */
#define VID_PRE_SMOKE  125   /* 烟雾浓度 */
#define VID_FACE_FAN_AUTO   126   /* 已为您打开风扇 */
#define VID_FACE_LIGHT_AUTO 127   /* 已为您打开室内灯 */
#define VID_ENV_UPDATING    128   /* 环境数据正在更新 */
#define VID_AUTO_LIGHT_ON   129   /* 已自动开启灯光 */
#define VID_AUTO_LIGHT_OFF  130   /* 已自动关闭灯光 */
#define VID_AUTO_FAN_ON     131   /* 已自动开启风扇 */
#define VID_AUTO_FAN_OFF    132   /* 已自动关闭风扇 */
#define VID_AUTO_FAN_SPEED  133   /* 风扇已自动调节 */
#define VID_PM25_ALARM      134   /* 警告，粉尘浓度超标，请及时通风 */
#define VID_SMOKE_ALARM     135   /* 警告，烟雾浓度超标，请立即处理 */

/* ── 温度告警阈值 ── */
#define TEMP_HIGH_THRESHOLD  30   /* 超过此值播报"温度偏高" */
#define AIR_ALARM_REPEAT_COUNT 2U
#define PM25_ALARM_REPEAT_INTERVAL_MS 10000UL
#define VOICE_UART1_LOG_DEFAULT 0U
#define PM25_ALARM_CLEAR_MARGIN 10U
#define VOICE_AIR_ALARM_PM25  0x01U
#define VOICE_AIR_ALARM_SMOKE 0x02U

#define VOICE_UART1_LOG(...)                                      \
    do {                                                          \
        if (VOICE_UART1_LOG_DEFAULT) uart_printf(&huart1, __VA_ARGS__); \
    } while (0)

/* 数字-中文片段映射 */
static const uint8_t digit_vid[10] = {
    VID_ZERO, VID_ONE, VID_TWO, VID_THREE, VID_FOUR,
    VID_FIVE, VID_SIX, VID_SEVEN, VID_EIGHT, VID_NINE
};

static char voice_pending_cmd[VOICE_PLAY_CMD_SIZE];
static uint32_t voice_pending_tick = 0;
static uint8_t voice_pending_valid = 0;
static uint8_t face_welcome_pending = 0;
static uint8_t face_welcome_sent = 0;
static uint32_t face_welcome_last_sent_tick = 0;
static uint8_t face_welcome_last_seq = 0;
static uint8_t auto_notify_pending_mask = 0U;
static uint32_t auto_notify_last_sent_tick = 0U;
static uint8_t air_alarm_pending_mask = 0U;
static uint8_t pm25_alarm_latched = 0U;
static uint32_t pm25_alarm_last_sent_tick = 0U;

static int voice_clamp_value(int value, int min_value, int max_value);

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

static int voice_append_id_buf(char *buf, int buf_size, int offset, uint16_t id)
{
    int written;

    if (offset < 0 || offset >= buf_size) return buf_size;

    written = snprintf(buf + offset, buf_size - offset, ",%u", (unsigned int)id);
    if (written < 0 || written >= buf_size - offset) return buf_size;

    return offset + written;
}

static int voice_append_number_checked(char *buf, int buf_size, int offset,
                                       int value, int min_value, int max_value)
{
    int written;

    if (offset < 0 || offset >= buf_size) return buf_size;

    value = voice_clamp_value(value, min_value, max_value);

    written = voice_append_number_buf(buf + offset, buf_size - offset, value);
    if (written < 0 || written >= buf_size - offset) return buf_size;

    return offset + written;
}

static int voice_round_sensor_value(float value)
{
    if (value >= 0.0f) {
        return (int)(value + 0.5f);
    }

    return (int)(value - 0.5f);
}

static uint8_t voice_build_face_welcome_cmd(char *buf, int buf_size)
{
    int offset;

    if (buf == NULL || buf_size <= 1) return 0U;

    offset = snprintf(buf, buf_size, "PLAYS:%u", (unsigned int)FACE_WELCOME_PLAY_ID);
    if (offset <= 0 || offset >= buf_size) return 0U;

    if (device_state_temperature_valid()) {
        offset = voice_append_id_buf(buf, buf_size, offset, VID_PRE_TEMP);
        offset = voice_append_number_checked(buf, buf_size, offset,
                                             voice_round_sensor_value(device_state_get_temperature()),
                                             -10, 50);
        offset = voice_append_id_buf(buf, buf_size, offset, VID_DEGREE);
    } else {
        offset = voice_append_id_buf(buf, buf_size, offset, VID_ENV_UPDATING);
    }

    if (device_state_humidity_valid()) {
        offset = voice_append_id_buf(buf, buf_size, offset, VID_PRE_HUMI);
        offset = voice_append_id_buf(buf, buf_size, offset, VID_PERCENT);
        offset = voice_append_number_checked(buf, buf_size, offset,
                                             voice_round_sensor_value(device_state_get_humidity()),
                                             0, 100);
    } else if (device_state_temperature_valid()) {
        offset = voice_append_id_buf(buf, buf_size, offset, VID_ENV_UPDATING);
    }

    offset = voice_append_id_buf(buf, buf_size, offset, VID_FACE_FAN_AUTO);

    if (device_state_light_valid()) {
        offset = voice_append_id_buf(buf, buf_size, offset, VID_PRE_LUX);
        offset = voice_append_number_checked(buf, buf_size, offset,
                                             voice_round_sensor_value(device_state_get_light()),
                                             0, VOICE_MAX_SPOKEN_VALUE);
        offset = voice_append_id_buf(buf, buf_size, offset, VID_LUX);
    } else {
        offset = voice_append_id_buf(buf, buf_size, offset, VID_ENV_UPDATING);
    }

    offset = voice_append_id_buf(buf, buf_size, offset, VID_FACE_LIGHT_AUTO);

    return (offset > 0 && offset < buf_size) ? 1U : 0U;
}

void voice_notify_automation(uint8_t event_mask)
{
    if (event_mask == 0U) return;

    if (event_mask & VOICE_AUTO_EVENT_LIGHT_ON) {
        auto_notify_pending_mask &= (uint8_t)~VOICE_AUTO_EVENT_LIGHT_OFF;
    }
    if (event_mask & VOICE_AUTO_EVENT_LIGHT_OFF) {
        auto_notify_pending_mask &= (uint8_t)~VOICE_AUTO_EVENT_LIGHT_ON;
    }
    if (event_mask & VOICE_AUTO_EVENT_FAN_ON) {
        auto_notify_pending_mask &= (uint8_t)~(VOICE_AUTO_EVENT_FAN_OFF |
                                               VOICE_AUTO_EVENT_FAN_SPEED);
    }
    if (event_mask & VOICE_AUTO_EVENT_FAN_OFF) {
        auto_notify_pending_mask &= (uint8_t)~(VOICE_AUTO_EVENT_FAN_ON |
                                               VOICE_AUTO_EVENT_FAN_SPEED);
    }
    if ((event_mask & VOICE_AUTO_EVENT_FAN_SPEED) &&
        !(auto_notify_pending_mask & VOICE_AUTO_EVENT_FAN_ON)) {
        auto_notify_pending_mask &= (uint8_t)~VOICE_AUTO_EVENT_FAN_OFF;
    }

    auto_notify_pending_mask |= event_mask;
}

static int voice_append_play_id(char *buf, int buf_size, int offset,
                                uint16_t id)
{
    int written;

    if (offset == 0) {
        written = snprintf(buf, buf_size, "PLAYS:%u", (unsigned int)id);
        if (written <= 0 || written >= buf_size) return buf_size;
        return written;
    }

    return voice_append_id_buf(buf, buf_size, offset, id);
}

static uint8_t voice_build_auto_notify_cmd(uint8_t mask, char *buf, int buf_size)
{
    int offset = 0;

    if (buf == NULL || buf_size <= 1 || mask == 0U) return 0U;

    if (mask & VOICE_AUTO_EVENT_LIGHT_ON) {
        offset = voice_append_play_id(buf, buf_size, offset, VID_AUTO_LIGHT_ON);
    }
    if (mask & VOICE_AUTO_EVENT_LIGHT_OFF) {
        offset = voice_append_play_id(buf, buf_size, offset, VID_AUTO_LIGHT_OFF);
    }
    if (mask & VOICE_AUTO_EVENT_FAN_ON) {
        offset = voice_append_play_id(buf, buf_size, offset, VID_AUTO_FAN_ON);
    } else if (mask & VOICE_AUTO_EVENT_FAN_OFF) {
        offset = voice_append_play_id(buf, buf_size, offset, VID_AUTO_FAN_OFF);
    } else if (mask & VOICE_AUTO_EVENT_FAN_SPEED) {
        offset = voice_append_play_id(buf, buf_size, offset, VID_AUTO_FAN_SPEED);
    }

    return (offset > 0 && offset < buf_size) ? 1U : 0U;
}

static void voice_update_pm25_alarm_events(void)
{
    if (device_state_pm25_valid()) {
        uint16_t limit = device_state_get_pm25_limit();
        uint32_t now = HAL_GetTick();

        if (limit > 0U) {
            float pm25 = device_state_get_pm25();
            uint16_t clear_level = limit > PM25_ALARM_CLEAR_MARGIN ?
                                   (uint16_t)(limit - PM25_ALARM_CLEAR_MARGIN) : 0U;

            if (device_state_pm25_alarm()) {
                if (!pm25_alarm_latched ||
                    pm25_alarm_last_sent_tick == 0U ||
                    now - pm25_alarm_last_sent_tick >= PM25_ALARM_REPEAT_INTERVAL_MS) {
                    air_alarm_pending_mask |= VOICE_AIR_ALARM_PM25;
                }
                pm25_alarm_latched = 1U;
            } else if (pm25 <= (float)clear_level) {
                pm25_alarm_latched = 0U;
                pm25_alarm_last_sent_tick = 0U;
                air_alarm_pending_mask &= (uint8_t)~VOICE_AIR_ALARM_PM25;
            }
        }
    }
}

static uint8_t voice_build_air_alarm_cmd(uint8_t mask, char *buf, int buf_size)
{
    int offset = 0;

    if (buf == NULL || buf_size <= 1 || mask == 0U) return 0U;

    if (mask & VOICE_AIR_ALARM_PM25) {
        for (uint8_t i = 0U; i < AIR_ALARM_REPEAT_COUNT; i++) {
            offset = voice_append_play_id(buf, buf_size, offset, VID_PM25_ALARM);
        }
    }
    if (mask & VOICE_AIR_ALARM_SMOKE) {
        for (uint8_t i = 0U; i < AIR_ALARM_REPEAT_COUNT; i++) {
            offset = voice_append_play_id(buf, buf_size, offset, VID_SMOKE_ALARM);
        }
    }

    return (offset > 0 && offset < buf_size) ? 1U : 0U;
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
        VOICE_UART1_LOG("[VOICE] PLAYS too long, drop\r\n");
        return;
    }

    voice_pending_tick = HAL_GetTick() + VOICE_REPLY_DELAY_MS;
    voice_pending_valid = 1;
}

static uint8_t voice_plays_service(void)
{
    if (!voice_pending_valid) return 0;
    if ((int32_t)(HAL_GetTick() - voice_pending_tick) < 0) return 0;

    voice_pending_valid = 0;
    uart_printf(&huart3, "%s\r\n", voice_pending_cmd);
    return 1;
}

static uint8_t voice_face_welcome_service(void)
{
    uint32_t now = HAL_GetTick();

    if (face_take_welcome_event()) {
        /*
         * 持续识别到人脸期间事件会重复产生（face.c 不再只在上升沿触发一次），
         * 播报频率由本冷却时间控制：冷却期内事件直接丢弃，
         * 冷却结束后下一条事件恢复播报。
         */
        face_welcome_last_seq = face_get_event_seq();
        if (!face_welcome_sent ||
            now - face_welcome_last_sent_tick >= FACE_WELCOME_COOLDOWN_MS) {
            face_welcome_pending = 1;
        }
    }

    /*
     * 用户语音命令还处于延迟发送阶段时不能覆盖它；已发送的命令会由
     * ASRPRO 自己的播放队列串行播放，欢迎语从同一串口通道进入队列。
     */
    if (voice_pending_valid || !face_welcome_pending) return 0U;

    char welcome_cmd[VOICE_PLAY_CMD_SIZE];

    if (voice_build_face_welcome_cmd(welcome_cmd, sizeof(welcome_cmd))) {
        uart_printf(&huart3, "%s\r\n", welcome_cmd);
    } else {
        uart_printf(&huart3, "PLAY:%u\r\n", FACE_WELCOME_PLAY_ID);
    }

    face_welcome_pending = 0;
    face_welcome_sent = 1;
    face_welcome_last_sent_tick = HAL_GetTick();
    return 1U;
}

uint8_t voice_face_welcome_pending(void)
{
    return face_welcome_pending;
}

uint8_t voice_face_welcome_sent(void)
{
    return face_welcome_sent;
}

uint8_t voice_face_welcome_event_gen(void)
{
    return face_welcome_last_seq;
}

static uint8_t voice_auto_notify_service(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t mask;
    char notify_cmd[VOICE_PLAY_CMD_SIZE];

    if (voice_pending_valid || face_welcome_pending || auto_notify_pending_mask == 0U) {
        return 0U;
    }
    if (auto_notify_last_sent_tick != 0U &&
        now - auto_notify_last_sent_tick < 5000UL) {
        return 0U;
    }

    mask = auto_notify_pending_mask;
    if (!voice_build_auto_notify_cmd(mask, notify_cmd, sizeof(notify_cmd))) {
        auto_notify_pending_mask = 0U;
        return 0U;
    }

    auto_notify_pending_mask = 0U;
    auto_notify_last_sent_tick = now;
    voice_plays_send("%s", notify_cmd);
    return 1U;
}

static uint8_t voice_pm25_alarm_service(void)
{
    uint8_t mask;
    char alarm_cmd[VOICE_PLAY_CMD_SIZE];

    voice_update_pm25_alarm_events();

    /* 告警优先级高于普通延迟播报：不等待 voice_pending_cmd，直接发往 ASRPRO 队列 */
    if (air_alarm_pending_mask == 0U) {
        return 0U;
    }

    mask = air_alarm_pending_mask;
    if (!voice_build_air_alarm_cmd(mask, alarm_cmd, sizeof(alarm_cmd))) {
        air_alarm_pending_mask = 0U;
        return 0U;
    }

    air_alarm_pending_mask = 0U;
    /* 告警直接入 ASRPRO 队列，避免被后续查询播报覆盖。 */
    uart_printf(&huart3, "%s\r\n", alarm_cmd);
    if (mask & VOICE_AIR_ALARM_PM25) {
        pm25_alarm_last_sent_tick = HAL_GetTick();
    }
    return 1U;
}

static int voice_clamp_value(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static uint8_t voice_take_pending_frame(char *local_buf, uint16_t local_buf_size)
{
    uint16_t len;

    if (local_buf == NULL || local_buf_size <= 1U) return 0U;

    len = my_uart3_take_frame((uint8_t *)local_buf,
                              (uint16_t)(local_buf_size - 1U));
    if (len == 0U) {
        local_buf[0] = '\0';
        return 0U;
    }

    local_buf[len] = '\0';
    return 1U;
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
    /* ── 立即拷贝到局部缓冲区，防止 UART 接收竞争覆盖 ── */
    char local_buf[128];
    memset(local_buf, 0, sizeof(local_buf));
    if (!voice_take_pending_frame(local_buf, sizeof(local_buf))) return;

    VOICE_UART1_LOG("[VOICE] %s\r\n", local_buf);

    /* ── 过滤 ASRPRO 模块的 echo/应答消息（[RX] 开头），不当作语音指令处理 ── */
    if (strncmp(local_buf, "[RX]", 4) == 0) {
        return;
    }

    /* ---- 在局部缓冲区上按 ':' 分割字段 ---- */
    char *cat  = strtok(local_buf, ":\r\n");   /* CATEGORY                     */
    char *tgt  = strtok(NULL,      ":\r\n");   /* TARGET（灯带: "1"/"3"/"ALL"）*/
    char *act  = strtok(NULL,      ":\r\n");   /* ACTION                       */
    char *val  = strtok(NULL,      ":\r\n");   /* VALUE（可选）                 */

    /* ── 调试：打印解析结果 ── */
    VOICE_UART1_LOG("[VOICE] cat=%s tgt=%s act=%s val=%s\r\n",
                    cat ? cat : "(null)",
                    tgt ? tgt : "(null)",
                    act ? act : "(null)",
                    val ? val : "(null)");

    if (cat == NULL) {
        return;  /* 局部缓冲区，不需要 cleanup */
    }

    /* ================================================
     *  自动控制 AUTO
     *
     *  AUTO:ON  进入自动控制
     *  AUTO:OFF 进入手动覆盖
     * ================================================ */
    if (strcmp(cat, VOICE_CAT_AUTO) == 0) {
        if (act == NULL && tgt != NULL) {
            act = tgt;
        }

        if (act != NULL && strcmp(act, "ON") == 0) {
            device_state_set_auto_enabled(1U, DEVICE_SOURCE_VOICE);
        } else if (act != NULL && strcmp(act, "OFF") == 0) {
            device_state_set_auto_enabled(0U, DEVICE_SOURCE_VOICE);
        }
        lcd_send();
    }

    /* ================================================
     *  灯带控制  LIGHT
     *
     *  新格式: LIGHT:<target>:<action>[:value]
     *     target = "1" 室内灯, "3" 室外灯, 或 "ALL" (全部)
     *     target = "2" 为原入户灯保留通道，不作为可用灯具
     *     action = ON / OFF / COLOR / MODE / AUTO
     *  兼容旧格式: LIGHT:<action>[:value]  (无 target，默认灯带1)
     * ================================================ */
    else if (strcmp(cat, VOICE_CAT_LIGHT) == 0) {

        /* ---- 解析 target ---- */
        uint8_t strip_id = 0;       /* 0=非法, 255=ALL */

        if (tgt != NULL) {
            /* 判断 tgt 是 target 还是 action（兼容旧格式）*/
            if (strcmp(tgt, "ON") == 0 || strcmp(tgt, "OFF") == 0
                || strcmp(tgt, "COLOR") == 0 || strcmp(tgt, "MODE") == 0
                || strcmp(tgt, "AUTO") == 0 || strcmp(tgt, "MANUAL") == 0) {
                /* 旧格式: LIGHT:ON — tgt 字段实际是 action */
                act = tgt;
                tgt = NULL;
                strip_id = 1;       /* 默认灯带1 */
            } else if (strcmp(tgt, "ALL") == 0) {
                strip_id = 255;     /* ALL 标记 */
            } else {
                strip_id = (uint8_t)atoi(tgt);
                if (!device_state_strip_is_active(strip_id)) return;
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
            for (uint8_t _i = 0, _done = 0; _i < DEVICE_STATE_STRIP_COUNT && !_done; _i++) { \
                uint8_t id; \
                if (all) { id = _i + 1; } \
                else     { id = strip_id; _done = 1; } \
                if (!device_state_strip_is_active(id)) continue;

        #define END_FOR }

        /* --- LIGHT:<t>:ON --- */
        if (strcmp(act, "ON") == 0) {
            FOR_EACH_STRIP(sid, (strip_id == 255))
                device_state_set_strip_rgb(sid, 125, 125, 125, DEVICE_SOURCE_VOICE);
            END_FOR
        }
        /* --- LIGHT:<t>:OFF --- */
        else if (strcmp(act, "OFF") == 0) {
            FOR_EACH_STRIP(sid, (strip_id == 255))
                device_state_set_strip_rgb(sid, 0, 0, 0, DEVICE_SOURCE_VOICE);
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
                device_state_set_strip_rgb(sid, cr, cg, cb, DEVICE_SOURCE_VOICE);
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
                device_state_set_strip_rgb(sid, cr, cg, cb, DEVICE_SOURCE_VOICE);
            END_FOR
        }
        /* --- LIGHT:AUTO / LIGHT:<t>:AUTO --- */
        else if (strcmp(act, "AUTO") == 0) {
            device_state_set_auto_enabled(1U, DEVICE_SOURCE_VOICE);
        }
        /* --- LIGHT:MANUAL / LIGHT:<t>:MANUAL --- */
        else if (strcmp(act, "MANUAL") == 0) {
            device_state_set_auto_enabled(0U, DEVICE_SOURCE_VOICE);
        }

        #undef FOR_EACH_STRIP
        #undef END_FOR
        lcd_send();
    }

    /* ================================================ */
    /*  三、风扇控制  FAN                                */
    /* ================================================ */
    else if (strcmp(cat, VOICE_CAT_FAN) == 0) {

        /* FAN 没有 TARGET 字段：tgt 是动作，act 是动作参数。 */
        if (tgt != NULL) {
            if (act == NULL) {
                act = tgt;
                tgt = NULL;
            } else if (strcmp(tgt, "SPEED") == 0) {
                /* FAN:SPEED:UP/DOWN/1~4 */
                val = act;
                act = tgt;
                tgt = NULL;
            }
        }

        /* --- FAN:ON --- */
        if (act != NULL && strcmp(act, "AUTO") == 0) {
            device_state_set_fan_mode(DEVICE_FAN_AUTO, DEVICE_SOURCE_VOICE);
        }
        else if (act != NULL && strcmp(act, "MANUAL") == 0) {
            device_state_set_auto_enabled(0U, DEVICE_SOURCE_VOICE);
        }
        else if (act != NULL && strcmp(act, "ON") == 0) {
            if (device_state_get_fan_speed() == 0U) {
                device_state_set_fan(500, DEVICE_FAN_MANUAL, DEVICE_SOURCE_VOICE);
            }
        }
        /* --- FAN:OFF --- */
        else if (act != NULL && strcmp(act, "OFF") == 0) {
            device_state_set_fan(0, DEVICE_FAN_OFF, DEVICE_SOURCE_VOICE);
        }
        /* --- FAN:SPEED:UP / DOWN / 档位 --- */
        else if (act != NULL && strcmp(act, "SPEED") == 0) {
            if (val != NULL) {
                if (strcmp(val, "UP") == 0) {
                    device_state_set_fan((uint16_t)(device_state_get_fan_speed() + 200U),
                                         DEVICE_FAN_MANUAL, DEVICE_SOURCE_VOICE);
                }
                else if (strcmp(val, "DOWN") == 0) {
                    device_state_set_fan(device_state_get_fan_speed() > 200U ?
                                         (uint16_t)(device_state_get_fan_speed() - 200U) : 0U,
                                         DEVICE_FAN_MANUAL, DEVICE_SOURCE_VOICE);
                }
                else {
                    /* 数值档位 1~4 */
                    uint8_t gear = (uint8_t)atoi(val);
                    if (gear >= 1U && gear <= 4U) {
                        device_state_set_fan((uint16_t)gear * 250U,
                                             DEVICE_FAN_MANUAL, DEVICE_SOURCE_VOICE);
                    }
                }
            }
        }
        lcd_send();
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
            device_state_set_board_led(1U, DEVICE_SOURCE_VOICE);
        }
        else if (act != NULL && strcmp(act, "OFF") == 0) {
            device_state_set_board_led(0U, DEVICE_SOURCE_VOICE);
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
            int v = (int)(device_state_get_temperature() + 0.5f);
            v = voice_clamp_value(v, -10, 50);

            /* 拼装完整 PLAYS 字符串，一次发送避免 ASRPRO 超时截断 */
            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_TEMP);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_DEGREE);
            if (v > TEMP_HIGH_THRESHOLD)
                off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_TEMP_HIGH);
            voice_plays_send("%s", seg);

            VOICE_UART1_LOG("[QUERY] temp=%.1f => 语音拼接%s\r\n",
                            device_state_get_temperature(),
                            v > TEMP_HIGH_THRESHOLD ? " [偏高]" : "");
            lcd_send();
        }
        /* --- QUERY:HUMI --- */
        else if (act != NULL && strcmp(act, "HUMI") == 0) {
            int v = (int)(device_state_get_humidity() + 0.5f);
            v = voice_clamp_value(v, 0, 100);

            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d,%d", VID_PRE_HUMI, VID_PERCENT);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            voice_plays_send("%s", seg);

            VOICE_UART1_LOG("[QUERY] humi=%.1f => voice\r\n", device_state_get_humidity());
            lcd_send();
        }
        /* --- QUERY:PM25 --- */
        else if (act != NULL && strcmp(act, "PM25") == 0) {
            if (!device_state_pm25_valid()) {
                voice_plays_send("PLAYS:%d", VID_ENV_UPDATING);
                VOICE_UART1_LOG("[QUERY] pm25 invalid => updating\r\n");
            } else {
                int v = (int)(device_state_get_pm25() + 0.5f);
                uint8_t pm25_alarm = device_state_pm25_alarm();
                v = voice_clamp_value(v, 0, VOICE_MAX_SPOKEN_VALUE);

                char seg[VOICE_PLAY_CMD_SIZE];
                int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_PM25);
                off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
                off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_UGPM3);
                if (pm25_alarm) {
                    for (uint8_t i = 0U; i < AIR_ALARM_REPEAT_COUNT; i++) {
                        off = voice_append_play_id(seg, sizeof(seg), off, VID_PM25_ALARM);
                    }
                }
                voice_plays_send("%s", seg);

                VOICE_UART1_LOG("[QUERY] pm25=%.1f ug/m3 => voice%s\r\n",
                                device_state_get_pm25(),
                                pm25_alarm ? " [alarm]" : "");
            }
            lcd_send();
        }
        /* --- QUERY:LIGHT --- */
        else if (act != NULL && strcmp(act, "LIGHT") == 0) {
            int v = (int)(device_state_get_light() + 0.5f);
            v = voice_clamp_value(v, 0, VOICE_MAX_SPOKEN_VALUE);

            char seg[80];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_LUX);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            off += snprintf(seg + off, sizeof(seg) - off, ",%d", VID_LUX);
            voice_plays_send("%s", seg);

            VOICE_UART1_LOG("[QUERY] lux=%.0f => voice\r\n", device_state_get_light());
            lcd_send();
        }
        /* --- QUERY:SMOKE --- */
        else if (act != NULL && strcmp(act, "SMOKE") == 0) {
            float smoke_ppm;
            uint8_t smoke_alarm;

#if SMOKE_TEST_MODE
            smoke_test_trigger_alarm();
#endif
            smoke_ppm = smoke_get_ppm();
            smoke_alarm = (smoke_is_alarmed() ||
                           smoke_ppm >= (float)device_state_get_smoke_limit_ppm()) ? 1U : 0U;

            int v = (int)(smoke_ppm + 0.5f);
            v = voice_clamp_value(v, 0, VOICE_MAX_SPOKEN_VALUE);

            char seg[VOICE_PLAY_CMD_SIZE];
            int off = snprintf(seg, sizeof(seg), "PLAYS:%d", VID_PRE_SMOKE);
            off += voice_append_number_buf(seg + off, sizeof(seg) - off, v);
            if (smoke_alarm) {
                for (uint8_t i = 0U; i < AIR_ALARM_REPEAT_COUNT; i++) {
                    off = voice_append_play_id(seg, sizeof(seg), off, VID_SMOKE_ALARM);
                }
            }
            voice_plays_send("%s", seg);

            VOICE_UART1_LOG("[QUERY] smoke=%.1f ppm => voice%s\r\n",
                            (double)smoke_ppm,
                            smoke_alarm ? " [alarm]" : "");
            lcd_send();
        }
        /* --- QUERY:ALL --- */
        else if (act != NULL && strcmp(act, "ALL") == 0) {
            int tv = voice_clamp_value((int)(device_state_get_temperature() + 0.5f), -10, 50);
            int hv = voice_clamp_value((int)(device_state_get_humidity() + 0.5f), 0, 100);
            int pv = voice_clamp_value((int)(device_state_get_pm25() + 0.5f), 0, VOICE_MAX_SPOKEN_VALUE);
            int lv = voice_clamp_value((int)(device_state_get_light() + 0.5f), 0, VOICE_MAX_SPOKEN_VALUE);
            int sv = voice_clamp_value((int)(device_state_get_smoke() + 0.5f), 0, VOICE_MAX_SPOKEN_VALUE);

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

            VOICE_UART1_LOG("[QUERY] all t=%d h=%d pm25=%d lux=%d smoke=%d%s\r\n",
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
    uint8_t voice_sent;
    uint8_t frames = 0U;

    while (uart3_msg_pending && frames < VOICE_MAX_FRAMES_PER_SERVICE) {
        voice_parse();
        frames++;
    }

    /* 空气质量告警优先：先处理告警，避免被普通延迟播报挤掉 */
    if (!voice_pm25_alarm_service()) {
        voice_sent = voice_plays_service();
        if (!voice_sent && !voice_face_welcome_service()) {
            voice_auto_notify_service();
        }
    }
}
