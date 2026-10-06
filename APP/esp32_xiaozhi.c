#include "esp32_xiaozhi.h"
#include "esp32_backend.h"
#include "esp32.h"

#include "BH1750.h"
#include "dht11.h"
#include "PM25.h"
#include "board_led.h"
#include "fan.h"
#include "home_cmd.h"
#include "mqtt_config.h"
#include "my_uart.h"
#include "smoke.h"
#include "status_led.h"
#include "ws2812.h"
#include "ws2812_2.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ESP32_XIAOZHI_STATE_BUF_SIZE 768U
/* 一条 home/cmd 负载的上限；超出直接丢弃（对端最长也就百来字节） */
#define ESP32_XIAOZHI_CMD_BUF_SIZE 512U
#define ESP32_XIAOZHI_NTP_QUERY_INTERVAL_MS 60000U
#define ESP32_XIAOZHI_PING_INTERVAL_MS 30000U
#define ESP32_XIAOZHI_CHECK_INTERVAL_MS 10000U
#define ESP32_XIAOZHI_RST_WAIT_MS 3000U
#define ESP32_XIAOZHI_FAIL_COOLDOWN_MS 30000U
/* AT+MQTTPUBRAW 从 OK 到 > 的等待上限。ESP-AT 文档允许 MQTT 命令最多 10s，
 * 比通用 AT 超时（5s）宽松，见 esp32_xiaozhi_at_busy_guard()。 */
#define ESP32_XIAOZHI_PUBRAW_PROMPT_TIMEOUT_MS 10000U
/* home/cmd 订阅没成功时，多久补试一次 */
#define ESP32_XIAOZHI_SUB_RETRY_INTERVAL_MS 60000U
/* 链路断了多久还没自己回来，就升级成重走一遍连接流程 */
#define ESP32_XIAOZHI_LINK_RECOVER_MS 90000U

#define ESP32_XIAOZHI_DEBUG 0

#if ESP32_XIAOZHI_DEBUG
#define ESP32_XIAOZHI_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define ESP32_XIAOZHI_DEBUG_PRINTF(...) ((void)0)
#endif

typedef enum {
  ESP32_XIAOZHI_INIT_IDLE,
  ESP32_XIAOZHI_INIT_ATE,
  ESP32_XIAOZHI_INIT_ATE_WAIT,
  ESP32_XIAOZHI_INIT_RST,
  ESP32_XIAOZHI_INIT_RST_WAIT,
  ESP32_XIAOZHI_INIT_CWMODE,
  ESP32_XIAOZHI_INIT_CWMODE_WAIT,
  ESP32_XIAOZHI_INIT_CWJAP,
  ESP32_XIAOZHI_INIT_CWJAP_WAIT,
  ESP32_XIAOZHI_INIT_MQTTDISC,
  ESP32_XIAOZHI_INIT_MQTTDISC_WAIT,
  ESP32_XIAOZHI_INIT_MQTTUSERCFG,
  ESP32_XIAOZHI_INIT_MQTTUSERCFG_WAIT,
  ESP32_XIAOZHI_INIT_MQTTCONN,
  ESP32_XIAOZHI_INIT_MQTTCONN_WAIT,
  ESP32_XIAOZHI_INIT_SUB_CMD,
  ESP32_XIAOZHI_INIT_SUB_CMD_WAIT,
  ESP32_XIAOZHI_INIT_NTPCFG,
  ESP32_XIAOZHI_INIT_NTPCFG_WAIT,
  ESP32_XIAOZHI_INIT_DONE,
  ESP32_XIAOZHI_INIT_FAIL
} esp32_xiaozhi_init_state_t;

typedef struct {
  esp32_xiaozhi_init_state_t state;
  uint8_t retry_count;
  uint32_t start_time;
  uint32_t timeout_ms;
  char cmd_buf[256];
} esp32_xiaozhi_init_ctx_t;

static esp32_xiaozhi_init_ctx_t init_ctx = {
    ESP32_XIAOZHI_INIT_IDLE, 0U, 0U, 0U, {0}};

static char raw_payload[ESP32_XIAOZHI_STATE_BUF_SIZE] = {0};
static uint16_t raw_payload_len = 0U;
static uint8_t raw_active = 0U;
static uint8_t raw_wait_prompt = 0U;
/* 等 '>' 的起始时刻，用于给 AT+MQTTPUBRAW 单独计时（见 at_busy_guard） */
static uint32_t raw_prompt_tick = 0U;
/* 收到 home/cmd 后置位：下一次 esp32_xiaozhi_run_send 跳过发布周期立刻上报。
 * 命令排出去就清掉；AT 忙或串口写失败时保留到下一轮重试。 */
static volatile uint8_t publish_now = 0U;

static char cmd_json[ESP32_XIAOZHI_CMD_BUF_SIZE] = {0};

static uint32_t smart_home_unix_ts = 0U;
static uint32_t last_ntp_query_tick = 0U;
static uint32_t last_mqtt_ping_tick = 0U;
static uint32_t last_esp_check_tick = 0U;
/* home/cmd 是否订阅成功。0 时"能上报、控制没反应"，所以除了点亮状态灯，
 * 还要在 esp32_xiaozhi_check_online() 里定期补订阅。 */
static uint8_t sub_ok = 0U;
static uint32_t last_sub_retry_tick = 0U;

/* MQTT 连接状态。AT+MQTTCONN 成功时置位，+MQTTCONNECTED / +MQTTDISCONNECTED
 * 分别置位和清零。链路没通时不发布，免得每 3s 白挨一条 ERROR。
 * （WiFi 状态不用单独记：WiFi 断了 MQTT 必然也跟着断，见 WIFI DISCONNECT 分支。） */
static uint8_t mqtt_up = 0U;
/* 链路从"通"变成"不通"的时刻，用于分级恢复（见 esp32_xiaozhi_check_online） */
static uint32_t link_down_tick = 0U;

/* 连续多少轮初始化都失败。攒够了才做 AT+RST 硬复位 —— 见初始化失败的处理。 */
static uint8_t fail_cycles = 0U;

static void raw_reset(void)
{
  raw_payload_len = 0U;
  raw_payload[0] = '\0';
  raw_active = 0U;
  raw_wait_prompt = 0U;
}

void esp32_xiaozhi_reset_publish(void)
{
  raw_reset();
}

