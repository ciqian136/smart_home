#include "esp32.h"
#include "device_state.h"

#include "my_uart.h"
#include <string.h>
#include <stdlib.h>

/*
引脚定义
STM32:串口2:TX:PA2
						RX:PA3
ESP32:串口1:TX:7
						RX:6
*/
/*个人配置*/
#define WIFI_SSID "iQOO Neo"
#define WIFI_PASSWORD "88888888"

#define MQTT_SERVER "mqtts.heclouds.com"
#define MQTT_PORT 1883

#define PRODUCT_ID "zs8Fz7juvp"
#define DEVICE_NAME "one_test"
#define MQTT_TOKEN "version=2018-10-31&res=products%2Fzs8Fz7juvp%2Fdevices%2Fone_test&et=1911035456&method=md5&sign=LaO7G69DItgrJh2ng4LNdw%3D%3D"

/*利用c语言自动拼接*/
#define TOPIC_POST_RELAY "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/property/post/reply"
#define TOPIC_SET "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/property/set"
#define TOPIC_POST "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/property/post"
#define TOPIC_SET_RELAY "$sys/" PRODUCT_ID "/" DEVICE_NAME "/thing/property/set_reply"

#define ESP32_REPLY_RETRY_MS 500U
#define ESP32_REPLY_MAX_RETRY 3U

/* ── AT 指令忙 / 超时保护 ──────────────────────── */
#define AT_CMD_TIMEOUT_MS      5000U   /* AT 指令超时（5s），超时自动解除 busy */
#define ESP_FAIL_COOLDOWN_MS  30000U   /* 初始化失败后冷却时间（30s），避免频繁重试 */
#define ESP_RST_WAIT_MS        3000U   /* AT+RST 后等待复位 */
#define ESP_CHECK_INTERVAL_MS  10000U  /* 每 10s 检查一次连接状态 */
#define MQTT_PING_INTERVAL_MS  30000U  /* 每 30s 发送一次 MQTT PING */
#define ESP_RUNTIME_TIMEOUT_RECONNECTS 3U
#define ESP_RECONNECT_MIN_INTERVAL_MS  3000U
#define ESP_NO_ALIVE_RECONNECT_MS      120000U

volatile uint8_t esp32_rx_pending = 0;
volatile uint8_t at_cmd_busy       = 0;   /* 1=AT 指令执行中，禁止发送新命令 */
volatile uint8_t esp32_initialized = 0;   /* 1=非阻塞初始化已完成 */
volatile uint8_t esp32_uart1_status_report_enabled =
    ESP32_UART1_STATUS_REPORT_DEFAULT;

/* ESP 状态日志统一由此开关控制，默认不发送到串口1。 */
#define ESP32_UART1_STATUS_PRINTF(...)                                      \
  do {                                                                      \
    if (esp32_uart1_status_report_enabled) {                                \
      uart_printf(&huart1, __VA_ARGS__);                                    \
    }                                                                       \
  } while (0)

static uint32_t at_cmd_start_tick = 0;      /* 发送 AT 指令时的系统 tick */
static uint8_t  at_cmd_timeout_logged = 0;  /* 防止超时日志重复刷屏 */
static uint8_t  esp32_got_ok = 0;           /* recv 检测到 OK，通知 init 状态机 */

/* ── set_reply 回复 ────────────────────────────── */
static char pending_reply_msg_id[16] = {0};
volatile uint8_t need_send_reply = 0;

/* ── 在线检测计时 ──────────────────────────────── */
static uint32_t last_esp_check_tick  = 0;
static uint32_t last_mqtt_ping_tick  = 0;
static uint8_t  esp_offline_flag     = 0;
static uint8_t  esp_reconnect_requested = 0;
static uint8_t  esp_runtime_timeout_count = 0;
static uint32_t last_esp_alive_tick = 0;
static uint32_t last_reconnect_tick = 0;

static void esp32_note_alive(void);
static void esp32_request_reconnect(const char *reason);
static void esp32_start_reconnect(void);

#define ESP_LIGHT_TARGET_ALL      255U
#define ESP_LIGHT_PRESET_OFF        0
#define ESP_LIGHT_PRESET_ON         1
#define ESP_LIGHT_PRESET_WARM       2
#define ESP_LIGHT_PRESET_WHITE      3
#define ESP_LIGHT_PRESET_NIGHT      4
#define ESP_LIGHT_PRESET_READ       5
#define ESP_LIGHT_PRESET_RED        6
#define ESP_LIGHT_PRESET_GREEN      7
#define ESP_LIGHT_PRESET_BLUE       8
#define ESP_LIGHT_PRESET_CUSTOM    99

typedef struct {
  int id;
  const char *name;
  uint8_t r;
  uint8_t g;
  uint8_t b;
} esp_light_preset_t;

static const esp_light_preset_t light_presets[] = {
    {ESP_LIGHT_PRESET_OFF,   "OFF",   0U,   0U,   0U},
    {ESP_LIGHT_PRESET_ON,    "ON",    125U, 125U, 125U},
    {ESP_LIGHT_PRESET_WARM,  "WARM",  255U, 200U, 100U},
    {ESP_LIGHT_PRESET_WHITE, "WHITE", 255U, 255U, 255U},
    {ESP_LIGHT_PRESET_NIGHT, "NIGHT", 12U,  10U,  5U},
    {ESP_LIGHT_PRESET_READ,  "READ",  178U, 140U, 70U},
    {ESP_LIGHT_PRESET_RED,   "RED",   255U, 0U,   0U},
    {ESP_LIGHT_PRESET_GREEN, "GREEN", 0U,   255U, 0U},
    {ESP_LIGHT_PRESET_BLUE,  "BLUE",  0U,   0U,   255U},
};

static char esp32_upper_ascii(char ch)
{
  return (ch >= 'a' && ch <= 'z') ? (char)(ch - ('a' - 'A')) : ch;
}

static uint8_t esp32_str_equal_ci(const char *a, const char *b)
{
  if (a == NULL || b == NULL) return 0U;
  while (*a != '\0' && *b != '\0') {
    if (esp32_upper_ascii(*a) != esp32_upper_ascii(*b)) return 0U;
    a++;
    b++;
  }
  return (*a == '\0' && *b == '\0') ? 1U : 0U;
}

static const esp_light_preset_t *esp32_find_light_preset_by_id(int preset_id)
{
  for (uint8_t i = 0U; i < sizeof(light_presets) / sizeof(light_presets[0]); i++) {
    if (light_presets[i].id == preset_id) return &light_presets[i];
  }
  return NULL;
}

static int esp32_light_preset_id_from_name(const char *name)
{
  if (name == NULL || name[0] == '\0') return -1;

  for (uint8_t i = 0U; i < sizeof(light_presets) / sizeof(light_presets[0]); i++) {
    if (esp32_str_equal_ci(name, light_presets[i].name)) {
      return light_presets[i].id;
    }
  }

  if (esp32_str_equal_ci(name, "DEFAULT") ||
      esp32_str_equal_ci(name, "NORMAL") ||
      esp32_str_equal_ci(name, "DIM")) {
    return ESP_LIGHT_PRESET_ON;
  }
  if (esp32_str_equal_ci(name, "CLOSE") ||
      esp32_str_equal_ci(name, "CLOSED") ||
      esp32_str_equal_ci(name, "SLEEP")) {
    return ESP_LIGHT_PRESET_OFF;
  }

  return -1;
}

