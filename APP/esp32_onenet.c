#include "esp32_onenet.h"
#include "esp32_backend.h"
#include "esp32.h"

#include "BH1750.h"
#include "dht11.h"
#include "board_led.h"
#include "fan.h"
#include "json_parser.h"
#include "mqtt_config.h"
#include "my_uart.h"
#include "PM25.h"
#include "smoke.h"
#include "ws2812.h"
#include "ws2812_2.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ONENET_MQTT_SERVER "mqtts.heclouds.com"
#define ONENET_MQTT_PORT 1883
#define ONENET_PRODUCT_ID "zs8Fz7juvp"
#define ONENET_DEVICE_NAME "one_test"
#define ONENET_MQTT_TOKEN \
  "version=2018-10-31&res=products%2Fzs8Fz7juvp%2Fdevices%2Fone_test&et=1911035456&method=md5&sign=LaO7G69DItgrJh2ng4LNdw%3D%3D"

#define ONENET_TOPIC_POST_RELAY \
  "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post/reply"
#define ONENET_TOPIC_SET \
  "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set"
#define ONENET_TOPIC_POST \
  "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
#define ONENET_TOPIC_SET_RELAY \
  "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set_reply"

#define ONENET_FAIL_COOLDOWN_MS 30000U
#define ONENET_RST_WAIT_MS 3000U
#define ONENET_CHECK_INTERVAL_MS 10000U
#define ONENET_PING_INTERVAL_MS 30000U
#define ONENET_SENSOR_FLOAT_FMT "%.1f"

#define ONENET_DEBUG 0

#if ONENET_DEBUG
#define ONENET_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define ONENET_DEBUG_PRINTF(...) ((void)0)
#endif

static char pending_reply_msg_id[16] = {0};
static uint32_t last_esp_check_tick = 0U;
static uint32_t last_mqtt_ping_tick = 0U;

typedef enum {
  ONENET_INIT_IDLE,
  ONENET_INIT_ATE,
  ONENET_INIT_ATE_WAIT,
  ONENET_INIT_RST,
  ONENET_INIT_RST_WAIT,
  ONENET_INIT_CWMODE,
  ONENET_INIT_CWMODE_WAIT,
  ONENET_INIT_CWJAP,
  ONENET_INIT_CWJAP_WAIT,
  ONENET_INIT_MQTTUSERCFG,
  ONENET_INIT_MQTTUSERCFG_WAIT,
  ONENET_INIT_MQTTCONN,
  ONENET_INIT_MQTTCONN_WAIT,
  ONENET_INIT_SUB_POST_REPLY,
  ONENET_INIT_SUB_POST_REPLY_WAIT,
  ONENET_INIT_SUB_SET,
  ONENET_INIT_SUB_SET_WAIT,
  ONENET_INIT_DONE,
  ONENET_INIT_FAIL
} onenet_init_state_t;

typedef struct {
  onenet_init_state_t state;
  uint8_t retry_count;
  uint32_t start_time;
  uint32_t timeout_ms;
  char cmd_buf[256];
} onenet_init_ctx_t;

static onenet_init_ctx_t init_ctx = {ONENET_INIT_IDLE, 0U, 0U, 0U, {0}};

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

static void queue_set_reply(const char *msg_id)
{
  if (msg_id == NULL || msg_id[0] == '\0') return;
  strncpy(pending_reply_msg_id, msg_id, sizeof(pending_reply_msg_id) - 1U);
  pending_reply_msg_id[sizeof(pending_reply_msg_id) - 1U] = '\0';
  need_send_reply = 1U;
}

void esp32_onenet_flush_reply(void)
{
  char command[256];

  if (!need_send_reply || at_cmd_busy) return;

  snprintf(command, sizeof(command),
           "AT+MQTTPUB=0,\"%s\",\"{\\\"id\\\":\\\"%s\\\"\\,\\\"code\\\":200\\,\\\"msg\\\":\\\"success\\\"}\",0,0\r\n",
           ONENET_TOPIC_SET_RELAY, pending_reply_msg_id);
  uart_printf(&huart2, "%s", command);

  at_cmd_busy = 1U;
  esp32_at_cmd_start_tick = HAL_GetTick();
  esp32_at_cmd_timeout_logged = 0U;
  need_send_reply = 0U;
  memset(pending_reply_msg_id, 0, sizeof(pending_reply_msg_id));
}