void esp32_xiaozhi_publish_now(void)
{
  publish_now = 1U;
}

/*
 * 交给 esp32_check_cmd_timeout() 的"这条 AT 命令自己计时"钩子。
 *
 * 为什么需要：AT+MQTTPUBRAW 的应答是 OK 和 > 两条，看到 > 之后我们才发负载。
 * 如果通用 5s 超时在等 > 期间把 at_cmd_busy 清掉，后续 AT 指令就会被 ESP-AT
 * 当成 MQTT 负载吃掉，AT 通道从此错位几十秒。所以等 > 期间不让通用超时收尾，
 * 由这里自己计时。
 *
 * 返回 1 = 还在等 >，通用超时先别管；
 * 返回 0 = 已经收尾（或本来就没在等），让通用超时照常走。
 *
 * 等超时了不能只是放弃：ESP-AT 此刻多半正卡在"还差 length 个字节"的数据阶段，
 * 于是主动把 length 个字节补完，让它走完这次发布、恢复同步。这次发布上去的
 * 内容会是垃圾，但 home/state 是 retained + 3s 周期上报，下一条就会覆盖掉 ——
 * 比让 AT 通道错位几十秒（连带 home/cmd 收不到）要划算得多。
 */
uint8_t esp32_xiaozhi_at_busy_guard(void)
{
  if (!raw_wait_prompt) return 0U;

  if (HAL_GetTick() - raw_prompt_tick <= ESP32_XIAOZHI_PUBRAW_PROMPT_TIMEOUT_MS) {
    return 1U;
  }

  ESP32_XIAOZHI_DEBUG_PRINTF("[MQTT] pubraw prompt timeout, flush payload\r\n");
  if (raw_payload_len > 0U) {
    (void)my_uart_write(&huart2, (const uint8_t *)raw_payload, raw_payload_len);
  }
  raw_reset();
  return 0U;
}

static uint8_t state_append(char *buffer,
                            uint16_t capacity,
                            uint16_t *offset,
                            const char *format, ...)
{
  va_list ap;
  int written;

  if (buffer == NULL || offset == NULL || format == NULL ||
      *offset >= capacity) {
    return 0U;
  }

  va_start(ap, format);
  written = vsnprintf(buffer + *offset, capacity - *offset, format, ap);
  va_end(ap);

  if (written < 0 || (uint16_t)written >= (uint16_t)(capacity - *offset)) {
    return 0U;
  }

  *offset = (uint16_t)(*offset + (uint16_t)written);
  return 1U;
}

static uint16_t build_state_payload(char *payload, uint16_t capacity)
{
  uint16_t offset = 0U;
  const char *online = esp32_initialized ? "true" : "false";

  if (!state_append(
          payload, capacity, &offset,
          "{\"source\":\"smarthome-bridge\",\"version\":\"1.0.0\","
          "\"ts\":%lu,\"count\":1,\"online\":%s,"
          "\"devices\":{\"" SMART_HOME_MQTT_DEVICE_ID "\":{\"name\":\""
          SMART_HOME_MQTT_DEVICE_NAME "\","
          "\"online\":%s",
          (unsigned long)smart_home_unix_ts, online, online)) {
    return 0U;
  }

  if (DHT11_is_ready() &&
      !state_append(payload, capacity, &offset,
                    ",\"temp\":%.1f,\"humi\":%.1f",
                    (double)DHT11_get_temp(),
                    (double)DHT11_get_humi())) {
    return 0U;
  }

  if (bh1750_is_ready() &&
      !state_append(payload, capacity, &offset,
                    ",\"light\":%.1f",
                    (double)bh1750_get_lux())) {
    return 0U;
  }

  if (PM25_is_ready() &&
      !state_append(payload, capacity, &offset,
                    ",\"pm25\":%u",
                    (unsigned int)PM25_get_adc())) {
    return 0U;
  }

  if (smoke_is_ready() &&
      !state_append(payload, capacity, &offset,
                    ",\"mq2\":%u",
                    (unsigned int)smoke_get_adc())) {
    return 0U;
  }

  if (!state_append(
          payload, capacity, &offset,
          ",\"fan\":%u,\"led\":%s,"
          "\"rgb1\":[%u,%u,%u],\"rgb2\":[%u,%u,%u]}}}",
          (unsigned int)fan_get_speed(),
          board_led_is_on() ? "true" : "false",
          (unsigned int)ws2812_get_base_r(),
          (unsigned int)ws2812_get_base_g(),
          (unsigned int)ws2812_get_base_b(),
          (unsigned int)ws2812_2_get_base_r(),
          (unsigned int)ws2812_2_get_base_g(),
          (unsigned int)ws2812_2_get_base_b())) {
    return 0U;
  }

  return offset;
}

static uint8_t raw_publish_start(const char *payload, uint16_t payload_len)
{
  char command[160];
  int command_len;

  if (payload == NULL || payload_len == 0U ||
      payload_len >= sizeof(raw_payload)) {
    return 0U;
  }

  command_len = snprintf(command, sizeof(command),
                         "AT+MQTTPUBRAW=0,\"%s\",%u,0,1\r\n",
                         SMART_HOME_MQTT_STATE_TOPIC,
                         (unsigned int)payload_len);
  if (command_len <= 0 || (uint16_t)command_len >= sizeof(command)) {
    return 0U;
  }

  memcpy(raw_payload, payload, payload_len);
  raw_payload[payload_len] = '\0';
  raw_payload_len = payload_len;

  if (my_uart_write(&huart2, (const uint8_t *)command,
                    (uint16_t)command_len) != (uint16_t)command_len) {
    raw_reset();
    return 0U;
  }

  raw_active = 1U;
  raw_wait_prompt = 1U;
  raw_prompt_tick = HAL_GetTick();
  at_cmd_busy = 1U;
  esp32_at_cmd_start_tick = HAL_GetTick();
  esp32_at_cmd_timeout_logged = 0U;
  return 1U;
}

static uint8_t is_leap_year(int year)
{
  return ((year % 4 == 0 && year % 100 != 0) ||
          (year % 400 == 0)) ? 1U : 0U;
}