static uint8_t esp32_apply_light_preset_to_strip(uint8_t strip_id, int preset_id)
{
  const esp_light_preset_t *preset = esp32_find_light_preset_by_id(preset_id);
  if (preset == NULL || !device_state_strip_is_active(strip_id)) return 0U;

  return device_state_set_strip_rgb(strip_id, preset->r, preset->g, preset->b,
                                    DEVICE_SOURCE_CLOUD);
}

static uint8_t esp32_apply_light_preset(uint8_t target, int preset_id)
{
  uint8_t applied = 0U;

  if (target == ESP_LIGHT_TARGET_ALL) {
    for (uint8_t i = 0U; i < DEVICE_STATE_STRIP_COUNT; i++) {
      uint8_t sid = (uint8_t)(i + 1U);
      if (esp32_apply_light_preset_to_strip(sid, preset_id)) {
        applied = 1U;
      }
    }
    return applied;
  }

  return esp32_apply_light_preset_to_strip(target, preset_id);
}

static int esp32_get_light_preset_state(uint8_t strip_id)
{
  uint8_t r = 0U, g = 0U, b = 0U;
  if (!device_state_get_strip_rgb(strip_id, &r, &g, &b)) {
    return ESP_LIGHT_PRESET_CUSTOM;
  }

  for (uint8_t i = 0U; i < sizeof(light_presets) / sizeof(light_presets[0]); i++) {
    if (light_presets[i].r == r && light_presets[i].g == g && light_presets[i].b == b) {
      return light_presets[i].id;
    }
  }

  return ESP_LIGHT_PRESET_CUSTOM;
}

static void esp32_note_alive(void)
{
  last_esp_alive_tick = HAL_GetTick();
  esp_runtime_timeout_count = 0U;
}

static void esp32_request_reconnect(const char *reason)
{
  if (!esp_reconnect_requested) {
    ESP32_UART1_STATUS_PRINTF("[ESP32] reconnect requested: %s\r\n",
                              reason ? reason : "unknown");
  }
  esp_reconnect_requested = 1U;
  esp_offline_flag = 1U;
}

static void queue_set_reply(const char *msg_id)
{
  if (msg_id == NULL || msg_id[0] == '\0') return;
  strncpy(pending_reply_msg_id, msg_id, sizeof(pending_reply_msg_id) - 1);
  need_send_reply = 1;
}

/**
  * @brief  发送待回复的 set_reply（独立于数据上报）
  * @note   优先于普通数据上报执行
  */
void esp32_flush_reply(void)
{
  if (!need_send_reply) return;
  if (esp_reconnect_requested) return;
  if (at_cmd_busy) return;  /* 等当前指令完成，下一轮再试 */

  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "AT+MQTTPUB=0,\"%s\",\"{\\\"id\\\":\\\"%s\\\"\\,\\\"code\\\":200\\,\\\"msg\\\":\\\"success\\\"}\",0,0\r\n",
           TOPIC_SET_RELAY, pending_reply_msg_id);
  uart_printf(&huart2, "%s", cmd);

  at_cmd_busy = 1;
  at_cmd_start_tick = HAL_GetTick();
  at_cmd_timeout_logged = 0;
  need_send_reply = 0;
  memset(pending_reply_msg_id, 0, sizeof(pending_reply_msg_id));

  /* 短轮询：快速捕获 set_reply 的 OK 响应 */
  uint32_t poll_start = HAL_GetTick();
  while (HAL_GetTick() - poll_start < 100) {
    if (esp32_rx_pending) {
      esp32_rx_pending = 0;
      esp32_run_recv();
      if (!at_cmd_busy) break;
    }
    HAL_Delay(1);
  }
}

/* ── 发送 case 枚举 ─────────────────────────────── */
#define SEND_CASE_TEST_INT  0
#define SEND_CASE_MQ2       1
#define SEND_CASE_PM25      2
#define SEND_CASE_HUMI      3
#define SEND_CASE_TEMP      4
#define SEND_CASE_LIGHT     5
#define SEND_CASE_FAN       6
#define SEND_CASE_RGB_GROUP 7    /* 灯带 RGB 上报（轮询）*/
#define SEND_CASE_LED       8
#define SEND_CASE_MODE      9    /* AUTO/MANUAL + fan mode */
#define SEND_CASE_PRESET    10   /* 灯带预设状态上报（轮询）*/
#define SEND_CASE_MAX       11

/**
  * @brief  ESP32 数据发送任务 - 向 OneNET 分时上报传感器数据
  *         每次调用只发送一个属性组（~50ms），切换 case 顺序轮询
  *         完整一轮 11 个 case，每 1s 发送一次（10→1 降频），总间隔约 11s
  */
