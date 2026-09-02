#ifndef __ESP32_H__
#define __ESP32_H__

#include <stdint.h>


/** @brief ESP32 初始化（WiFi连接 + MQTT配置 + 订阅主题）— 阻塞式 */
void esp32_init(void);
/** @brief ESP32 非阻塞初始化（状态机驱动，每轮调用一次，无阻塞延时）*/
void esp32_init_nonblock(void);
/** @brief AT 指令超时检测（无条件调用，防止 busy 死锁）*/
void esp32_check_cmd_timeout(void);
/** @brief 发送待回复的 set_reply（每轮主循环都调用，确保及时响应）*/
void esp32_flush_reply(void);
/** @brief ESP32 在线状态维护（MQTT PING + WiFi 检测 + 离线重连）*/
void esp32_check_online(void);
uint8_t esp32_get_online(void);
/** @brief ESP32 数据发送任务（上报传感器数据到 OneNET）*/
void esp32_run_send(void);
/** @brief ESP32 数据接收处理任务（处理云端下发的控制指令 + OK/ERROR 检测）*/
void esp32_run_recv(void);

/* ── 全局标志 ─────────────────────────────────── */
extern volatile uint8_t esp32_rx_pending;     /* 串口收到新数据 */
extern volatile uint8_t esp32_uart1_status_report_enabled;

#endif

