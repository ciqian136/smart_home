#ifndef __JSON_PARSER_H__
#define __JSON_PARSER_H__

#include "headfile.h"
#include <stdarg.h>

/**
 * @brief MQTT 订阅消息类型枚举
 */
typedef enum {
  MSG_UNKNOWN = 0,   /* 未知/不支持的消息类型 */
  MSG_POST_REPLY,    /* 属性上报响应（post/reply）*/
  MSG_PROPERTY_SET,  /* 云端下发的属性设置（property/set）*/
  MSG_SET_REPLY      /* 属性设置响应（set_reply）*/
} MqttMsgType_t;

/**
 * @brief  从 OneNET 属性设置的 JSON 中批量解析多个参数
 * @param json        输入的 JSON 字符串
 * @param param_count 要解析的参数数量
 * @param found       输出数组，标记每个参数是否找到 (1=找到, 0=未找到)
 * @param ...         可变参数，格式为：(char *key, int type, void *output)
 *                    type = 'i' → int*  (整型)
 *                    type = 'f' → float* (浮点型)
 *                    type = 'b' → int*  (布尔型 true/false)
 *                    type = 's' → char* (字符串型，缓冲区至少64字节)
 * @return 成功解析的参数数量
 */
uint8_t parse_onenet_params(const char *json, uint8_t param_count,
                            uint8_t *found, ...);

/**
 * @brief  从 JSON 字符串中提取消息 ID (msg_id)
 * @param json   输入的 JSON 字符串
 * @param msg_id 输出缓冲区
 * @param size   缓冲区大小
 * @return 1=成功提取, 0=未找到
 */
uint8_t json_get_msg_id(const char *json, char *msg_id, uint8_t size);

/**
 * @brief  根据 MQTT 主题判断消息类型
 * @param topic MQTT 主题字符串
 * @return 对应的消息类型枚举值
 */
MqttMsgType_t get_msg_type(const char *topic);

/**
 * @brief  从 +MQTTSUBRECV 消息中提取主题 (topic)
 * @param subrecv 原始接收字符串
 * @param topic   输出缓冲区
 * @param size    缓冲区大小
 */
void extract_topic(const char *subrecv, char *topic, uint8_t size);

/**
 * @brief  从 +MQTTSUBRECV 消息中提取 JSON 负载
 * @param subrecv  原始接收字符串
 * @param json_buf 输出缓冲区
 * @param size     缓冲区大小
 */
void extract_json(const char *subrecv, char *json_buf, uint16_t size);

#endif


