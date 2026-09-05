#ifndef __VOICE_H__
#define __VOICE_H__

#include "headfile.h"

void voice_run_send(void);
void voice_parse(void);
void voice_alert_smoke_over_limit(uint16_t adc);
void voice_alert_dust_over_limit(uint16_t adc);
void voice_play_face_zeng(void);

#endif
