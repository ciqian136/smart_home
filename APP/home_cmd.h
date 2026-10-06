#ifndef __HOME_CMD_H__
#define __HOME_CMD_H__

#include <stdint.h>

/*
 * home/cmd 下行控制指令的解析与执行。
 *
 * 小智（ESP32-S3）用语音识别 -> MCP 工具 -> MQTT 发布到 SMART_HOME_MQTT_CMD_TOPIC，
 * STM32 的 ESP32 通过 AT 固件订阅该主题，收到 +MQTTSUBRECV 后把负载交给本模块。
 *
 * 负载格式（字段全部可选，只应用出现的字段）：
 *
 *   {"id":"sh-7","ts":1789818624,"source":"xiaozhi","device":"smart_home_01",
 *    "params":{"led":true,"fan":500,"rgb1_r":255,"rgb1_g":200,"rgb1_b":100}}
 *
 * 也接受 OneNET 旧的 {"key":{"value":...}} 包装，以及 OneNET 旧字段名
 * （LED / RGB1_RAD / RGB1_GREEN / RGB1_BLUE / RGB2_*），方便和旧代码互操作。
 *
 * 查询用 {"op":"query"}，STM32 收到后立刻重发一次 home/state。
 */

typedef enum {
  HOME_CMD_NONE = 0, /* 没有任何可识别字段，不要因此重发状态 */
  HOME_CMD_APPLIED,  /* 至少应用了一个字段 */
  HOME_CMD_QUERY     /* op=query：请求立刻上报一次状态 */
} home_cmd_result_t;

/**
 * @brief  解析并执行一条 home/cmd 指令
 * @param  json  以 '\0' 结尾的 JSON 负载（可为 NULL）
 * @return 处理结果，见 home_cmd_result_t
 */
home_cmd_result_t home_cmd_apply(const char *json);

#endif
