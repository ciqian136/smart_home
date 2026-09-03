#ifndef __VOICE_H__
#define __VOICE_H__

#include "headfile.h"

#define VOICE_FACE_EVENT_KNOWN   1U
#define VOICE_FACE_EVENT_UNKNOWN 2U

void voice_run_send(void);
void voice_parse(void);
void voice_alert_smoke_over_limit(float ppm);
void voice_alert_dust_over_limit(float ugm3);
void voice_face_link_event(uint8_t event, uint16_t face_id);

#endif
