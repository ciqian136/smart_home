#include "esp32.h"
#include "DHT11.h"
#include "fan.h"

#include "my_uart.h"
#include <string.h>

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

volatile uint8_t esp32_rx_pending = 0;

static char pending_reply_cmd[256] = {0};
static uint8_t pending_reply_valid = 0;
static uint8_t pending_reply_try_count = 0;
static uint32_t pending_reply_last_send = 0;

static void queue_set_reply(const char *msg_id)
{
  if (msg_id == NULL || msg_id[0] == '\0') {
    return;
  }

  snprintf(pending_reply_cmd, sizeof(pending_reply_cmd),
           "AT+MQTTPUB=0,\"%s\",\"{\\\"id\\\":\\\"%s\\\"\\,\\\"code\\\":200\\,\\\"msg\\\":\\\"success\\\"}\",0,0\r\n",
           TOPIC_SET_RELAY, msg_id);
  pending_reply_valid = 1;
  pending_reply_try_count = 0;
  pending_reply_last_send = 0;
}

static void send_pending_reply(void)
{
  if (!pending_reply_valid) {
    return;
  }

  uint32_t now = HAL_GetTick();
  if (now - pending_reply_last_send < ESP32_REPLY_RETRY_MS) {
    return;
  }

  uart_printf(&huart2, "%s", pending_reply_cmd);
  pending_reply_last_send = now;
  pending_reply_try_count++;

  if (pending_reply_try_count >= ESP32_REPLY_MAX_RETRY) {
    pending_reply_valid = 0;
  }
}

/**
  * @brief  ESP32 数据发送任务 - 向 OneNET 上报传感器数据
  *         通过 MQTT 协议将测试数据和烟雾传感器数据发布到云端
  */
void esp32_run_send(void) {

  send_pending_reply();

  static uint32_t test_int = 0;
  static char cmd_buf[512] = {0};

  test_int++;
	
  build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 3, "test_int", 'i', test_int,
                   "PM25", 'i', PM25_get_adc(), "light_level", 'f',
                   bh1750_get_lux(), "temp", 'f', DHT11_get_temp(), "humi",
                   'f', DHT11_get_humi());
  uart_printf(&huart2, cmd_buf);
  memset(cmd_buf, 0, sizeof(cmd_buf));

  build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 2, "temp", 'f',
                   DHT11_get_temp(), "humi", 'f', DHT11_get_humi());
  uart_printf(&huart2, cmd_buf);
  memset(cmd_buf, 0, sizeof(cmd_buf));

  if (smoke_is_ready()) {
    build_onenet_cmd(cmd_buf, TOPIC_POST, "123", 2, "smoke_adc", 'i',
                     smoke_get_adc(), "smoke_alarm", 'i', smoke_is_alarmed());
    uart_printf(&huart2, cmd_buf);
  }
}

/**
  * @brief  ESP32 数据接收处理任务 - 处理来自 OneNET 平台的下行消息
  *         解析 MQTT 订阅消息，支持属性上报响应和设备属性设置
  */
void esp32_run_recv(void) {
  if (uart2_rx_len == 0)
    return;

  char *subrecv_start = strstr(uart2_rx_buf, "+MQTTSUBRECV:");
  if (subrecv_start != NULL) {
    if (strstr(subrecv_start, "}\r\n") != NULL) {
      MQTT_Handle(subrecv_start);
      memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
      uart2_rx_len = 0;
    }
    esp32_rx_pending = 0;
    return;
  }

  if (strstr(uart2_rx_buf, "OK\r\n") != NULL || strstr(uart2_rx_buf, "ERROR\r\n") != NULL) {
    memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
    uart2_rx_len = 0;
  }

  esp32_rx_pending = 0;
}
/**
  * @brief  ESP32 初始化 - 连接 WiFi 并完成 MQTT 服务器配置
  *         依次执行：AT指令回显设置、复位、STA模式配置、
  *         WiFi连接、MQTT用户配置、MQTT连接、主题订阅
  */
void esp32_init(void) {
  static char cmd_buf[512];

  send_cmd_wait_resp_it(&huart2, "ATE1\r\n", "OK", 2000, 3);
  uart_printf(&huart2, "AT+RST\r\n");
  HAL_Delay(2000);
  send_cmd_wait_resp_it(&huart2, "AT+CWMODE=1\r\n", "OK", 2000, 3);
  sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
  send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 10000, 3);
  sprintf(cmd_buf, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
          DEVICE_NAME, PRODUCT_ID, MQTT_TOKEN);
  send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 8000, 3);
  sprintf(cmd_buf, "AT+MQTTCONN=0,\"%s\",%d,1\r\n", MQTT_SERVER, MQTT_PORT);
  send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 8000, 3);
  sprintf(cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n", TOPIC_POST_RELAY);
  send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 5000, 3);
  sprintf(cmd_buf, "AT+MQTTSUB=0,\"%s\",0\r\n", TOPIC_SET);
  send_cmd_wait_resp_it(&huart2, cmd_buf, "OK", 5000, 3);
  send_cmd_wait_resp_it(&huart2, "AT+CIPSNTPCFG=1,8,\"ntp1.aliyun.com\"\r\n", "OK", 2000, 3);
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
        uart_printf(&huart1, "%s", uart2_rx_buf);
        memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
        uart2_rx_len = 0;
        return 0;
      }
      HAL_Delay(10);
    }
    /* 超时未收到期望响应，打印错误信息并重试 */
      uart_printf(&huart1, "%s", uart2_rx_buf);
    
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
      uart_printf(&huart1, "\r\n[MSG] up success(code=%d)\r\n", code);
    } else {
      uart_printf(&huart1, "\r\n[MSG] up false (code=%d)\r\n", code);
    }
    break;
  }

  /* ========== 云端下发的属性设置（property/set）========== */
  case MSG_PROPERTY_SET: {
    int led_on = 0;        /* LED开关 */
    int brightness = 0;    /* LED亮度 */
    int fan =0;            /* 风扇控制*/

    /* 标记每个参数是否被下发 */
    uint8_t found[3] = {0};

    /* 批量解析云台下发的参数 */
    uint8_t n = parse_onenet_params(
        json_buf, 3, found, "LED", 'b', &led_on,
        "led_brightness", 'i', &brightness,"fan",'i',&fan);

    uart_printf(&huart1, "[PARSE] parse %d param\r\n", n);

    /* 逐一处理已下发的参数 */
    if (found[0]) { /* LED控制(测试用) */
      HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5,led_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
      uart_printf(&huart1, "[CTRL] LED %s\r\n", led_on ? "OFF" : "ON");
    }
    
    if (found[1]) { /* LED灯带亮度调节 */
      
      uart_printf(&huart1, "[CTRL] light_level=%d\r\n", brightness);
    }
    
    if(found[2]){
      fan_set(fan);
       uart_printf(&huart1, "[CTRL] fan_level=%d\r\n", fan);     
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
    uart_printf(&huart1, "[MSG]cmd setted\r\n");
    break;
  }

  /* ========== 未知消息类型 ========== */
  default: {
    uart_printf(&huart1, "[MSG] don't know\r\n");
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
    uart_printf(&huart1, "[NTP] Time: %s\r\n", ntp_time_str);

}