void esp32_run_send(void) {

  esp32_flush_reply();

  /* 非阻塞初始化未完成 或 AT 指令忙则跳过 */
  if (esp_reconnect_requested) return;
  if (!esp32_initialized) return;
  if (at_cmd_busy) return;

  /* 降频：每 10 次调用只发送 1 次（1s 间隔），减少 UART 碰撞 */
  static uint8_t skip = 0;
  if (++skip < 10) return;
  skip = 0;

  static uint32_t test_int = 0;
  static uint8_t  send_case = 0;
  static char cmd_buf[512] = {0};

  memset(cmd_buf, 0, sizeof(cmd_buf));

  switch (send_case) {

  /* ── 0: 测试计数器 ──────────────────────────── */
  case SEND_CASE_TEST_INT:
    test_int++;
    build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                     "test_int", 'i', test_int);
    break;

  /* ── 1: 烟雾传感器 MQ2 (ppm) ───────────────── */
  case SEND_CASE_MQ2:
    if (device_state_smoke_valid()) {
      build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                       "MQ2", 'f', (double)device_state_get_smoke());
    }
    break;

  /* ── 2: PM2.5 (µg/m³) ──────────────────────── */
  case SEND_CASE_PM25:
    if (device_state_pm25_valid()) {
      build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                       "PM25", 'f', (double)device_state_get_pm25());
    }
    break;

  /* ── 3: 湿度 (%) ───────────────────────────── */
  case SEND_CASE_HUMI:
    if (device_state_humidity_valid()) {
      build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                       "humi", 'f', (double)device_state_get_humidity());
    }
    break;

  /* ── 4: 温度 (°C) ──────────────────────────── */
  case SEND_CASE_TEMP:
    if (device_state_temperature_valid()) {
      build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                       "temp", 'f', (double)device_state_get_temperature());
    }
    break;

  /* ── 5: 光照强度 (lux) ─────────────────────── */
  case SEND_CASE_LIGHT:
    if (device_state_light_valid()) {
      build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                       "light", 'f', (double)device_state_get_light());
    }
    break;

  /* ── 6: 风扇转速 ────────────────────────────── */
  case SEND_CASE_FAN:
    build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                     "fan", 'i', device_state_get_fan_speed());
    break;

  /* ── 7: 灯带 RGB 轮询上报：1=室内, 3=室外, 2=保留不用 ── */
  case SEND_CASE_RGB_GROUP: {
    static uint8_t rgb_report_idx = 0;
    uint8_t sid = rgb_report_idx + 1;

    if (sid <= device_state_get_strip_count() && device_state_strip_is_active(sid)) {
        uint8_t r, g, b;
        char r_name[16], g_name[16], b_name[16];
        if (device_state_get_strip_rgb(sid, &r, &g, &b)) {
          snprintf(r_name, sizeof(r_name), "RGB%d_RAD",   sid);
          snprintf(g_name, sizeof(g_name), "RGB%d_GREEN", sid);
          snprintf(b_name, sizeof(b_name), "RGB%d_BLUE",  sid);

          build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 3,
                           r_name, 'i', r,
                           g_name, 'i', g,
                           b_name, 'i', b);
        }
    }

    rgb_report_idx++;
    if (rgb_report_idx >= device_state_get_strip_count()) rgb_report_idx = 0;
    break;
  }

  /* ── 8: 板载 LED 状态 ───────────────────────── */
  case SEND_CASE_LED:
    build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                     "LED", 'b',
                     device_state_get_board_led());
    break;

  case SEND_CASE_MODE:
    build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 4,
                     "auto_enabled", 'b',
                     device_state_get_auto_enabled() &&
                         !device_state_manual_override_active(),
                     "fan_mode", 'i', device_state_get_fan_mode(),
                     "PM25_alarm", 'b', device_state_pm25_alarm(),
                     "MQ2_alarm", 'b', device_state_smoke_alarm());
    break;

  case SEND_CASE_PRESET: {
    static uint8_t preset_report_idx = 0U;
    uint8_t count = device_state_get_strip_count();

    for (uint8_t tries = 0U; tries < count; tries++) {
      uint8_t sid = (uint8_t)(preset_report_idx + 1U);
      char preset_name[24];

      preset_report_idx++;
      if (preset_report_idx >= count) preset_report_idx = 0U;
      if (!device_state_strip_is_active(sid)) continue;

      snprintf(preset_name, sizeof(preset_name), "RGB%d_PRESET_STATE", sid);
      build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 1,
                       preset_name, 'i', esp32_get_light_preset_state(sid));
      break;
    }
    break;
  }
  }

  send_case++;
  if (send_case >= SEND_CASE_MAX) send_case = 0;

  /* 有数据才发送（MQ2 未就绪时跳过） */
  if (cmd_buf[0] != '\0') {
    uart_printf(&huart2, "%s", cmd_buf);
    at_cmd_busy = 1;
    at_cmd_start_tick = HAL_GetTick();
    at_cmd_timeout_logged = 0;

    /* 短轮询（<100ms）：快速捕获 OK + 立即发送可能产生的 set_reply */
    uint32_t poll_start = HAL_GetTick();
    while (HAL_GetTick() - poll_start < 100) {
      if (esp32_rx_pending) {
        esp32_rx_pending = 0;
        esp32_run_recv();             /* 处理 OK/MQTTSUBRECV */
        esp32_flush_reply();          /* 若有 reply 排队，立即发送 */
        if (!at_cmd_busy) break;
      }
      HAL_Delay(1);
    }
  }

}

/**
  * @brief  AT 指令超时检测（无条件调用，不依赖 RX 中断）
  * @note   必须在 schedule_run 中每次循环都调用，防止 ESP32 死机时
  *         at_cmd_busy 永久卡死整个发送管道
  */
void esp32_check_cmd_timeout(void)
{
  if (at_cmd_busy && (HAL_GetTick() - at_cmd_start_tick) > AT_CMD_TIMEOUT_MS) {
    at_cmd_busy = 0;
    if (esp32_initialized && esp_runtime_timeout_count < 0xFFU) {
      esp_runtime_timeout_count++;
    }
    if (!at_cmd_timeout_logged) {
      ESP32_UART1_STATUS_PRINTF("[ESP32] AT cmd timeout, reset busy\r\n");
      at_cmd_timeout_logged = 1;   /* 只打印一次，后续由 send/init 清除此标志 */
    }
    if (esp32_initialized &&
        esp_runtime_timeout_count >= ESP_RUNTIME_TIMEOUT_RECONNECTS) {
      esp32_request_reconnect("AT timeout");
    }
  }
}

/* 辅助：从缓冲区移除已处理的前缀数据 */
static void buf_consume(char *buf, uint16_t *len, uint16_t consumed)
{
  if (consumed >= *len) {
    *len = 0;
    buf[0] = '\0';
  } else {
    uint16_t remaining = *len - consumed;
    memmove(buf, buf + consumed, remaining);
    *len = remaining;
    buf[remaining] = '\0';
  }
}

/**
  * @brief  ESP32 数据接收处理任务 - 处理 ESP32 返回的所有数据
  *         1. 清理 OK/ERROR/WIFI/MQTT 等纯状态行（避免堆积）
  *         2. 检测 +MQTTSUBRECV → 调用 MQTT_Handle → memmove 移除
  *         3. 检测 +CIPSNTPTIME → 调用 TIME_Handle → memmove 移除
  *         4. 所有 memmove 后更新 len，确保后续处理使用新长度
  */
