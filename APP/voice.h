#ifndef __VOICE_H__
#define __VOICE_H__

#include "headfile.h"

#define VOICE_AUTO_EVENT_LIGHT_ON   0x01U
#define VOICE_AUTO_EVENT_LIGHT_OFF  0x02U
#define VOICE_AUTO_EVENT_FAN_ON     0x04U
#define VOICE_AUTO_EVENT_FAN_OFF    0x08U
#define VOICE_AUTO_EVENT_FAN_SPEED  0x10U

void voice_run_send(void);
void voice_parse(void);
void voice_notify_automation(uint8_t event_mask);

#endif