static uint8_t parse_ntp_time(const char *value, uint32_t *timestamp)
{
  static const char *const month_names[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  static const uint16_t days_before_month[] = {
      0U, 31U, 59U, 90U, 120U, 151U,
      181U, 212U, 243U, 273U, 304U, 334U};
  char weekday[4] = {0};
  char month[4] = {0};
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int year = 0;
  int month_index = -1;
  uint32_t days = 0U;
  int64_t seconds;

  if (value == NULL || timestamp == NULL) return 0U;
  if (sscanf(value, "%3s %3s %d %d:%d:%d %d",
             weekday, month, &day, &hour, &minute, &second, &year) != 7) {
    return 0U;
  }

  for (int i = 0; i < 12; i++) {
    if (strncmp(month, month_names[i], 3) == 0) {
      month_index = i;
      break;
    }
  }

  if (month_index < 0 || year < 1970 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return 0U;
  }

  for (int y = 1970; y < year; y++) {
    days += is_leap_year(y) ? 366U : 365U;
  }

  days += days_before_month[month_index];
  if (month_index >= 2 && is_leap_year(year)) days++;
  days += (uint32_t)(day - 1);

  seconds = (int64_t)days * 86400LL +
            (int64_t)hour * 3600LL +
            (int64_t)minute * 60LL +
            second -
            (int64_t)SMART_HOME_MQTT_TIMEZONE * 3600LL;
  if (seconds < 0 || seconds > UINT32_MAX) return 0U;

  *timestamp = (uint32_t)seconds;
  return 1U;
}

static void handle_ntp_line(char *line)
{
  char *value;
  char *end;
  uint32_t timestamp;

  value = line + strlen("+CIPSNTPTIME:");
  while (*value == ' ') value++;

  end = strpbrk(value, "\r\n");
  if (end != NULL) *end = '\0';

  if (parse_ntp_time(value, &timestamp)) {
    smart_home_unix_ts = timestamp;
  }

  /* 别把正在等 '>' 的那条 AT+MQTTPUBRAW 的占用清掉：清了之后 run_send 会
   * 在 ESP-AT 的数据阶段里再发一条 MQTTPUBRAW，那条命令会被当成负载吃掉。 */
  if (!raw_wait_prompt) {
    at_cmd_busy = 0U;
    esp32_at_cmd_timeout_logged = 0U;
  }
  ESP32_XIAOZHI_DEBUG_PRINTF("[NTP] %s ts=%lu\r\n",
                             value, (unsigned long)smart_home_unix_ts);
}

static void process_status_lines(char *buffer, uint16_t *length)
{
  char *p = buffer;

  while (*p) {
    char *eol = strpbrk(p, "\r\n");
    uint16_t line_len;
    uint16_t total;
    int is_status = 0;

    if (eol == NULL) break;
    line_len = (uint16_t)(eol - p);
    total = (uint16_t)(line_len + 1U);
    if (eol[0] == '\r' && eol[1] == '\n') total = (uint16_t)(line_len + 2U);

    if (line_len == 2U &&
        (strncmp(p, "OK", 2) == 0 || strncmp(p, "ok", 2) == 0)) {
      is_status = 1;
      /* AT+MQTTPUBRAW 的应答是 OK 和 > 两条，可能被拆成两次收上来。
       * 只看到 OK 时命令还没结束：既不能丢负载，也不能放掉 at_cmd_busy，
       * 否则后面的 > 成了孤儿、ESP-AT 会一直停在数据阶段把后续 AT 指令
       * 当负载吃掉。这条命令的收尾交给 > 或 +MQTTPUB:OK/FAIL。 */
      if (!raw_wait_prompt) {
        at_cmd_busy = 0U;
        esp32_got_ok = 1U;
        esp32_at_cmd_timeout_logged = 0U;
        if (raw_active) raw_reset();
      }
    } else if ((line_len >= 5U &&
                (strncmp(p, "ERROR", 5) == 0 ||
                 strncmp(p, "error", 5) == 0)) ||
               (line_len >= 8U && strncmp(p, "ERR CODE", 8) == 0)) {
      /* MQTT 类命令失败时 ESP-AT 回的是 "ERR CODE:0x<8位>"（例如 0x6004
       * 已经连着、0x6051 处于断开状态），而不是普通的 "ERROR"。
       * 不认这一类的话 at_cmd_busy 要等满通用超时才放掉，
       * 后面那些 fail-fast 的判断就全都退化成"干等"。 */
      is_status = 1;
      at_cmd_busy = 0U;
      esp32_got_ok = 0U;
      esp32_at_cmd_timeout_logged = 0U;
      if (raw_active) raw_reset();
    } else if (line_len >= 11U && strncmp(p, "+MQTTPUB:OK", 11) == 0) {
      is_status = 1;
      /* 只认"当前这次发布"的回执：at_busy_guard 超时补发之后，ESP-AT 还会
       * 为那次发布补一条 +MQTTPUB:OK，如果它落在下一次发布的等 '>' 期间，
       * 无条件收尾会把新那次发布的提示符变成孤儿。 */
      if (raw_active && !raw_wait_prompt) {
        at_cmd_busy = 0U;
        esp32_at_cmd_timeout_logged = 0U;
        raw_reset();
        status_led_note_publish_ok(); /* 状态灯单闪 = 这一条真的发出去了 */
      }
    } else if (line_len >= 13U && strncmp(p, "+MQTTPUB:FAIL", 13) == 0) {
      is_status = 1;
      if (raw_active && !raw_wait_prompt) {
        at_cmd_busy = 0U;
        esp32_at_cmd_timeout_logged = 0U;
        raw_reset();
        status_led_note_publish_fail(); /* 状态灯转"长闪"= 发布被拒 */
      }
    } else if (strncmp(p, "WIFI DISCONNECT", 15) == 0) {
      /* WiFi 掉了。**不要**在这里重走初始化流程 —— 那是几十秒的停机
       * （CWJAP + MQTT + SUB），而 ESP-AT 自己几秒就能把 WiFi 和 MQTT 接回来。
       * 先把链路标记成断开、停掉发布，交给 ESP-AT 自动重连；
       * 真的久久不回来，由 check_online 里的超时升级去处理。
       *
       * WiFi 回来时 ESP-AT 会补一条 +MQTTCONNECTED，订阅在那时重建。 */
      is_status = 1;
      mqtt_up = 0U;
      sub_ok = 0U;
      status_led_note_subscribe(0U);
      if (link_down_tick == 0U) link_down_tick = HAL_GetTick();
      ESP32_XIAOZHI_DEBUG_PRINTF("[WIFI] disconnected\r\n");
    } else if (strncmp(p, "+MQTTCONNECTED", 14) == 0) {
      /* MQTT（重新）连上了：发布可以继续，但订阅必须重建 */
      is_status = 1;
      mqtt_up = 1U;
      link_down_tick = 0U;
      sub_ok = 0U;
      status_led_note_subscribe(0U);
      last_sub_retry_tick = HAL_GetTick() - ESP32_XIAOZHI_SUB_RETRY_INTERVAL_MS;
      ESP32_XIAOZHI_DEBUG_PRINTF("[MQTT] connected, resubscribing\r\n");
    } else if (line_len >= 17U && strncmp(p, "+MQTTDISCONNECTED", 17) == 0) {
      /* broker 掉线。同上：先只是标记，不重走初始化。
       * ESP-AT 开着 auto-reconnect，正常几秒就回来一条 +MQTTCONNECTED，
       * 那时再补订阅即可；这时候把整套初始化重跑一遍反而会
       * "CWJAP + MQTTCONN 重复下发" → ERROR → INIT_FAIL → AT+RST，
       * 把一次几秒的抖动放大成一分多钟的离线。 */
      is_status = 1;
      mqtt_up = 0U;
      sub_ok = 0U;
      status_led_note_subscribe(0U);
      if (link_down_tick == 0U) link_down_tick = HAL_GetTick();
      ESP32_XIAOZHI_DEBUG_PRINTF("[MQTT] disconnected\r\n");
    } else if (strncmp(p, "WIFI", 4) == 0 ||
               (strncmp(p, "+MQTT", 5) == 0 &&
                strstr(p, "+MQTTSUBRECV:") == NULL) ||
               line_len == 0U) {
      is_status = 1;
    }

    if (is_status) {
      uint16_t consumed = (uint16_t)((p + total) - buffer);
      if (consumed > *length) consumed = *length;

      /* 双保险：任何一条状态行的消费都绝不能越过缓冲里第一条 +MQTTSUBRECV。
       * 状态行是从缓冲开头一路吃到自己这里的，一旦跨过订阅记录，小智刚发来的
       * 控制指令就被无声丢掉了 —— 这个 bug 现象是"能上报、控制没反应"，很难查。 */
      {
        char *guard = strstr(buffer, "+MQTTSUBRECV:");
        if (guard != NULL && (uint16_t)(guard - buffer) < consumed) {
          esp32_buf_consume(buffer, length, (uint16_t)(guard - buffer));
          break; /* 剩下的留给 handle_subrecv() */
        }
      }

      esp32_buf_consume(buffer, length, consumed);
      p = buffer;
      continue;
    }

    /* 订阅记录必须原样留在缓冲里交给 handle_subrecv()。
     *
     * 这里如果只是 p = eol + 1 往下走，下一轮要么撞上记录后面的空行、
     * 要么撞上别的状态行，而它们的 consumed 都是"从缓冲开头算起"，
     * 于是整条 +MQTTSUBRECV 被连带吃掉 —— 指令收不到，上报却一切正常。
     * （ESP32 后端里原来的 OneNET 版本有这一行 break，小智版本漏掉了。） */
    if (strncmp(p, "+MQTTSUBRECV:", 13) == 0) break;
    if (strncmp(p, "+CIPSNTPTIME:", 13) == 0) break;
    p = eol + 1;
  }
}

/* 把一个字段解析成纯十进制数：允许两端有引号和空格，字段内出现任何非数字
 * 字符就说明它不是长度字段。 */
static uint8_t parse_decimal_field(const char *begin, const char *end,
                                   uint32_t *out)
{
  uint32_t value = 0U;
  uint8_t digits = 0U;
  const char *p;

  if (begin == NULL || end == NULL || out == NULL || end < begin) return 0U;

  for (p = begin; p < end; p++) {
    if (*p == ' ' || *p == '\t' || *p == '"') continue;
    if (*p < '0' || *p > '9') return 0U;
    if (++digits > 5U) return 0U; /* 长度不会超过 5 位 */
    value = value * 10U + (uint32_t)(*p - '0');
  }

  if (digits == 0U) return 0U;
  *out = value;
  return 1U;
}

/* 字段（剥掉两端引号/空格后）是否等于 want */
static uint8_t field_equals(const char *begin, const char *end, const char *want)
{
  size_t want_len;

  if (begin == NULL || end == NULL || want == NULL || end < begin) return 0U;

  while (begin < end && (*begin == '"' || *begin == ' ')) begin++;
  while (end > begin && (end[-1] == '"' || end[-1] == ' ')) end--;

  want_len = strlen(want);
  if ((size_t)(end - begin) != want_len) return 0U;
  return (strncmp(begin, want, want_len) == 0) ? 1U : 0U;
}

/*
 * 兜底：吃掉从 buffer 起到本行末尾（含 \r\n）。
 * 只在"认不出 +MQTTSUBRECV 的排版/负载长度"时用 —— 宁可丢一条记录，
 * 也不能让接收缓冲被一条永远解析不出来的记录堵死。
 * 行还没收完就返回 0 等下一轮；缓冲已顶满说明再也等不到了，清掉重来。
 */
static uint8_t consume_record_line(char *buffer, uint16_t *length)
{
  char *eol = strpbrk(buffer, "\r\n");
  uint32_t total;

  if (eol == NULL) {
    if (*length >= (uint16_t)(ESP32_RX_BUF_SIZE - 1U)) {
      esp32_rx_clear();
      *length = 0U; /* 同步局部长度，否则调用方末尾会把它写回全局 */
    }
    return 0U;
  }

  total = (uint32_t)(eol - buffer) + 1U;
  if (eol[0] == '\r' && eol[1] == '\n') total++;
  esp32_buf_consume(buffer, length, (uint16_t)total);
  return 1U;
}

/*
 * 取走缓冲区里的一条 +MQTTSUBRECV 记录。
 *
 * 这个响应在不同 AT 固件上有两种排版，工程里两处旧代码各按一种写：
 *   A: +MQTTSUBRECV:<link>,<topic>,<data_len>,<data>\r\n   （ESP-AT 官方文档）
 *   B: +MQTTSUBRECV:<link>,<data_len>,<topic>,<data>\r\n   （json_parser.c 注释里那种）
 * 两种排版的共同点是负载都在第 3 个逗号之后，差别只在"长度和主题谁在前"。
 * 所以这里不写死顺序：把第 1、2 个字段都试成十进制数，是数字的那个就是长度，
 * 另一个当主题。这样两种固件都能正确取到边界，负载里出现逗号/引号/花括号也不会解析错。
 *
 * 认不出长度字段时退化成"吃掉到本行行尾"，宁可丢一条也不让接收缓冲被顶死。
 * 不论主题是什么都会消费掉整条记录，但只有 SMART_HOME_MQTT_CMD_TOPIC 才执行。
 */
static uint8_t handle_subrecv(char *buffer, uint16_t *length)
{
  char *comma1;
  char *comma2;
  char *comma3;
  char *payload;
  const char *topic_begin = NULL;
  const char *topic_end = NULL;
  uint32_t field1_len = 0U;
  uint32_t field2_len = 0U;
  uint32_t data_len = 0U;
  uint32_t header_len;
  uint32_t total;
  uint8_t field1_is_len;
  uint8_t field2_is_len;
  uint8_t have_len = 0U;

  if (buffer == NULL || length == NULL || *length == 0U) return 0U;

  comma1 = strchr(buffer, ',');
  comma2 = (comma1 != NULL) ? strchr(comma1 + 1, ',') : NULL;
  comma3 = (comma2 != NULL) ? strchr(comma2 + 1, ',') : NULL;

  /* 分隔符不够（例如某版固件省掉了长度字段）：退化成按行消费 */
  if (comma3 == NULL) return consume_record_line(buffer, length);

  payload = comma3 + 1;
  header_len = (uint32_t)(payload - buffer);

  field1_is_len = parse_decimal_field(comma1 + 1, comma2, &field1_len);
  field2_is_len = parse_decimal_field(comma2 + 1, comma3, &field2_len);

  if (field2_is_len && !field1_is_len) {
    /* 排版 A：字段1 是主题，字段2 是长度 */
    data_len = field2_len;
    topic_begin = comma1 + 1;
    topic_end = comma2;
    have_len = 1U;
  } else if (field1_is_len && !field2_is_len) {
    /* 排版 B：字段1 是长度，字段2 是主题 */
    data_len = field1_len;
    topic_begin = comma2 + 1;
    topic_end = comma3;
    have_len = 1U;
  } else if (field1_is_len && field2_is_len) {
    /* 两个都是数字：按排版 A 取字段2 当长度，主题就认不出来了（不执行） */
    data_len = field2_len;
    have_len = 1U;
  }

  /* 长度不合理（超出接收缓冲）时不按长度算，避免整条记录永远等不齐 */
  if (have_len && (data_len >= ESP32_RX_BUF_SIZE)) {
    have_len = 0U;
  }

  if (!have_len) {
    /* 长度字段认不出来：退化成按行消费，至少保证接收缓冲能往前走 */
    return consume_record_line(buffer, length);
  }

  total = header_len + data_len;
  if ((uint32_t)(*length) < total) {
    /* 整条记录还没收齐。缓冲满说明这条再也等不到了，清掉重来 */
    if (*length >= (uint16_t)(ESP32_RX_BUF_SIZE - 1U)) {
      esp32_rx_clear();
      *length = 0U; /* 同步局部长度，否则函数末尾会把它写回全局 */
    }
    return 0U;
  }

  /* 记录后面通常还跟着 \r\n，顺手吃掉，免得留下空行干扰状态行扫描 */
  if ((uint32_t)(*length) > total + 1U &&
      buffer[total] == '\r' && buffer[total + 1U] == '\n') {
    total += 2U;
  } else if ((uint32_t)(*length) > total &&
             (buffer[total] == '\r' || buffer[total] == '\n')) {
    total += 1U;
  }

  if (data_len > 0U && data_len < ESP32_XIAOZHI_CMD_BUF_SIZE &&
      payload[0] == '{' && /* 负载必须是 JSON 对象，避免误判的排版把垃圾喂进解析器 */
      topic_begin != NULL &&
      field_equals(topic_begin, topic_end, SMART_HOME_MQTT_CMD_TOPIC)) {
    home_cmd_result_t result;

    memcpy(cmd_json, payload, data_len);
    cmd_json[data_len] = '\0';

    result = home_cmd_apply(cmd_json);
    if (result != HOME_CMD_NONE) {
      /* 控制生效了：不等发布周期，立刻把新状态回给对端 */
      esp32_xiaozhi_publish_now();
    }
    ESP32_XIAOZHI_DEBUG_PRINTF("[CMD] result=%d\r\n", (int)result);
  } else if (topic_begin != NULL) {
    ESP32_XIAOZHI_DEBUG_PRINTF("[SUB] ignored non-cmd topic\r\n");
  } else {
    ESP32_XIAOZHI_DEBUG_PRINTF("[SUB] ignored unknown record\r\n");
  }

  esp32_buf_consume(buffer, length, (uint16_t)total);
  return 1U;
}

void esp32_xiaozhi_process_rx(void)
{
  char *buffer = esp32_rx_buf;
  uint16_t length = esp32_rx_len;

  if (length == 0U) return;

  if (raw_wait_prompt) {
    /* 只找"下一条订阅记录之前"的 '>'：整段缓冲里搜会被后面某条订阅消息
     * 负载里的 '>' 骗到，把记录头截断掉。这里不能用"第一个换行"当边界 ——
     * 提示符本身可能带 \r\n 收尾，那样会把它一起吃掉。 */
    char *next_record = strstr(buffer, "+MQTTSUBRECV:");
    char *prompt = strchr(buffer, '>');

    if (prompt != NULL && next_record != NULL && prompt > next_record) {
      prompt = NULL;
    }

    if (prompt != NULL) {
      uint16_t consumed = (uint16_t)(prompt - buffer + 1);

      esp32_buf_consume(buffer, &length, consumed);
      raw_wait_prompt = 0U;

      if (raw_payload_len == 0U) {
        raw_reset();
        at_cmd_busy = 0U;
      } else {
        /* my_uart_write 是全有或全无：要么进 TX 环，要么返回 0。
         * 只要没进去就说明 ESP-AT 还差 length 个字节，此时一旦撒手，
         * 后面的 AT 指令就会被当成 MQTT 负载吃掉 —— 先退避重试几次，
         * 还不行就恢复等待状态，交给 at_busy_guard() 的超时路径收尾。 */
        uint8_t retry = 0U;

        while (my_uart_write(&huart2, (const uint8_t *)raw_payload,
                             raw_payload_len) != raw_payload_len) {
          if (++retry > 20U) {
            raw_wait_prompt = 1U;
            raw_prompt_tick = HAL_GetTick();
            ESP32_XIAOZHI_DEBUG_PRINTF("[MQTT] raw payload not queued\r\n");
            break;
          }
          my_uart_service_tx();
          HAL_Delay(1U);
        }
      }
    }
  }

  process_status_lines(buffer, &length);

  /* NTP 行必须在订阅消息之前处理。
   * 两者都只等下一次 process_rx，但不管先处理谁，都不能把对方那条记录
   * 连根吃掉 —— 这里先确认 NTP 行确实排在订阅记录前面才动它。
   * consumed 也必须在 handle_ntp_line() 之前算好：那个函数会把行尾的
   * '\r' 改成 '\0'，之后再判断 "eol[1]=='\n'" 就永远不成立了。 */
  {
    char *ntp_start = strstr(buffer, "+CIPSNTPTIME:");
    char *sub_start = strstr(buffer, "+MQTTSUBRECV:");

    if (ntp_start != NULL && (sub_start == NULL || ntp_start < sub_start)) {
      char *ntp_end = strpbrk(ntp_start, "\r\n");
      if (ntp_end != NULL) {
        uint16_t consumed = (uint16_t)((ntp_end + 1) - buffer);
        if (ntp_end[0] == '\r' && ntp_end[1] == '\n') consumed++;
        handle_ntp_line(ntp_start);
        esp32_buf_consume(buffer, &length, consumed);
      }
    }
  }

  /* 订阅消息：一次可能收到多条，循环取到取不动为止 */
  {
    uint8_t guard = 0U;

    while (guard < 4U) {
      char *sub = strstr(buffer, "+MQTTSUBRECV:");

      if (sub == NULL || length == 0U) break;

      if (sub != buffer) {
        /* 记录前面正常只剩一个 \r\n；吃掉，保证下面按记录头开始解析。
         * 前面的 NTP 行已经在上一步处理掉了，不会在这里被连带丢弃。 */
        esp32_buf_consume(buffer, &length, (uint16_t)(sub - buffer));
        if (length == 0U) break;
      }

      if (!handle_subrecv(buffer, &length)) break;
      guard++;
    }
  }

  esp32_rx_len = length;
}

static uint8_t check_uart2_response(const char *expected)
{
  esp32_rx_drain();
  if (esp32_rx_len > 0U && strstr(esp32_rx_buf, expected) != NULL) {
    at_cmd_busy = 0U;
    esp32_at_cmd_timeout_logged = 0U;
    (void)esp32_rx_consume_expected(expected);
    return 1U;
  }
  return 0U;
}

void esp32_xiaozhi_init_nonblock(void)
{
  static uint32_t fail_start = 0U;

  if (esp32_initialized) return;

  if (init_ctx.state == ESP32_XIAOZHI_INIT_FAIL) {
    if (fail_start == 0U) {
      fail_start = HAL_GetTick();
      if (fail_cycles < 255U) fail_cycles++;
    }

    if (HAL_GetTick() - fail_start >= ESP32_XIAOZHI_FAIL_COOLDOWN_MS) {
      fail_start = 0U;
      init_ctx.retry_count = 0U;
      esp32_rx_clear();

      /* 分两级恢复。
       * 绝大多数失败只是 broker/WiFi 暂时不可用，ESP32 本身好好地在跑，
       * 这时 AT+RST 会把 WiFi 关联也一起丢掉，反而多花十几秒。
       * 所以先"软恢复"：只重走 CWMODE → CWJAP → MQTT → SUB。
       * 连续几轮都拉不起来，才怀疑 ESP32 自己卡了，那时候才做硬复位。 */
      if (fail_cycles >= 3U) {
        fail_cycles = 0U;
        init_ctx.state = ESP32_XIAOZHI_INIT_IDLE; /* → ATE → AT+RST 硬复位 */
        ESP32_XIAOZHI_DEBUG_PRINTF("[ESP32] repeated failures, hard reset\r\n");
      } else {
        init_ctx.state = ESP32_XIAOZHI_INIT_CWMODE;
        ESP32_XIAOZHI_DEBUG_PRINTF("[ESP32] soft reconnect\r\n");
      }
    }
    return;
  }

  switch (init_ctx.state) {
    case ESP32_XIAOZHI_INIT_IDLE:
      init_ctx.state = ESP32_XIAOZHI_INIT_ATE;
      init_ctx.retry_count = 0U;
      esp32_rx_clear();
      break;

    case ESP32_XIAOZHI_INIT_ATE:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      uart_printf(&huart2, "ATE0\r\n");
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 2000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_ATE_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_ATE_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ESP32_XIAOZHI_INIT_RST;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ESP32_XIAOZHI_INIT_FAIL;
        else init_ctx.state = ESP32_XIAOZHI_INIT_ATE;
      }
      break;

    case ESP32_XIAOZHI_INIT_RST:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      uart_printf(&huart2, "AT+RST\r\n");
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = ESP32_XIAOZHI_RST_WAIT_MS;
      init_ctx.state = ESP32_XIAOZHI_INIT_RST_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_RST_WAIT:
      if (HAL_GetTick() - init_ctx.start_time >= init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        esp32_rx_clear();
        init_ctx.retry_count = 0U;
        init_ctx.state = ESP32_XIAOZHI_INIT_CWMODE;
      }
      break;

    case ESP32_XIAOZHI_INIT_CWMODE:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      uart_printf(&huart2, "AT+CWMODE=1\r\n");
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 2000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_CWMODE_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_CWMODE_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ESP32_XIAOZHI_INIT_CWJAP;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ESP32_XIAOZHI_INIT_FAIL;
        else init_ctx.state = ESP32_XIAOZHI_INIT_CWMODE;
      }
      break;

    case ESP32_XIAOZHI_INIT_CWJAP:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n",
              SMART_HOME_WIFI_SSID, SMART_HOME_WIFI_PASSWORD);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 10000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_CWJAP_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_CWJAP_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ESP32_XIAOZHI_INIT_MQTTDISC;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ESP32_XIAOZHI_INIT_FAIL;
        else init_ctx.state = ESP32_XIAOZHI_INIT_CWJAP;
      }
      break;

    /*
     * 先清掉可能已经存在的 MQTT 连接。
     *
     * 为什么需要：AT+MQTTCONN 的 <reconnect>=1 让 ESP-AT 自己重连，broker 抖一下
     * 它会自己接回来。这时候如果我们再下发一次 AT+MQTTCONN，ESP-AT 会回
     * ERROR（AT_MQTT_ALREADY_CONNECTED 0x6004），重试 3 次就掉进 INIT_FAIL，
     * 接着 AT+RST 把 ESP32 整个复位 —— 一次几秒的抖动被放大成一分多钟离线。
     * 先断干净再连，这条路径才确定。
     *
     * 命令名是 AT+MQTTCLEAN（ESP-AT 没有 AT+MQTTDISCONNECT 这条命令；
     * 官方 MQTTCONN 文档里就是"先 MQTTCLEAN，再重新配置参数、再连接"）。
     * 冷启动时本来就没连接，这条可能回错误码，所以成功失败都往下走。
     */
    case ESP32_XIAOZHI_INIT_MQTTDISC:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      uart_printf(&huart2, "AT+MQTTCLEAN=0\r\n");
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 3000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_MQTTDISC_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_MQTTDISC_WAIT:
      /* 成功、失败、超时都继续：这一步只是"清干净重来" */
      if (esp32_got_ok || !at_cmd_busy ||
          HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        esp32_at_cmd_timeout_logged = 0U;
        mqtt_up = 0U;
        init_ctx.state = ESP32_XIAOZHI_INIT_MQTTUSERCFG;
      }
      break;

    case ESP32_XIAOZHI_INIT_MQTTUSERCFG:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf,
              "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
              SMART_HOME_MQTT_CLIENT_ID,
              SMART_HOME_MQTT_USERNAME,
              SMART_HOME_MQTT_PASSWORD);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 8000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_MQTTUSERCFG_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_MQTTUSERCFG_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ESP32_XIAOZHI_INIT_MQTTCONN;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ESP32_XIAOZHI_INIT_FAIL;
        else init_ctx.state = ESP32_XIAOZHI_INIT_MQTTUSERCFG;
      }
      break;

    case ESP32_XIAOZHI_INIT_MQTTCONN:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+MQTTCONN=0,\"%s\",%d,1\r\n",
              SMART_HOME_MQTT_SERVER, SMART_HOME_MQTT_PORT);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      /* ESP-AT 自己等 MQTTCONN 的默认超时是 15s（文档里 <timeout_ms> 默认 15000）。
       * 我们必须等得比它久，否则它还在连、我们已经判超时并重发，
       * ESP-AT 只能回 ERROR，白烧掉 3 次重试。 */
      init_ctx.timeout_ms = 16000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_MQTTCONN_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_MQTTCONN_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.retry_count = 0U;
        mqtt_up = 1U;
        link_down_tick = 0U;
        init_ctx.state = ESP32_XIAOZHI_INIT_SUB_CMD;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ESP32_XIAOZHI_INIT_FAIL;
        else init_ctx.state = ESP32_XIAOZHI_INIT_MQTTCONN;
      }
      break;

    /* 订阅 home/cmd —— 小智的下行控制指令靠这条订阅才能收到。
     * 失败不让整个初始化失败：状态上报是主链路，订阅是附加能力，
     * 真订阅不上时 +MQTTDISCONNECTED 触发的重连还会再走一遍这里。 */
    case ESP32_XIAOZHI_INIT_SUB_CMD:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n",
              SMART_HOME_MQTT_CMD_TOPIC);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      /* 比 ESP-AT 的 MQTT 命令默认超时（15s）短，但留足 SUBACK 往返。
       * 订阅被拒时 ESP-AT 会立刻回 ERROR，_WAIT 里是 fail-fast，不会白等。 */
      init_ctx.timeout_ms = 8000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_SUB_CMD_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_SUB_CMD_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.retry_count = 0U;
        sub_ok = 1U;
        status_led_note_subscribe(1U);
        init_ctx.state = ESP32_XIAOZHI_INIT_NTPCFG;
      } else if (!at_cmd_busy ||
                 HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        /* 走到这里有两种情况：ESP-AT 回了 ERROR（at_cmd_busy 已被那条 ERROR
         * 清掉），或者等超时。都算这一次失败。
         * 特意让 ERROR 立刻算失败、不干等满 5s —— 补订阅是定期重试的，
         * 一次失败卡 5s 会把状态上报也一起拖住。 */
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) {
          init_ctx.retry_count = 0U;
          init_ctx.state = ESP32_XIAOZHI_INIT_NTPCFG;
          /* 订阅没成功也继续走完初始化（状态上报是主链路），
           * 但要记下来：状态灯转三闪，check_online 会定期补订阅。 */
          sub_ok = 0U;
          last_sub_retry_tick = HAL_GetTick();
          status_led_note_subscribe(0U);
          ESP32_XIAOZHI_DEBUG_PRINTF("[MQTT] subscribe %s failed\r\n",
                                     SMART_HOME_MQTT_CMD_TOPIC);
        } else {
          init_ctx.state = ESP32_XIAOZHI_INIT_SUB_CMD;
        }
      }
      break;

    case ESP32_XIAOZHI_INIT_NTPCFG:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+CIPSNTPCFG=1,%d,\"%s\"\r\n",
              SMART_HOME_MQTT_TIMEZONE, SMART_HOME_MQTT_NTP_SERVER);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 3000U;
      init_ctx.state = ESP32_XIAOZHI_INIT_NTPCFG_WAIT;
      break;
    case ESP32_XIAOZHI_INIT_NTPCFG_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ESP32_XIAOZHI_INIT_DONE;
        esp32_initialized = 1U;
        fail_cycles = 0U; /* 这一轮初始化跑通了，硬复位的计数清掉 */
        last_ntp_query_tick = HAL_GetTick() - ESP32_XIAOZHI_NTP_QUERY_INTERVAL_MS;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) {
          init_ctx.state = ESP32_XIAOZHI_INIT_DONE;
          esp32_initialized = 1U;
          fail_cycles = 0U;
          last_ntp_query_tick = HAL_GetTick() - ESP32_XIAOZHI_NTP_QUERY_INTERVAL_MS;
        } else {
          init_ctx.state = ESP32_XIAOZHI_INIT_NTPCFG;
        }
      }
      break;

    case ESP32_XIAOZHI_INIT_DONE:
      esp32_initialized = 1U;
      fail_cycles = 0U;
      break;
    case ESP32_XIAOZHI_INIT_FAIL:
    default:
      break;
  }
}