void esp32_run_recv(void) {
  uint8_t rx_frame[sizeof(uart2_rx_buf)];
  uint16_t rx_len;

  while ((rx_len = my_uart2_take_frame(rx_frame, sizeof(rx_frame))) > 0U) {
    uint16_t available = (uint16_t)(sizeof(uart2_rx_buf) - 1U - uart2_rx_len);
    if (rx_len > available) {
      /* Keep the newest complete receive chunk if the parser is stuck. */
      uart2_rx_len = 0U;
      available = (uint16_t)(sizeof(uart2_rx_buf) - 1U);
    }
    if (rx_len > available) rx_len = available;
    memcpy(uart2_rx_buf + uart2_rx_len, rx_frame, rx_len);
    uart2_rx_len = (uint16_t)(uart2_rx_len + rx_len);
    uart2_rx_buf[uart2_rx_len] = '\0';
  }

  if (uart2_rx_len == 0U) return;

  char    *buf = uart2_rx_buf;
  uint16_t len = uart2_rx_len;

  /* ── 1. 清理纯状态行 + 检测 OK/ERROR 清除 busy ── */
  {
    char *p = buf;
    while (*p) {
      char *eol = strpbrk(p, "\r\n");
      if (!eol) break;

      uint16_t line_len = (eol - p);
      uint16_t total    = line_len + 1;
      if (eol[0] == '\r' && eol[1] == '\n') total = line_len + 2;

      int is_status = 0;
      if (line_len == 2 && (strncmp(p, "OK", 2) == 0 || strncmp(p, "ok", 2) == 0)) {
        is_status = 1;
        at_cmd_busy = 0;              /* OK → 命令成功 */
        at_cmd_timeout_logged = 0;
        esp32_got_ok = 1;             /* 通知 init 状态机 */
        esp32_note_alive();
      } else if (line_len >= 5 && (strncmp(p, "ERROR", 5) == 0 || strncmp(p, "error", 5) == 0)) {
        is_status = 1;
        at_cmd_busy = 0;              /* ERROR → 命令失败 */
        at_cmd_timeout_logged = 0;
        esp32_got_ok = 1;             /* 也通知 init（失败也算响应） */
        if (esp32_initialized && esp_runtime_timeout_count < 0xFFU) {
          esp_runtime_timeout_count++;
        }
      } else if (line_len >= 15 && strncmp(p, "WIFI DISCONNECT", 15) == 0) {
        is_status = 1;
        if (esp32_initialized) esp32_request_reconnect("WIFI DISCONNECT");
      } else if (line_len >= 8 && strncmp(p, "WIFI", 4) == 0) {
        is_status = 1;
        if (strstr(p, "GOT IP") != NULL || strstr(p, "CONNECTED") != NULL) {
          esp32_note_alive();
        }
      } else if (line_len >= 9 && strncmp(p, "+CWSTATE:", 9) == 0) {
        int state = atoi(p + 9);
        is_status = 1;
        at_cmd_busy = 0;
        at_cmd_timeout_logged = 0;
        if (state == 2) {
          esp32_note_alive();
        } else if (esp32_initialized) {
          esp32_request_reconnect("CWSTATE offline");
        }
      } else if (strstr(p, "MQTTDISCONNECTED") != NULL) {
        is_status = 1;
        if (esp32_initialized) esp32_request_reconnect("MQTT disconnected");
      } else if (strncmp(p, "+MQTTPING:", 10) == 0 ||
                 strstr(p, "MQTTCONNECTED") != NULL) {
        is_status = 1;
        at_cmd_busy = 0;
        at_cmd_timeout_logged = 0;
        esp32_note_alive();
      } else if (strncmp(p, "+MQTT", 5) == 0 && strstr(p, "+MQTTSUBRECV:") == NULL) {
        is_status = 1;
        esp32_note_alive();
      } else if (line_len == 0) {
        is_status = 1;
      }

      if (esp32_initialized &&
          esp_runtime_timeout_count >= ESP_RUNTIME_TIMEOUT_RECONNECTS) {
        esp32_request_reconnect("runtime ERROR");
      }

      if (is_status) {
        uint16_t consumed = (p + total) - buf;
        consumed = consumed > len ? len : consumed;
        buf_consume(buf, &len, consumed);
        p = buf;
        continue;
      }
      /* +MQTTSUBRECV: 或 +CIPSNTPTIME: → 停止清理，保留给后续处理函数 */
      if (strncmp(p, "+MQTTSUBRECV:", 13) == 0 ||
          strncmp(p, "+CIPSNTPTIME:", 13) == 0) {
        break;
      }
      /* 其他非状态行（命令回显等）→ 跳过，继续扫描后面的 OK/ERROR */
      p = eol + 1;
    }
    uart2_rx_len = len;  /* 同步全局长度 */
  }

  /* ── 2. 检测 +MQTTSUBRECV（post/reply 到达 = 命令必然已完成）── */
  {
    char *sub_start = strstr(buf, "+MQTTSUBRECV:");
    if (sub_start != NULL) {
      char *msg_end = strstr(sub_start, "}\r\n");
      if (msg_end != NULL) {
        at_cmd_busy = 0;              /* post/reply 到达 = 命令已完成 */
        at_cmd_timeout_logged = 0;
        esp32_note_alive();
        MQTT_Handle(sub_start);

        uint16_t consumed = (msg_end + 3) - buf;
        buf_consume(buf, &len, consumed);
        uart2_rx_len = len;
      }
    }
  }

  /* ── 3. 检测 +CIPSNTPTIME ──────────────────── */
  {
    char *ntp_start = strstr(buf, "+CIPSNTPTIME:");
    if (ntp_start != NULL) {
      char *ntp_end = strstr(ntp_start, "\r\n");
      if (ntp_end != NULL) {
        TIME_Handle(ntp_start);

        uint16_t consumed = (ntp_end + 2) - buf;   /* +2 跳过 "\r\n" */
        buf_consume(buf, &len, consumed);
        uart2_rx_len = len;
      }
    }
  }

  esp32_rx_pending = 0;
}
/* ================================================================ */
/*  非阻塞初始化状态机（基于 CH32 esp8266_init_nonblock 逻辑）         */
/* ================================================================ */

typedef enum {
  ESP_INIT_IDLE,
  ESP_INIT_ATE,
  ESP_INIT_ATE_WAIT,
  ESP_INIT_RST,
  ESP_INIT_RST_WAIT,
  ESP_INIT_CWMODE,
  ESP_INIT_CWMODE_WAIT,
  ESP_INIT_CWJAP,
  ESP_INIT_CWJAP_WAIT,
  ESP_INIT_MQTTUSERCFG,
  ESP_INIT_MQTTUSERCFG_WAIT,
  ESP_INIT_MQTTCONN,
  ESP_INIT_MQTTCONN_WAIT,
  ESP_INIT_SUB_POST_REPLY,
  ESP_INIT_SUB_POST_REPLY_WAIT,
  ESP_INIT_SUB_SET,
  ESP_INIT_SUB_SET_WAIT,
  ESP_INIT_DONE,
  ESP_INIT_FAIL
} esp_init_state_t;

typedef struct {
  esp_init_state_t state;
  uint8_t  retry_count;
  uint32_t start_time;
  uint32_t timeout_ms;
  char     cmd_buf[256];
} esp_init_ctx_t;

static esp_init_ctx_t init_ctx = {ESP_INIT_IDLE, 0, 0, 0, {0}};

static void esp32_start_reconnect(void)
{
  uint32_t now = HAL_GetTick();

  if (last_reconnect_tick != 0U &&
      now - last_reconnect_tick < ESP_RECONNECT_MIN_INTERVAL_MS) {
    return;
  }

  last_reconnect_tick = now;
  esp32_initialized = 0;
  esp_reconnect_requested = 0U;
  esp_offline_flag = 0U;
  at_cmd_busy = 0U;
  at_cmd_timeout_logged = 0U;
  esp32_got_ok = 0U;
  esp_runtime_timeout_count = 0U;
  init_ctx.state = ESP_INIT_IDLE;
  init_ctx.retry_count = 0U;
  init_ctx.start_time = 0U;
  init_ctx.timeout_ms = 0U;
  memset(init_ctx.cmd_buf, 0, sizeof(init_ctx.cmd_buf));
  memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
  uart2_rx_len = 0U;
  esp32_rx_pending = 0U;
  my_uart2_clear_frames();
  last_esp_check_tick = now;
  last_mqtt_ping_tick = now;

  ESP32_UART1_STATUS_PRINTF("[ESP32] reconnect start\r\n");
}

/* 快速检查 uart2 接收缓冲区是否包含期望字符串，找到后清空 */
static uint8_t check_uart2_response(const char *expected)
{
  if (uart2_rx_len > 0 && strstr(uart2_rx_buf, expected) != NULL) {
    at_cmd_busy = 0;
    at_cmd_timeout_logged = 0;
    esp32_note_alive();
    memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
    uart2_rx_len = 0;
    return 1;
  }
  return 0;
}

/**
  * @brief  ESP32 非阻塞初始化（每轮调用一次，无阻塞延时）
  * @note   状态机驱动，依次执行：ATE → RST → CWMODE → CWJAP →
  *         MQTTUSERCFG → MQTTCONN → SUB_POST_REPLY → SUB_SET → DONE
  *         每步支持 3 次重试，失败后等待 5s 自动重新初始化
  */
