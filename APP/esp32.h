#ifndef __ESP32_H__
#define __ESP32_H__
#include "headfile.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usart.h"
#include "json_parser.h"


/** @brief ESP32 初始化（WiFi连接 + MQTT配置 + 订阅主题）*/
void esp32_init(void);
/** @brief ESP32 数据发送任务（上报传感器数据到 OneNET）*/
void esp32_run_send(void);
/** @brief ESP32 数据接收处理任务（处理云端下发的控制指令）*/
void esp32_run_recv(void);
/** @brief 构建 OneNET 标准属性上报 AT 命令（支持整型/浮点型/布尔型/字符串型）*/
void build_onenet_cmd(char *outbuf, const char *topic, const char *msg_id,uint8_t param_count, ...);
/** @brief 发送 AT 指令并等待期望响应（带超时重试机制）*/
int8_t send_cmd_wait_resp_it(UART_HandleTypeDef *huart, char *cmd,char *expected_resp, uint32_t time_out_ms, uint8_t max_retries);
void MQTT_Handle(char *subrecv_start);
void TIME_Handle(char *timerecv_start);
extern volatile uint8_t esp32_rx_pending;

#endif