void esp32_xiaozhi_init(void)
{
  esp32_initialized = 0U;
  at_cmd_busy = 0U;
  init_ctx.state = ESP32_XIAOZHI_INIT_IDLE;
  init_ctx.retry_count = 0U;
  sub_ok = 0U;
  last_sub_retry_tick = 0U;
  mqtt_up = 0U;
  link_down_tick = 0U;
  fail_cycles = 0U;
  status_led_note_subscribe(0U);
  raw_reset();
}

/* 链路是否可用：初始化跑完 且 MQTT 连着。状态灯用这个判断"双闪"。 */
uint8_t esp32_xiaozhi_link_ok(void)
{
  return (uint8_t)((esp32_initialized && mqtt_up) ? 1U : 0U);
}

void esp32_xiaozhi_run_send(void)
{
  static uint32_t last_publish_tick = 0U;
  static char payload[ESP32_XIAOZHI_STATE_BUF_SIZE] = {0};
  uint32_t now = HAL_GetTick();
  uint16_t payload_len;

  /* raw_active 也要挡：上一次发布的负载可能刚写完、还在等 +MQTTPUB:OK，
   * 这时再发一条 AT+MQTTPUBRAW 会插进 ESP-AT 的数据阶段。
   * mqtt_up 同理：MQTT 没连上时每 3s 白挨一条 ERROR，也没意义。 */
  if (!esp32_initialized || !mqtt_up || at_cmd_busy || raw_active) return;
  if (!publish_now && last_publish_tick != 0U &&
      now - last_publish_tick < SMART_HOME_MQTT_PUBLISH_INTERVAL_MS) {
    return;
  }

  payload_len = build_state_payload(payload, sizeof(payload));
  if (payload_len == 0U) return;

  if (raw_publish_start(payload, payload_len)) {
    last_publish_tick = now;
    /* AT 命令已经排出去就清掉请求；真发不出去（AT 忙 / 串口写失败）时
     * raw_publish_start 返回 0，请求保留到下一轮重试。 */
    publish_now = 0U;
  }
}