void esp32_init_nonblock(void)
{
  if (esp32_initialized) return;

  /* 失败冷却：等待 5s 后重新开始 */
  if (init_ctx.state == ESP_INIT_FAIL) {
    static uint32_t fail_start = 0;
    if (fail_start == 0) fail_start = HAL_GetTick();
    if (HAL_GetTick() - fail_start >= ESP_FAIL_COOLDOWN_MS) {
      fail_start = 0;
      init_ctx.state = ESP_INIT_IDLE;
      init_ctx.retry_count = 0;
      memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
      uart2_rx_len = 0;
      esp32_rx_pending = 0;
      my_uart2_clear_frames();
      esp_reconnect_requested = 0U;
      esp_offline_flag = 0U;
      esp_runtime_timeout_count = 0U;
      ESP32_UART1_STATUS_PRINTF("[ESP32] re-init after fail cooldown\r\n");
    }
    return;
  }

  switch (init_ctx.state) {

  /* ── IDLE: 准备开始 ─────────────────────────── */
  case ESP_INIT_IDLE:
    ESP32_UART1_STATUS_PRINTF("[ESP32] init start...\r\n");
    init_ctx.state = ESP_INIT_ATE;
    init_ctx.retry_count = 0;
    memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
    uart2_rx_len = 0;
    break;

  /* ── ATE0: 关闭回显，减少 UART 流量，降低帧碰撞 ── */
  case ESP_INIT_ATE:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    uart_printf(&huart2, "ATE0\r\n");
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = 2000;
    init_ctx.state = ESP_INIT_ATE_WAIT;
    break;
  case ESP_INIT_ATE_WAIT:
    if (esp32_got_ok || check_uart2_response("OK")) {
      ESP32_UART1_STATUS_PRINTF("[ESP32] init: ATE ok\r\n");
      init_ctx.state = ESP_INIT_RST;
    } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
      at_cmd_busy = 0;
      if (++init_ctx.retry_count >= 3) init_ctx.state = ESP_INIT_FAIL;
      else init_ctx.state = ESP_INIT_ATE;
    }
    break;

  /* ── AT+RST: 模块复位 ────────────────────────── */
  case ESP_INIT_RST:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    uart_printf(&huart2, "AT+RST\r\n");
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = ESP_RST_WAIT_MS;
    init_ctx.state = ESP_INIT_RST_WAIT;
    break;
  case ESP_INIT_RST_WAIT:
    /* RST 强制等待 3s（复位期间不检查 OK） */
    if (HAL_GetTick() - init_ctx.start_time >= init_ctx.timeout_ms) {
      at_cmd_busy = 0;  /* RST 等待结束，清除 busy */
      memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
      uart2_rx_len = 0;
      init_ctx.retry_count = 0;
      init_ctx.state = ESP_INIT_CWMODE;
    }
    break;

  /* ── CWMODE=1: STA 模式 ──────────────────────── */
  case ESP_INIT_CWMODE:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    uart_printf(&huart2, "AT+CWMODE=1\r\n");
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = 2000;
    init_ctx.state = ESP_INIT_CWMODE_WAIT;
    break;
  case ESP_INIT_CWMODE_WAIT:
    if (esp32_got_ok || check_uart2_response("OK")) {
      init_ctx.state = ESP_INIT_CWJAP;
    } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
      at_cmd_busy = 0;  /* 超时清除 busy */
      if (++init_ctx.retry_count >= 3) init_ctx.state = ESP_INIT_FAIL;
      else init_ctx.state = ESP_INIT_CWMODE;
    }
    break;

  /* ── CWJAP: 连接 WiFi ────────────────────────── */
  case ESP_INIT_CWJAP:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    sprintf(init_ctx.cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    uart_printf(&huart2, "%s", init_ctx.cmd_buf);
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = 10000;
    init_ctx.state = ESP_INIT_CWJAP_WAIT;
    break;
  case ESP_INIT_CWJAP_WAIT:
    if (esp32_got_ok || check_uart2_response("OK")) {
      ESP32_UART1_STATUS_PRINTF("[ESP32] init: WiFi connected\r\n");
      init_ctx.state = ESP_INIT_MQTTUSERCFG;
    } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
      at_cmd_busy = 0;  /* 超时清除 busy */
      if (++init_ctx.retry_count >= 3) init_ctx.state = ESP_INIT_FAIL;
      else init_ctx.state = ESP_INIT_CWJAP;
    }
    break;

  /* ── MQTTUSERCFG: 用户配置 ──────────────────── */
  case ESP_INIT_MQTTUSERCFG:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    sprintf(init_ctx.cmd_buf,
            "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
            DEVICE_NAME, PRODUCT_ID, MQTT_TOKEN);
    uart_printf(&huart2, "%s", init_ctx.cmd_buf);
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = 8000;
    init_ctx.state = ESP_INIT_MQTTUSERCFG_WAIT;
    break;
  case ESP_INIT_MQTTUSERCFG_WAIT:
    if (esp32_got_ok || check_uart2_response("OK")) {
      init_ctx.state = ESP_INIT_MQTTCONN;
    } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
      at_cmd_busy = 0;  /* 超时清除 busy */
      if (++init_ctx.retry_count >= 3) init_ctx.state = ESP_INIT_FAIL;
      else init_ctx.state = ESP_INIT_MQTTUSERCFG;
    }
    break;

  /* ── MQTTCONN: 连接 OneNET ──────────────────── */
  case ESP_INIT_MQTTCONN:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    sprintf(init_ctx.cmd_buf, "AT+MQTTCONN=0,\"%s\",%d,1\r\n", MQTT_SERVER, MQTT_PORT);
    uart_printf(&huart2, "%s", init_ctx.cmd_buf);
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = 8000;
    init_ctx.state = ESP_INIT_MQTTCONN_WAIT;
    break;
  case ESP_INIT_MQTTCONN_WAIT:
    if (esp32_got_ok || check_uart2_response("OK")) {
      ESP32_UART1_STATUS_PRINTF("[ESP32] init: MQTT connected\r\n");
      init_ctx.state = ESP_INIT_SUB_POST_REPLY;
    } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
      at_cmd_busy = 0;  /* 超时清除 busy */
      if (++init_ctx.retry_count >= 3) init_ctx.state = ESP_INIT_FAIL;
      else init_ctx.state = ESP_INIT_MQTTCONN;
    }
    break;

  /* ── SUB: post/reply 主题 ──────────────────── */
  case ESP_INIT_SUB_POST_REPLY:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    sprintf(init_ctx.cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n", TOPIC_POST_RELAY);
    uart_printf(&huart2, "%s", init_ctx.cmd_buf);
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = 5000;
    init_ctx.state = ESP_INIT_SUB_POST_REPLY_WAIT;
    break;
  case ESP_INIT_SUB_POST_REPLY_WAIT:
    if (esp32_got_ok || check_uart2_response("OK")) {
      init_ctx.state = ESP_INIT_SUB_SET;
    } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
      at_cmd_busy = 0;  /* 超时清除 busy */
      if (++init_ctx.retry_count >= 3) init_ctx.state = ESP_INIT_FAIL;
      else init_ctx.state = ESP_INIT_SUB_POST_REPLY;
    }
    break;

  /* ── SUB: property/set 主题 ─────────────────── */
  case ESP_INIT_SUB_SET:
    if (at_cmd_busy) break;
    at_cmd_busy = 1;
    at_cmd_timeout_logged = 0;
    esp32_got_ok = 0;
    sprintf(init_ctx.cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n", TOPIC_SET);
    uart_printf(&huart2, "%s", init_ctx.cmd_buf);
    init_ctx.start_time = HAL_GetTick();
    init_ctx.timeout_ms = 5000;
    init_ctx.state = ESP_INIT_SUB_SET_WAIT;
    break;
  case ESP_INIT_SUB_SET_WAIT:
    if (esp32_got_ok || check_uart2_response("OK")) {
      init_ctx.state = ESP_INIT_DONE;
      esp32_initialized = 1;
      esp_reconnect_requested = 0U;
      esp_offline_flag = 0U;
      esp32_note_alive();
      ESP32_UART1_STATUS_PRINTF("[ESP32] nonblock init done!\r\n");
    } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
      at_cmd_busy = 0;  /* 超时清除 busy */
      if (++init_ctx.retry_count >= 3) init_ctx.state = ESP_INIT_FAIL;
      else init_ctx.state = ESP_INIT_SUB_SET;
    }
    break;

  /* ── DONE / FAIL ────────────────────────────── */
  case ESP_INIT_DONE:
    esp32_initialized = 1;
    break;
  case ESP_INIT_FAIL:
  default:
    ESP32_UART1_STATUS_PRINTF("[ESP32] nonblock init failed!\r\n");
    break;
  }
}