void esp32_onenet_run_send(void)
{
  static uint8_t skip = 0U;
  static uint32_t test_int = 0U;
  static uint8_t send_case = 0U;
  static char command[512] = {0};

  if (!esp32_initialized || at_cmd_busy) return;
  if (++skip < 10U) return;
  skip = 0U;
  memset(command, 0, sizeof(command));

  switch (send_case) {
    case 0:
      test_int++;
      build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                       "test_int", 'i', test_int);
      break;
    case 1:
      if (smoke_is_ready()) {
        build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                         "MQ2", 'i', smoke_get_adc());
      }
      break;
    case 2:
      if (PM25_is_ready()) {
        build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                         "PM25", 'i', PM25_get_adc());
      }
      break;
    case 3:
      if (DHT11_is_ready()) {
        build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                         "humi", 'f', DHT11_get_humi());
      }
      break;
    case 4:
      if (DHT11_is_ready()) {
        build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                         "temp", 'f', DHT11_get_temp());
      }
      break;
    case 5:
      build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                       "light", 'f', (double)bh1750_get_lux());
      break;
    case 6:
      build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                       "fan", 'i', fan_get_speed());
      break;
    case 7:
      build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 3,
                       "RGB1_RAD", 'i', ws2812_get_base_r(),
                       "RGB1_GREEN", 'i', ws2812_get_base_g(),
                       "RGB1_BLUE", 'i', ws2812_get_base_b());
      break;
    case 8:
      build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 3,
                       "RGB2_RAD", 'i', ws2812_2_get_base_r(),
                       "RGB2_GREEN", 'i', ws2812_2_get_base_g(),
                       "RGB2_BLUE", 'i', ws2812_2_get_base_b());
      break;
    case 9:
      build_onenet_cmd(command, ONENET_TOPIC_POST, "123", 1,
                       "LED", 'b', board_led_is_on());
      break;
    default:
      break;
  }

  if (command[0] != '\0') {
    uart_printf(&huart2, "%s", command);
    at_cmd_busy = 1U;
    esp32_at_cmd_start_tick = HAL_GetTick();
    esp32_at_cmd_timeout_logged = 0U;
  }

  send_case++;
  if (send_case >= 10U) send_case = 0U;
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
      at_cmd_busy = 0U;
      esp32_got_ok = 1U;
      esp32_at_cmd_timeout_logged = 0U;
    } else if (line_len >= 5U &&
               (strncmp(p, "ERROR", 5) == 0 ||
                strncmp(p, "error", 5) == 0)) {
      is_status = 1;
      at_cmd_busy = 0U;
      esp32_got_ok = 0U;
      esp32_at_cmd_timeout_logged = 0U;
    } else if (strncmp(p, "WIFI", 4) == 0 ||
               (strncmp(p, "+MQTT", 5) == 0 &&
                strstr(p, "+MQTTSUBRECV:") == NULL) ||
               line_len == 0U) {
      is_status = 1;
    }

    if (is_status) {
      uint16_t consumed = (uint16_t)((p + total) - buffer);
      if (consumed > *length) consumed = *length;
      esp32_buf_consume(buffer, length, consumed);
      p = buffer;
      continue;
    }

    if (strncmp(p, "+MQTTSUBRECV:", 13) == 0) break;
    p = eol + 1;
  }
}

void esp32_onenet_process_rx(void)
{
  char *buffer = esp32_rx_buf;
  uint16_t length = esp32_rx_len;

  if (length == 0U) return;
  process_status_lines(buffer, &length);

  {
    char *sub_start = strstr(buffer, "+MQTTSUBRECV:");
    if (sub_start != NULL) {
      char *msg_end = strstr(sub_start, "}\r\n");
      if (msg_end != NULL) {
        at_cmd_busy = 0U;
        esp32_at_cmd_timeout_logged = 0U;
        MQTT_Handle(sub_start);
        esp32_buf_consume(buffer, &length,
                          (uint16_t)((msg_end + 3) - buffer));
      }
    }
  }

  esp32_rx_len = length;
}

