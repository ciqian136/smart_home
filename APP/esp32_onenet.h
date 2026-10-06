#ifndef __ESP32_ONENET_H__
#define __ESP32_ONENET_H__

#include <stdint.h>

void esp32_onenet_init(void);
void esp32_onenet_init_nonblock(void);
void esp32_onenet_run_send(void);
void esp32_onenet_process_rx(void);
void esp32_onenet_check_online(void);
void esp32_onenet_flush_reply(void);

void build_onenet_cmd(char *outbuf,
                      const char *topic,
                      const char *msg_id,
                      uint8_t param_count,
                      ...);
void MQTT_Handle(char *subrecv_start);

#endif