/* ================================================================ */
/*  在线检测：MQTT PING + WiFi 状态查询                                */
/* ================================================================ */

/**
  * @brief  ESP32 在线状态维护
  *         - 每 30s 发送 AT+MQTTPING 保持连接
  *         - 每 10s 发送 AT+CWSTATE? 检测 WiFi 状态
  *         - 检测到离线后触发重新初始化
  */
void esp32_check_online(void)
{
  uint32_t now = HAL_GetTick();

  /* 离线重连：检测到离线标志，重置初始化状态 */
  if (esp_reconnect_requested || esp_offline_flag) {
    esp32_start_reconnect();
    return;
  }

  if (!esp32_initialized) return;

  if (last_esp_alive_tick != 0U &&
      now - last_esp_alive_tick >= ESP_NO_ALIVE_RECONNECT_MS) {
    esp32_request_reconnect("no alive response");
    esp32_start_reconnect();
    return;
  }

  /* MQTT PING: 每 30s */
  if (now - last_mqtt_ping_tick >= MQTT_PING_INTERVAL_MS) {
    last_mqtt_ping_tick = now;
    if (!at_cmd_busy) {
      uart_printf(&huart2, "AT+MQTTPING=0\r\n");
      at_cmd_busy = 1;
      at_cmd_start_tick = now;
      at_cmd_timeout_logged = 0;
    }
  }

  /* WiFi 状态查询: 每 10s */
  if (now - last_esp_check_tick >= ESP_CHECK_INTERVAL_MS) {
    last_esp_check_tick = now;
    if (!at_cmd_busy) {
      uart_printf(&huart2, "AT+CWSTATE?\r\n");
      at_cmd_busy = 1;
      at_cmd_start_tick = now;
      at_cmd_timeout_logged = 0;
    }
  }
}

uint8_t esp32_get_online(void)
{
  return (esp32_initialized && !esp_reconnect_requested && !esp_offline_flag) ? 1U : 0U;
}

/**
  * @brief  ESP32 阻塞式初始化（保留，非阻塞方式未完成时使用）
  *         依次执行：AT指令回显设置、复位、STA模式配置、
  *         WiFi连接、MQTT用户配置、MQTT连接、主题订阅
  */
void esp32_init(void) {
  static char cmd_buf[512];
  int8_t ret;

  ret  = send_cmd_wait_resp_it(&huart2, "ATE0\r\n", "OK", 2000, 3);
  uart_printf(&huart2, "AT+RST\r\n");
  HAL_Delay(3000);
  ret += send_cmd_wait_resp_it(&huart2, "AT+CWMODE=1\r\n", "OK", 2000, 3);
  sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
  ret += send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 10000, 3);
  sprintf(cmd_buf, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
          DEVICE_NAME, PRODUCT_ID, MQTT_TOKEN);
  ret += send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 8000, 3);
  sprintf(cmd_buf, "AT+MQTTCONN=0,\"%s\",%d,1\r\n", MQTT_SERVER, MQTT_PORT);
  ret += send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 8000, 3);
  sprintf(cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n", TOPIC_POST_RELAY);
  ret += send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 5000, 3);
  sprintf(cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n", TOPIC_SET);
  ret += send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 5000, 3);
  send_cmd_wait_resp_it(&huart2, "AT+CIPSNTPCFG=1,8,\"ntp1.aliyun.com\"\r\n", "OK", 2000, 3);

  /* 只有全部成功（ret==0）才标记初始化完成 */
  if (ret == 0) {
    esp32_initialized = 1;
    ESP32_UART1_STATUS_PRINTF("[ESP32] blocking init done!\r\n");
  } else {
    ESP32_UART1_STATUS_PRINTF(
        "[ESP32] blocking init FAILED (%d), fallback to nonblock\r\n", ret);
    /* esp32_initialized 保持 0，非阻塞 init 会接管 */
  }
}
/**
  * @brief  发送 AT 指令并等待期望响应（带超时和重试机制，阻塞式）
  * @param huart         串口句柄指针
  * @param cmd           要发送的 AT 指令字符串
  * @param expected_resp 期望的响应关键字（如 "OK"）
  * @param time_out_ms   每次等待的超时时间（毫秒）
  * @param max_retries   最大重试次数
  * @return 0=成功收到期望响应, -1=重试后仍失败
  */
int8_t send_cmd_wait_resp_it(UART_HandleTypeDef *huart, char *cmd,
                             char *expected_resp, uint32_t time_out_ms,
                             uint8_t max_retries) {
  uint8_t retry_count = 0;
  uint32_t start_time = 0;
  while (retry_count < max_retries) {
    /* 清除接收缓冲区中的残留数据 */
    memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
    uart2_rx_len = 0;

    /* 发送 AT 指令 */
    uart_printf(huart, "%s", cmd);
    start_time = HAL_GetTick();
    /* 在超时时间内循环检测响应 */
    while ((HAL_GetTick() - start_time) < time_out_ms) {
      /* 如果收到期望的响应关键字，返回成功 */
      if (strstr(uart2_rx_buf, expected_resp) != NULL) {
        ESP32_UART1_STATUS_PRINTF("%s", uart2_rx_buf);
        memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
        uart2_rx_len = 0;
        return 0;
      }
      HAL_Delay(10);
    }
    /* 超时未收到期望响应，打印错误信息并重试 */
      ESP32_UART1_STATUS_PRINTF("%s", uart2_rx_buf);
    
    retry_count++;
    HAL_Delay(500);
  }
  return -1; /* 重试耗尽，返回失败 */
}
/**
  * @brief  构建 OneNET 标准属性上报命令（AT+MQTTPUB 格式）
  *         支持整型、浮点型、布尔型、字符串型参数的 JSON 打包
  * @param outbuf       输出缓冲区（存放完整的 AT 指令）
  * @param topic        MQTT 发布主题
  * @param msg_id       消息 ID
  * @param param_count  可变参数的数量
  * @param ...          可变参数列表，格式为：(char *key, int type, value)
  *                     type='i' → int, type='f' → double,
  *                     type='b' → int (0=false, 非0=true),
  *                     type='s' → char*
  */
