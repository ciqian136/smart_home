#ifndef __STATUS_LED_H__
#define __STATUS_LED_H__

#include "main.h"
#include <stdint.h>

/*
 * PB5 上的"上传状态灯"。
 *
 * 目的：不接电脑、不看串口，也能一眼看出 STM32 到底有没有在往 MQTT 上传数据，
 * 以及卡在哪一步。
 *
 * 为什么用 PB5：这块板子三颗 LED 里，一颗是电源指示，两颗接 GPIO —— PE5 和 PB5。
 * PE5 是用户可控的板载 LED（语音 LED:ON/OFF、云端下发 led 字段都会改它），
 * 拿它做状态指示会和用户操作打架；PB5 在 gpio.c 里已经配成推挽输出且没人用，
 * 正好当状态灯。
 *
 * 灯语（"闪"= 亮 120ms / 灭 120ms；闪烁之间的静默期 LED 是灭的）：
 *
 *   双闪，每 2s 一组     ESP32/MQTT 链路没通（`esp32_xiaozhi_link_ok() == 0`，
 *                        即初始化没跑完 **或** MQTT 掉线）
 *                        —— 查 WiFi、AT 接线供电、broker
 *   三闪，每 2s 一组     链路通了，但 home/cmd **没订阅上**
 *                        —— "能上报、控制没反应" 就是这个样子
 *   一次长闪(500ms)，每 2s   订阅正常，但发布一直不成功（超过 10s 没收到
 *                        +MQTTPUB:OK，或收到了 +MQTTPUB:FAIL）
 *   单闪，每 3s 一次     一切正常：每成功发布一次闪一下（发布周期就是 3s）
 *   全灭                 链路刚建立、还没发出第一条；或者 MCU 卡死了
 *
 * 注意"双闪"用的是 `esp32_xiaozhi_link_ok()`（初始化完成 **且** MQTT 连着），
 * 不能只看 `esp32_initialized` —— broker 掉线时初始化状态机仍然是 DONE。
 *
 * 极性：默认按"引脚拉高点亮"接（`.ioc` 里 PB5 的 PinState 是 RESET，
 * 也就是上电默认灭）。如果实测是反的（表现为常亮、偶尔熄灭），把
 * status_led.c 里的 STATUS_LED_ACTIVE_HIGH 改成 0 —— 只有那一处要改。
 */

/** @brief 初始化 PB5 状态灯（先把灯熄灭，避免上电瞬间常亮） */
void status_led_init(void);

/** @brief 周期调用（建议 20ms）推进灯语状态机 */
void status_led_poll(void);

/** @brief 一次发布成功（收到 +MQTTPUB:OK）：状态灯单闪一下 */
void status_led_note_publish_ok(void);

/** @brief 一次发布失败（收到 +MQTTPUB:FAIL）：状态灯转入"发布异常"提示 */
void status_led_note_publish_fail(void);

/** @brief 后端告知 home/cmd 订阅状态：0 = 没订阅上（收不到控制指令） */
void status_led_note_subscribe(uint8_t ok);

#endif
