#ifndef __ESP32_XIAOZHI_H__
#define __ESP32_XIAOZHI_H__

#include <stdint.h>

void esp32_xiaozhi_init(void);
void esp32_xiaozhi_init_nonblock(void);
void esp32_xiaozhi_run_send(void);
void esp32_xiaozhi_process_rx(void);
void esp32_xiaozhi_check_online(void);
void esp32_xiaozhi_reset_publish(void);

/* 请求下一次调度立刻上报一次 home/state（不等发布周期）。
 * 收到小智的 home/cmd 后调用，让控制结果尽快回到对端。 */
void esp32_xiaozhi_publish_now(void);

/* 供 esp32_check_cmd_timeout() 调用的"这条 AT 命令自己计时"钩子。
 * 返回 1 表示正在等 AT+MQTTPUBRAW 的 '>'，通用 AT 超时先别收尾；
 * 返回 0 表示可以按通用超时处理。详见 esp32_xiaozhi.c 里的实现说明。 */
uint8_t esp32_xiaozhi_at_busy_guard(void);

/* 链路是否可用：初始化跑完 **且** MQTT 连着。
 * 状态灯用它判断"双闪"，注意不能直接用 esp32_initialized ——
 * broker 掉线时初始化状态机仍然是 DONE。 */
uint8_t esp32_xiaozhi_link_ok(void);

#endif