void build_onenet_cmd(char *outbuf, const char *topic, const char *msg_id,
                      uint8_t param_count, ...) {
  static char payload[512];
  memset(payload, 0, sizeof(payload));
  int offset = 0; /* 当前已写入 payload 的字符偏移量 */
  va_list ap;
  va_start(ap, param_count);
  /* JSON 头部：id、version 和 params 起始 */
  offset += sprintf(
      payload + offset,
      "{\\\"id\\\":\\\"%s\\\"\\,\\\"version\\\":\\\"1.0\\\"\\,\\\"params\\\":{",
      msg_id);
  for (uint8_t i = 0; i < param_count; i++) {
    char *key = va_arg(ap, char *);  /* 参数名 */
    int type = va_arg(ap, int);      /* 参数类型标识 */
    /* 除第一个参数外，前面加上逗号分隔 */
    if (i > 0)
      offset += sprintf(payload + offset, "\\,");
    /* 根据参数类型构建对应的 JSON value 字段 */
    if (type == 'i') {
      /* 整型：{"key":{"value":123}} */
      int val = va_arg(ap, int);
      offset +=
          sprintf(payload + offset, "\\\"%s\\\":{\\\"value\\\":%d}", key, val);
    } else if (type == 'f') {
      /* 浮点型：{"key":{"value":12.34}} */
      double val = va_arg(ap, double);
      offset += sprintf(payload + offset, "\\\"%s\\\":{\\\"value\\\":%.2f}",
                        key, val);
    } else if (type == 'b') {
      /* 布尔型：{"key":{"value":true}} 或 {"key":{"value":false}} */
      int val = va_arg(ap, int);
      offset += sprintf(payload + offset, "\\\"%s\\\":{\\\"value\\\":%s}",
                        key, val ? "true" : "false");
    } else if (type == 's') {
      /* 字符串型：{"key":{"value":"abc"}} */
      char *val = va_arg(ap, char *);
      offset += sprintf(payload + offset,
                        "\\\"%s\\\":{\\\"value\\\":\\\"%s\\\"}", key, val);
    }
  }
  va_end(ap);
  /* JSON 尾部闭合 */
  sprintf(payload + offset, "}}");
  /* 组装完整的 AT+MQTTPUB 命令 */
  sprintf(outbuf, "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n", topic, payload);
}


