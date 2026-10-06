#ifndef __ESP32_H__
#define __ESP32_H__
#include "headfile.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usart.h"
#include "json_parser.h"

/*
 * MQTT backend selection:
 *   1 = xiaozhi broker + home/state (current default)
 *   0 = legacy OneNET MQTT implementation
 */
#define ESP32_MQTT_BACKEND_XIAOZHI 1U


/** @brief ESP32 初始化（WiFi连接 + MQTT配置 + 订阅主题）— 阻塞式 */
void esp32_init(void);
/** @brief ESP32 非阻塞初始化（状态机驱动，每轮调用一次，无阻塞延时）*/
void esp32_init_nonblock(void);
/** @brief AT 指令超时检测（无条件调用，防止 busy 死锁）*/
void esp32_check_cmd_timeout(void);
/** @brief 发送遗留 OneNET set_reply（当前单向状态上报模式不会触发）*/
void esp32_flush_reply(void);
/** @brief ESP32 在线状态维护（MQTT PING + WiFi 检测 + 离线重连）*/
void esp32_check_online(void);
/** @brief ESP32 数据发送任务（发布 retained home/state 状态 JSON）*/
void esp32_run_send(void);
/** @brief ESP32 数据接收处理任务（AT 响应、home/cmd 下发指令、遗留 OneNET 下发解析）*/
void esp32_run_recv(void);
/** @brief 构建 OneNET 标准属性上报 AT 命令（支持整型/浮点型/布尔型/字符串型）*/
/** @brief 发送 AT 指令并等待期望响应（带超时重试机制）— 阻塞式 */
int8_t send_cmd_wait_resp_it(UART_HandleTypeDef *huart, char *cmd,char *expected_resp, uint32_t time_out_ms, uint8_t max_retries);

/* ── 全局标志 ─────────────────────────────────── */
extern volatile uint8_t esp32_rx_pending;     /* 串口收到新数据 */
extern volatile uint8_t at_cmd_busy;          /* AT 指令执行中 */
extern volatile uint8_t esp32_initialized;    /* 非阻塞初始化完成 */
extern volatile uint8_t need_send_reply;      /* 需要回复 set_reply */

#endif

