#include "json_parser.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief  在 JSON 字符串中查找指定 key 对应的 value 起始位置
 *         支持两种格式："key":{"value":...} 和 "key":value
 * @param json  JSON 字符串
 * @param key   要查找的键名
 * @return value 字符串的起始指针，未找到返回 NULL
 */
static const char *json_find_value(const char *json, const char *key) {
  char search[64];

  /* 标准 OneNET 格式："key":{"value": */
  sprintf(search, "\"%s\":{\"value\":", key);
  const char *p = strstr(json, search);
  if (p != NULL) {
    return p + strlen(search);
  }

  /* 备用简化格式（不带外层花括号）："key":value */
  sprintf(search, "\"%s\":", key);
  p = strstr(json, search);
  if (p == NULL)
    return NULL;
  p += strlen(search);

  /* 跳过值前面的空白字符 */
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;
  return p;
}

/**
 * @brief  解析整数值（从当前位置开始，支持负号）
 * @param str  指向整数字符串的起始位置
 * @param val  输出解析得到的整数值
 * @return 解析完成后字符串的指针位置
 */
static const char *parse_int_value(const char *str, int *val) {
  int sign = 1;
  /* 处理负号 */
  if (*str == '-') {
    sign = -1;
    str++;
  }

  /* 逐位解析数字 */
  int num = 0;
  while (*str >= '0' && *str <= '9') {
    num = num * 10 + (*str - '0');
    str++;
  }

  *val = num * sign;
  return str;
}

/**
 * @brief  解析浮点数值（从当前位置开始，支持小数部分）
 * @param str  指向浮点数字符串的起始位置
 * @param val  输出解析得到的浮点值
 * @return 解析完成后字符串的指针位置
 */
static const char *parse_float_value(const char *str, float *val) {
  int int_part = 0;
  /* 先解析整数部分 */
  str = parse_int_value(str, &int_part);

  float frac_part = 0.0f;
  float divisor = 1.0f;

  /* 解析小数部分 */
  if (*str == '.') {
    str++;
    while (*str >= '0' && *str <= '9') {
      frac_part = frac_part * 10.0f + (*str - '0');
      divisor *= 10.0f;
      str++;
    }
    frac_part /= divisor;
  }

  /* 整数部分与小数部分组合，注意负数情况 */
  *val = (float)int_part + ((int_part >= 0) ? frac_part : -frac_part);
  return str;
}

/**
 * @brief  解析布尔值（true / false）
 * @param str  指向布尔值字符串的起始位置
 * @param val  输出布尔值（1=true, 0=false）
 * @return 解析完成后字符串的指针位置
 */
static const char *parse_bool_value(const char *str, int *val) {
  if (strncmp(str, "true", 4) == 0) {
    *val = 1;
    return str + 4;
  } else if (strncmp(str, "false", 5) == 0) {
    *val = 0;
    return str + 5;
  }
  *val = 0;
  return str;
}

/**
 * @brief  解析带引号的字符串值（支持常见的转义字符）
 * @param str      指向字符串起始引号的位置
 * @param buf      输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 解析完成后字符串的指针位置
 */
static const char *parse_string_value(const char *str, char *buf,
                                      uint16_t buf_size) {
  /* 不是引号开头则返回空字符串 */
  if (*str != '"') {
    buf[0] = '\0';
    return str;
  }

  str++; /* 跳过开头引号 */
  uint16_t i = 0;

  /* 逐字符解析直到遇到结束引号或字符串结尾 */
  while (*str != '"' && *str != '\0' && i < buf_size - 1) {
    /* 处理转义序列 */
    if (*str == '\\' && *(str + 1) != '\0') {
      str++;
      switch (*str) {
      case '"':  buf[i++] = '"';  break;
      case '\\': buf[i++] = '\\'; break;
      case 'n':  buf[i++] = '\n'; break;
      case 'r':  buf[i++] = '\r'; break;
      case 't':  buf[i++] = '\t'; break;
      default:   buf[i++] = *str; break;
      }
    } else {
      buf[i++] = *str;
    }
    str++;
  }
  buf[i] = '\0';

  /* 跳过结尾引号 */
  if (*str == '"')
    str++;

  return str;
}

/**
 * @brief  从 OneNET 属性设置的 JSON 中批量解析多个参数
 *         支持整型('i')、浮点型('f')、布尔型('b')、字符串型('s')
 * @param json        输入的 JSON 字符串
 * @param param_count 要解析的参数数量
 * @param found       输出数组，标记每个参数是否找到（1=找到, 0=未找到）
 * @param ...         可变参数，格式为：(char *key, int type, void *output)
 *                    type='i' → int*, type='f' → float*,
 *                    type='b' → int*, type='s' → char*(≥64字节)
 * @return 成功解析的参数数量
 */