void MQTT_Handle(char *subrecv_start)
{
  /* 检查是否收到完整的 JSON 帧（以 "}\r\n" 结尾）*/
  if (strstr(subrecv_start, "}\r\n") == NULL) {
    return; /* 数据尚未收完，等待下次接收 */
  }

  /* 提取主题（topic）和 JSON 负载 */
  static char topic[128] = {0};
  static char json_buf[512] = {0};

  extract_topic(subrecv_start, topic, sizeof(topic));
  extract_json(subrecv_start, json_buf, sizeof(json_buf));

  /* 调试打印（已注释，需要时可启用）*/
 // uart_printf(&huart1, "[RECV] topic=%s\r\n", topic);
 // uart_printf(&huart1, "[RECV] json=%s\r\n", json_buf);

  /* 根据主题判断消息类型 */
  MqttMsgType_t msg_type = get_msg_type(topic);

  switch (msg_type) {

  /* ========== 属性上报响应（post/reply）========== */
  case MSG_POST_REPLY: {
    int code = 0;
    uint8_t found[1] = {0};

    /* 解析返回码 */
    parse_onenet_params(json_buf, 1, found, "code", 'i', &code);

    if (found[0] && code == 200) {
      ESP32_UART1_STATUS_PRINTF("\r\n[MSG] up success(code=%d)\r\n", code);
    } else {
      ESP32_UART1_STATUS_PRINTF("\r\n[MSG] up false (code=%d)\r\n", code);
    }
    break;
  }

  /* ========== 云端下发的属性设置（property/set）========== */
  case MSG_PROPERTY_SET: {
    int led = 0, fan_val = 0, auto_enabled = 0, fan_mode = 0;
    int temp_low_c10 = 0, temp_mid_c10 = 0, temp_high_c10 = 0;
    int light_on_lux = 0, light_off_lux = 0;
    int pm25_limit = 0, smoke_limit_ppm = 0;
    int light_target = 0, light_preset = 0;
    int rgb_all_preset = 0, rgb1_preset = 0, rgb2_preset = 0, rgb3_preset = 0;
    char light_preset_name[64] = {0};

    /* 基础控制：LED + 风扇（2 个字段）*/
    uint8_t found[11] = {0};
    parse_onenet_params(json_buf, 11, found,
        "LED", 'b', &led,
        "fan", 'i', &fan_val,
        "auto_enabled", 'b', &auto_enabled,
        "fan_mode", 'i', &fan_mode,
        "temp_low_c10", 'i', &temp_low_c10,
        "temp_mid_c10", 'i', &temp_mid_c10,
        "temp_high_c10", 'i', &temp_high_c10,
        "light_on_lux", 'i', &light_on_lux,
        "light_off_lux", 'i', &light_off_lux,
        "pm25_limit", 'i', &pm25_limit,
        "smoke_limit_ppm", 'i', &smoke_limit_ppm);

    /* 灯带预设控制。优先推荐 per-strip 字段；通用 light_target +
       light_preset/light_preset_name 可用于一个下拉框控制指定灯带。 */
    uint8_t preset_found[7] = {0};
    parse_onenet_params(json_buf, 7, preset_found,
        "light_target", 'i', &light_target,
        "light_preset", 'i', &light_preset,
        "light_preset_name", 's', light_preset_name,
        "RGBALL_PRESET", 'i', &rgb_all_preset,
        "RGB1_PRESET", 'i', &rgb1_preset,
        "RGB2_PRESET", 'i', &rgb2_preset,
        "RGB3_PRESET", 'i', &rgb3_preset);

    /* ── LED 控制 ──────────────────────────── */
    if (found[0]) {
      device_state_set_board_led(led ? 1U : 0U, DEVICE_SOURCE_CLOUD);
      ESP32_UART1_STATUS_PRINTF("[CTRL] LED %s\r\n", led ? "ON" : "OFF");
    }

    /* ── 风扇控制 ──────────────────────────── */
    if (found[1]) {
      device_state_set_fan((uint16_t)(fan_val < 0 ? 0 : fan_val),
                           DEVICE_FAN_MANUAL, DEVICE_SOURCE_CLOUD);
      ESP32_UART1_STATUS_PRINTF("[CTRL] fan=%d\r\n", fan_val);
    }

    if (found[3] && fan_mode >= DEVICE_FAN_OFF && fan_mode <= DEVICE_FAN_AUTO) {
      device_state_set_fan_mode((device_fan_mode_t)fan_mode,
                                DEVICE_SOURCE_CLOUD);
    }

    if (found[2]) {
      /* When both fields arrive, auto_enabled is the final authority. */
      device_state_set_auto_enabled(auto_enabled ? 1U : 0U,
                                    DEVICE_SOURCE_CLOUD);
    }

    if (found[4] && found[5] && found[6]) {
      device_state_set_temperature_thresholds((int16_t)temp_low_c10,
                                              (int16_t)temp_mid_c10,
                                              (int16_t)temp_high_c10,
                                              DEVICE_SOURCE_CLOUD);
    }
    if (found[7] && found[8]) {
      device_state_set_light_thresholds((uint16_t)(light_on_lux < 0 ? 0 : light_on_lux),
                                        (uint16_t)(light_off_lux < 0 ? 0 : light_off_lux),
                                        DEVICE_SOURCE_CLOUD);
    }
    if (found[9] && found[10]) {
      device_state_set_air_limits((uint16_t)(pm25_limit < 0 ? 0 : pm25_limit),
                                  (uint16_t)(smoke_limit_ppm < 0 ? 0 : smoke_limit_ppm),
                                  DEVICE_SOURCE_CLOUD);
    }

    /* ── 灯带预设：先应用预设，后面的精确 RGB 字段可覆盖预设结果 ── */
    if (preset_found[1] || preset_found[2]) {
      int preset_id = preset_found[1]
                          ? light_preset
                          : esp32_light_preset_id_from_name(light_preset_name);
      uint8_t target = ESP_LIGHT_TARGET_ALL;

      if (preset_found[0]) {
        if (light_target == (int)ESP_LIGHT_TARGET_ALL) {
          target = ESP_LIGHT_TARGET_ALL;
        } else if (light_target >= 1 && light_target <= DEVICE_STATE_STRIP_COUNT) {
          target = (uint8_t)light_target;
        } else {
          target = 0U;
        }
      }

      if (preset_id >= 0 && esp32_apply_light_preset(target, preset_id)) {
        ESP32_UART1_STATUS_PRINTF("[CTRL] light target=%d preset=%d\r\n",
                                  preset_found[0] ? light_target : ESP_LIGHT_TARGET_ALL,
                                  preset_id);
      } else {
        ESP32_UART1_STATUS_PRINTF("[CTRL] light preset invalid target=%d preset=%d name=%s\r\n",
                                  preset_found[0] ? light_target : ESP_LIGHT_TARGET_ALL,
                                  preset_id, light_preset_name);
      }
    }

    if (preset_found[3]) {
      if (esp32_apply_light_preset(ESP_LIGHT_TARGET_ALL, rgb_all_preset)) {
        ESP32_UART1_STATUS_PRINTF("[CTRL] RGBALL_PRESET=%d\r\n", rgb_all_preset);
      }
    }
    if (preset_found[4]) {
      if (esp32_apply_light_preset_to_strip(DEVICE_STRIP_INDOOR_ID, rgb1_preset)) {
        ESP32_UART1_STATUS_PRINTF("[CTRL] RGB1_PRESET=%d\r\n", rgb1_preset);
      }
    }
    if (preset_found[5]) {
      ESP32_UART1_STATUS_PRINTF("[CTRL] RGB2_PRESET reserved, ignored\r\n");
    }
    if (preset_found[6]) {
      if (esp32_apply_light_preset_to_strip(DEVICE_STRIP_OUTDOOR_ID, rgb3_preset)) {
        ESP32_UART1_STATUS_PRINTF("[CTRL] RGB3_PRESET=%d\r\n", rgb3_preset);
      }
    }

    /* ── 灯带 RGB：1=室内, 3=室外；2=原入户通道保留但不使用 ── */
    for (uint8_t sid = 1; sid <= DEVICE_STATE_STRIP_COUNT; sid++) {
        char rn[16], gn[16], bn[16];
        int  r_val = 0, g_val = 0, b_val = 0;
        uint8_t rgb_found[3] = {0};

        snprintf(rn, sizeof(rn), "RGB%d_RAD",   sid);
        snprintf(gn, sizeof(gn), "RGB%d_GREEN", sid);
        snprintf(bn, sizeof(bn), "RGB%d_BLUE",  sid);

        parse_onenet_params(json_buf, 3, rgb_found,
            rn, 'i', &r_val,
            gn, 'i', &g_val,
            bn, 'i', &b_val);

        if (rgb_found[0] || rgb_found[1] || rgb_found[2]) {
            if (!device_state_strip_is_active(sid)) {
                ESP32_UART1_STATUS_PRINTF("[CTRL] RGB%d reserved, ignored\r\n", sid);
                continue;
            }

            /* 只更新云台下发的通道，其余保持当前值 */
            uint8_t r, g, b;
            if (!device_state_get_strip_rgb(sid, &r, &g, &b)) continue;
            if (rgb_found[0]) r = (uint8_t)(r_val < 0 ? 0 : r_val > 255 ? 255 : r_val);
            if (rgb_found[1]) g = (uint8_t)(g_val < 0 ? 0 : g_val > 255 ? 255 : g_val);
            if (rgb_found[2]) b = (uint8_t)(b_val < 0 ? 0 : b_val > 255 ? 255 : b_val);

            if (sid <= device_state_get_strip_count()) {
                device_state_set_strip_rgb(sid, r, g, b, DEVICE_SOURCE_CLOUD);
                ESP32_UART1_STATUS_PRINTF(
                    "[CTRL] RGB%d R=%d G=%d B=%d\r\n", sid, r, g, b);
            }
        }
    }

    /* 向云端回复设置成功响应 */
    char msg_id[16] = {0};
    if (json_get_msg_id(json_buf, msg_id, sizeof(msg_id))) {
      queue_set_reply(msg_id);
    }
    break;
  }

  /* ========== 属性设置响应（set_reply）========== */
  case MSG_SET_REPLY: {
    ESP32_UART1_STATUS_PRINTF("[MSG]cmd setted\r\n");
    break;
  }

  /* ========== 未知消息类型 ========== */
  default: {
    ESP32_UART1_STATUS_PRINTF("[MSG] don't know\r\n");
    break;
  }
  }

}
static char ntp_time_str[64] = {0};          // 存放时间字符串（64字节足够）
volatile static uint8_t ntp_updated = 0;    // 时间更新标志
void TIME_Handle(char *timerecv_start)
{
  // 跳过前缀 "+CIPSNTPTIME:"
    char *p = timerecv_start + strlen("+CIPSNTPTIME:");

    // 去除开头的空格（如果有）
    while (*p == ' ') p++;

    // 查找字符串结尾（遇到 \r 或 \n 停止）
    char *end = strstr(p, "\r\n");
    if (end == NULL) {
        // 也可能是单独的 \r 或 \n
        end = strchr(p, '\r');
        if (end == NULL) end = strchr(p, '\n');
    }
    if (end) {
        *end = '\0';  // 截断字符串
    }

    // 复制到全局变量
    strncpy(ntp_time_str, p, sizeof(ntp_time_str) - 1);
    ntp_time_str[sizeof(ntp_time_str) - 1] = '\0';

    // 标记更新
    ntp_updated = 1;

    // 调试打印（可选）
    ESP32_UART1_STATUS_PRINTF("[NTP] Time: %s\r\n", ntp_time_str);

}