void esp32_onenet_init_nonblock(void)
{
  static uint32_t fail_start = 0U;

  if (esp32_initialized) return;

  if (init_ctx.state == ONENET_INIT_FAIL) {
    if (fail_start == 0U) fail_start = HAL_GetTick();
    if (HAL_GetTick() - fail_start >= ONENET_FAIL_COOLDOWN_MS) {
      fail_start = 0U;
      init_ctx.state = ONENET_INIT_IDLE;
      init_ctx.retry_count = 0U;
      esp32_rx_clear();
    }
    return;
  }

  switch (init_ctx.state) {
    case ONENET_INIT_IDLE:
      init_ctx.state = ONENET_INIT_ATE;
      init_ctx.retry_count = 0U;
      esp32_rx_clear();
      break;
    case ONENET_INIT_ATE:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      uart_printf(&huart2, "ATE0\r\n");
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 2000U;
      init_ctx.state = ONENET_INIT_ATE_WAIT;
      break;
    case ONENET_INIT_ATE_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ONENET_INIT_RST;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ONENET_INIT_FAIL;
        else init_ctx.state = ONENET_INIT_ATE;
      }
      break;
    case ONENET_INIT_RST:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      uart_printf(&huart2, "AT+RST\r\n");
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = ONENET_RST_WAIT_MS;
      init_ctx.state = ONENET_INIT_RST_WAIT;
      break;
    case ONENET_INIT_RST_WAIT:
      if (HAL_GetTick() - init_ctx.start_time >= init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        esp32_rx_clear();
        init_ctx.retry_count = 0U;
        init_ctx.state = ONENET_INIT_CWMODE;
      }
      break;
    case ONENET_INIT_CWMODE:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      uart_printf(&huart2, "AT+CWMODE=1\r\n");
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 2000U;
      init_ctx.state = ONENET_INIT_CWMODE_WAIT;
      break;
    case ONENET_INIT_CWMODE_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ONENET_INIT_CWJAP;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ONENET_INIT_FAIL;
        else init_ctx.state = ONENET_INIT_CWMODE;
      }
      break;
    case ONENET_INIT_CWJAP:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n",
              SMART_HOME_WIFI_SSID, SMART_HOME_WIFI_PASSWORD);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 10000U;
      init_ctx.state = ONENET_INIT_CWJAP_WAIT;
      break;
    case ONENET_INIT_CWJAP_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ONENET_INIT_MQTTUSERCFG;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ONENET_INIT_FAIL;
        else init_ctx.state = ONENET_INIT_CWJAP;
      }
      break;
    case ONENET_INIT_MQTTUSERCFG:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf,
              "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
              ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_MQTT_TOKEN);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 8000U;
      init_ctx.state = ONENET_INIT_MQTTUSERCFG_WAIT;
      break;
    case ONENET_INIT_MQTTUSERCFG_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ONENET_INIT_MQTTCONN;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ONENET_INIT_FAIL;
        else init_ctx.state = ONENET_INIT_MQTTUSERCFG;
      }
      break;
    case ONENET_INIT_MQTTCONN:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+MQTTCONN=0,\"%s\",%d,1\r\n",
              ONENET_MQTT_SERVER, ONENET_MQTT_PORT);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 8000U;
      init_ctx.state = ONENET_INIT_MQTTCONN_WAIT;
      break;
    case ONENET_INIT_MQTTCONN_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ONENET_INIT_SUB_POST_REPLY;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ONENET_INIT_FAIL;
        else init_ctx.state = ONENET_INIT_MQTTCONN;
      }
      break;
    case ONENET_INIT_SUB_POST_REPLY:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n",
              ONENET_TOPIC_POST_RELAY);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 5000U;
      init_ctx.state = ONENET_INIT_SUB_POST_REPLY_WAIT;
      break;
    case ONENET_INIT_SUB_POST_REPLY_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ONENET_INIT_SUB_SET;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ONENET_INIT_FAIL;
        else init_ctx.state = ONENET_INIT_SUB_POST_REPLY;
      }
      break;
    case ONENET_INIT_SUB_SET:
      if (at_cmd_busy) break;
      at_cmd_busy = 1U;
      esp32_at_cmd_timeout_logged = 0U;
      esp32_got_ok = 0U;
      sprintf(init_ctx.cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n",
              ONENET_TOPIC_SET);
      uart_printf(&huart2, "%s", init_ctx.cmd_buf);
      init_ctx.start_time = HAL_GetTick();
      init_ctx.timeout_ms = 5000U;
      init_ctx.state = ONENET_INIT_SUB_SET_WAIT;
      break;
    case ONENET_INIT_SUB_SET_WAIT:
      if (esp32_got_ok || check_uart2_response("OK")) {
        init_ctx.state = ONENET_INIT_DONE;
        esp32_initialized = 1U;
      } else if (HAL_GetTick() - init_ctx.start_time > init_ctx.timeout_ms) {
        at_cmd_busy = 0U;
        if (++init_ctx.retry_count >= 3U) init_ctx.state = ONENET_INIT_FAIL;
        else init_ctx.state = ONENET_INIT_SUB_SET;
      }
      break;
    case ONENET_INIT_DONE:
      esp32_initialized = 1U;
      break;
    case ONENET_INIT_FAIL:
    default:
      break;
  }
}