void esp32_xiaozhi_check_online(void)
{
  uint32_t now;

  if (!esp32_initialized) return;
  now = HAL_GetTick();

  if (now - last_mqtt_ping_tick >= ESP32_XIAOZHI_PING_INTERVAL_MS &&
      !at_cmd_busy) {
    last_mqtt_ping_tick = now;
    uart_printf(&huart2, "AT+MQTTPING=0\r\n");
    at_cmd_busy = 1U;
    esp32_at_cmd_start_tick = now;
    esp32_at_cmd_timeout_logged = 0U;
  }

  if (now - last_esp_check_tick >= ESP32_XIAOZHI_CHECK_INTERVAL_MS &&
      !at_cmd_busy) {
    last_esp_check_tick = now;
    uart_printf(&huart2, "AT+CWSTATE?\r\n");
    at_cmd_busy = 1U;
    esp32_at_cmd_start_tick = now;
    esp32_at_cmd_timeout_logged = 0U;
  }

  if (now - last_ntp_query_tick >= ESP32_XIAOZHI_NTP_QUERY_INTERVAL_MS &&
      !at_cmd_busy) {
    last_ntp_query_tick = now;
    uart_printf(&huart2, "AT+CIPSNTPTIME?\r\n");
    at_cmd_busy = 1U;
    esp32_at_cmd_start_tick = now;
    esp32_at_cmd_timeout_logged = 0U;
  }

  /*
   * 订阅补漏。
   *
   * 初始化时 AT+MQTTSUB 连续失败 3 次会带着"没订阅"继续往下走（状态上报是主链路，
   * 不能因为订阅失败整体失败），而 ESP-AT 自己的 MQTT 重连也不会恢复订阅。
   * 结果就是"能上报、控制没反应"—— 和小智完全无法控制设备的现象一模一样。
   *
   * 这里定期把状态机拉回 SUB_CMD 那一步重试，走的是同一套已经在用的流程；
   * SUB 成功后它会自己继续走 NTPCFG → DONE，把 esp32_initialized 重新置 1。
   */
  if (mqtt_up && !sub_ok &&
      now - last_sub_retry_tick >= ESP32_XIAOZHI_SUB_RETRY_INTERVAL_MS) {
    last_sub_retry_tick = now;
    esp32_initialized = 0U;
    init_ctx.retry_count = 0U;
    init_ctx.state = ESP32_XIAOZHI_INIT_SUB_CMD;
    ESP32_XIAOZHI_DEBUG_PRINTF("[MQTT] retry subscribe %s\r\n",
                               SMART_HOME_MQTT_CMD_TOPIC);
  }

  /*
   * 链路超时升级。
   *
   * 平时 WiFi/MQTT 抖一下，ESP-AT 的 auto-reconnect 几秒就接回来，我们只补一次订阅；
   * 要是九十秒还没回来，就说明自动重连救不回来，这时候才把状态机拉回去重走
   * CWMODE → CWJAP → MQTT → SUB（软恢复，不重启 ESP32）。
   */
  if (mqtt_up) {
    link_down_tick = 0U;
  } else if (link_down_tick == 0U) {
    link_down_tick = now;
  } else if (now - link_down_tick >= ESP32_XIAOZHI_LINK_RECOVER_MS) {
    link_down_tick = now;
    esp32_initialized = 0U;
    init_ctx.retry_count = 0U;
    init_ctx.state = ESP32_XIAOZHI_INIT_CWMODE;
    ESP32_XIAOZHI_DEBUG_PRINTF("[LINK] down too long, reconnecting\r\n");
  }
}
