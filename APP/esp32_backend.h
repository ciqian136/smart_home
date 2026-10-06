#ifndef __ESP32_BACKEND_H__
#define __ESP32_BACKEND_H__

#include <stdint.h>

#define ESP32_RX_BUF_SIZE 1536U

extern volatile uint8_t esp32_rx_pending;
extern volatile uint8_t at_cmd_busy;
extern volatile uint8_t esp32_initialized;
extern volatile uint8_t need_send_reply;

extern uint32_t esp32_at_cmd_start_tick;
extern uint8_t esp32_at_cmd_timeout_logged;
extern uint8_t esp32_got_ok;

extern char esp32_rx_buf[ESP32_RX_BUF_SIZE];
extern uint16_t esp32_rx_len;

void esp32_rx_update_pending(void);
void esp32_rx_drain(void);
void esp32_rx_clear(void);
void esp32_buf_consume(char *buf, uint16_t *len, uint16_t consumed);
uint8_t esp32_rx_consume_expected(const char *expected);

void esp32_xiaozhi_init(void);
void esp32_xiaozhi_init_nonblock(void);
void esp32_xiaozhi_run_send(void);
void esp32_xiaozhi_process_rx(void);
void esp32_xiaozhi_check_online(void);
void esp32_xiaozhi_reset_publish(void);
void esp32_xiaozhi_publish_now(void);
uint8_t esp32_xiaozhi_at_busy_guard(void);
uint8_t esp32_xiaozhi_link_ok(void);

void esp32_onenet_init(void);
void esp32_onenet_init_nonblock(void);
void esp32_onenet_run_send(void);
void esp32_onenet_process_rx(void);
void esp32_onenet_check_online(void);
void esp32_onenet_flush_reply(void);

#endif