void esp32_onenet_init(void)
{
  char command[512];
  int8_t result = 0;

  result += send_cmd_wait_resp_it(&huart2, "ATE0\r\n", "OK", 2000U, 3U);
  uart_printf(&huart2, "AT+RST\r\n");
  HAL_Delay(3000U);
  result += send_cmd_wait_resp_it(&huart2, "AT+CWMODE=1\r\n", "OK", 2000U, 3U);
  sprintf(command, "AT+CWJAP=\"%s\",\"%s\"\r\n",
          SMART_HOME_WIFI_SSID, SMART_HOME_WIFI_PASSWORD);
  result += send_cmd_wait_resp_it(&huart2, command, "OK", 10000U, 3U);
  sprintf(command, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
          ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_MQTT_TOKEN);
  result += send_cmd_wait_resp_it(&huart2, command, "OK", 8000U, 3U);
  sprintf(command, "AT+MQTTCONN=0,\"%s\",%d,1\r\n",
          ONENET_MQTT_SERVER, ONENET_MQTT_PORT);
  result += send_cmd_wait_resp_it(&huart2, command, "OK", 8000U, 3U);
  sprintf(command, "AT+MQTTSUB=0,\"%s\",0\r\n", ONENET_TOPIC_POST_RELAY);
  result += send_cmd_wait_resp_it(&huart2, command, "OK", 5000U, 3U);
  sprintf(command, "AT+MQTTSUB=0,\"%s\",0\r\n", ONENET_TOPIC_SET);
  result += send_cmd_wait_resp_it(&huart2, command, "OK", 5000U, 3U);
  if (result == 0) esp32_initialized = 1U;
}

void esp32_onenet_check_online(void)
{
  uint32_t now;

  if (!esp32_initialized) return;
  now = HAL_GetTick();

  if (now - last_mqtt_ping_tick >= ONENET_PING_INTERVAL_MS &&
      !at_cmd_busy) {
    last_mqtt_ping_tick = now;
    uart_printf(&huart2, "AT+MQTTPING=0\r\n");
    at_cmd_busy = 1U;
    esp32_at_cmd_start_tick = now;
    esp32_at_cmd_timeout_logged = 0U;
  }

  if (now - last_esp_check_tick >= ONENET_CHECK_INTERVAL_MS &&
      !at_cmd_busy) {
    last_esp_check_tick = now;
    uart_printf(&huart2, "AT+CWSTATE?\r\n");
    at_cmd_busy = 1U;
    esp32_at_cmd_start_tick = now;
    esp32_at_cmd_timeout_logged = 0U;
  }
}