uint8_t parse_onenet_params(const char *json, uint8_t param_count,
                            uint8_t *found, ...) {
  uint8_t success_count = 0;
  va_list ap;

  /* found 是最后一个固定参数，作为 va_start 的基准 */
  va_start(ap, found);

  for (uint8_t i = 0; i < param_count; i++) {
    char *key = va_arg(ap, char *);      /* 参数名 */
    int type = va_arg(ap, int);          /* 参数类型标识 */
    void *output = va_arg(ap, void *);   /* 输出值指针 */

    /* 在 JSON 中查找该 key 对应的 value 起始位置 */
    const char *value_start = json_find_value(json, key);

    if (value_start == NULL) {
      /* 未找到该参数，标记为 0，不修改输出值 */
      found[i] = 0;
      continue;
    }

    /* 找到该参数，标记为 1 */
    found[i] = 1;
    uint8_t parsed = 0;

    /* 根据类型标识调用对应的解析函数 */
    switch (type) {
    case 'i': /* 整型 */
      parse_int_value(value_start, (int *)output);
      parsed = 1;
      break;

    case 'f': /* 浮点型 */
      parse_float_value(value_start, (float *)output);
      parsed = 1;
      break;

    case 'b': /* 布尔型（true/false）*/
      parse_bool_value(value_start, (int *)output);
      parsed = 1;
      break;

    case 's': /* 字符串型 */
      parse_string_value(value_start, (char *)output, 64);
      parsed = 1;
      break;
    }

    if (parsed)
      success_count++;
  }

  va_end(ap);
  return success_count;
}

/**
 * @brief  从 JSON 字符串中提取消息 ID（msg_id）
 *         查找格式："id":"xxxx"
 * @param json   输入的 JSON 字符串
 * @param msg_id 输出缓冲区（存放消息ID字符串）
 * @param size   缓冲区大小
 * @return 1=成功提取, 0=未找到
 */
uint8_t json_get_msg_id(const char *json, char *msg_id, uint8_t size) {
  char *id_start = strstr(json, "\"id\":\"");
  if (id_start == NULL)
    return 0;

  id_start += 6; /* 跳过 "id":" */
  char *id_end = strchr(id_start, '"');
  if (id_end == NULL)
    return 0;

  uint8_t len = id_end - id_start;
  if (len >= size)
    len = size - 1;

  strncpy(msg_id, id_start, len);
  msg_id[len] = '\0';
  return 1;
}

/**
 * @brief  根据 MQTT 主题判断消息类型
 * @param topic MQTT 主题字符串
 * @return 对应的消息类型枚举值
 */
MqttMsgType_t get_msg_type(const char *topic) {
  if (strstr(topic, "/thing/property/post/reply") != NULL)
    return MSG_POST_REPLY;   /* 属性上报响应 */
  if (strstr(topic, "/thing/property/set") != NULL)
    return MSG_PROPERTY_SET; /* 属性设置（云端下发）*/
  if (strstr(topic, "/thing/property/set_reply") != NULL)
    return MSG_SET_REPLY;    /* 属性设置响应 */
  return MSG_UNKNOWN;        /* 未知消息类型 */
}

/**
 * @brief  从 +MQTTSUBRECV 消息中提取主题（topic）
 *         格式：+MQTTSUBRECV:0,"topic",...
 * @param subrecv +MQTTSUBRECV 原始消息字符串
 * @param topic   输出缓冲区（存放提取的主题）
 * @param size    缓冲区大小
 */
void extract_topic(const char *subrecv, char *topic, uint8_t size) {
  /* 找到第一个双引号，即为 topic 起始 */
  char *p = strchr(subrecv, '"');
  if (p == NULL) {
    topic[0] = '\0';
    return;
  }
  p++; /* 跳过起始引号 */

  /* 找到 topic 的结束引号 */
  char *end = strchr(p, '"');
  if (end == NULL) {
    topic[0] = '\0';
    return;
  }

  uint8_t len = end - p;
  if (len >= size)
    len = size - 1;
  strncpy(topic, p, len);
  topic[len] = '\0';
}

/**
 * @brief  从 +MQTTSUBRECV 消息中提取 JSON 负载
 *         MQTTSUB 格式：+MQTTSUBRECV:0,topic_len,topic,json
 *         第3个逗号之后即为 JSON 内容
 * @param subrecv  +MQTTSUBRECV 原始消息字符串
 * @param json_buf 输出缓冲区（存放提取的 JSON 字符串）
 * @param size     缓冲区大小
 */
void extract_json(const char *subrecv, char *json_buf, uint16_t size) {
  /* 找到第3个逗号后的内容作为 JSON 起始位置 */
  int comma_count = 0;
  const char *json_start = NULL;

  for (const char *p = subrecv; *p != '\0'; p++) {
    if (*p == ',')
      comma_count++;
    if (comma_count == 3) {
      json_start = p + 1;
      break;
    }
  }

  if (json_start == NULL) {
    json_buf[0] = '\0';
    return;
  }

  /* 找到 JSON 结尾标记 "}\r\n" */
  const char *json_end = strstr(json_start, "}\r\n");
  if (json_end == NULL) {
    json_buf[0] = '\0';
    return;
  }

  uint16_t len = (json_end + 1) - json_start;
  if (len >= size)
    len = size - 1;
  strncpy(json_buf, json_start, len);
  json_buf[len] = '\0';
}