void build_onenet_cmd(char *outbuf, const char *topic, const char *msg_id,
                      uint8_t param_count, ...)
{
  static char payload[512];
  int offset = 0;
  va_list ap;

  memset(payload, 0, sizeof(payload));
  va_start(ap, param_count);
  offset += sprintf(payload + offset,
                    "{\\\"id\\\":\\\"%s\\\"\\,\\\"version\\\":\\\"1.0\\\"\\,\\\"params\\\":{",
                    msg_id);

  for (uint8_t i = 0U; i < param_count; i++) {
    char *key = va_arg(ap, char *);
    int type = va_arg(ap, int);
    if (i > 0U) offset += sprintf(payload + offset, "\\,");

    if (type == 'i') {
      int value = va_arg(ap, int);
      offset += sprintf(payload + offset,
                        "\\\"%s\\\":{\\\"value\\\":%d}", key, value);
    } else if (type == 'f') {
      double value = va_arg(ap, double);
      offset += sprintf(payload + offset,
                        "\\\"%s\\\":{\\\"value\\\":" ONENET_SENSOR_FLOAT_FMT "}",
                        key, value);
    } else if (type == 'b') {
      int value = va_arg(ap, int);
      offset += sprintf(payload + offset,
                        "\\\"%s\\\":{\\\"value\\\":%s}",
                        key, value ? "true" : "false");
    } else if (type == 's') {
      char *value = va_arg(ap, char *);
      offset += sprintf(payload + offset,
                        "\\\"%s\\\":{\\\"value\\\":\\\"%s\\\"}",
                        key, value);
    }
  }

  va_end(ap);
  sprintf(payload + offset, "}}");
  sprintf(outbuf, "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n", topic, payload);
}

void MQTT_Handle(char *subrecv_start)
{
  static char topic[128] = {0};
  static char json_buf[512] = {0};
  MqttMsgType_t msg_type;

  if (subrecv_start == NULL || strstr(subrecv_start, "}\r\n") == NULL) {
    return;
  }

  extract_topic(subrecv_start, topic, sizeof(topic));
  extract_json(subrecv_start, json_buf, sizeof(json_buf));
  msg_type = get_msg_type(topic);

  switch (msg_type) {
    case MSG_POST_REPLY: {
      int code = 0;
      uint8_t found[1] = {0};
      parse_onenet_params(json_buf, 1U, found, "code", 'i', &code);
      break;
    }
    case MSG_PROPERTY_SET: {
      int led = 0;
      int fan_val = 0;
      int rgb1_r = 0, rgb1_g = 0, rgb1_b = 0;
      int rgb2_r = 0, rgb2_g = 0, rgb2_b = 0;
      uint8_t found[8] = {0};

      parse_onenet_params(json_buf, 8U, found,
                          "LED", 'b', &led,
                          "fan", 'i', &fan_val,
                          "RGB1_RAD", 'i', &rgb1_r,
                          "RGB1_GREEN", 'i', &rgb1_g,
                          "RGB1_BLUE", 'i', &rgb1_b,
                          "RGB2_RAD", 'i', &rgb2_r,
                          "RGB2_GREEN", 'i', &rgb2_g,
                          "RGB2_BLUE", 'i', &rgb2_b);

      if (found[0]) board_led_set((uint8_t)led);
      if (found[1]) fan_set(fan_val);

      if (found[2] || found[3] || found[4]) {
        uint8_t r = found[2] ? (uint8_t)rgb1_r : ws2812_get_base_r();
        uint8_t g = found[3] ? (uint8_t)rgb1_g : ws2812_get_base_g();
        uint8_t b = found[4] ? (uint8_t)rgb1_b : ws2812_get_base_b();
        ws2812_set_all(r, g, b);
      }

      if (found[5] || found[6] || found[7]) {
        uint8_t r = found[5] ? (uint8_t)rgb2_r : ws2812_2_get_base_r();
        uint8_t g = found[6] ? (uint8_t)rgb2_g : ws2812_2_get_base_g();
        uint8_t b = found[7] ? (uint8_t)rgb2_b : ws2812_2_get_base_b();
        ws2812_2_set_all(r, g, b);
      }

      {
        char msg_id[16] = {0};
        if (json_get_msg_id(json_buf, msg_id, sizeof(msg_id))) {
          queue_set_reply(msg_id);
        }
      }
      break;
    }
    case MSG_SET_REPLY:
    default:
      break;
  }
}
