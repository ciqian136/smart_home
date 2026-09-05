#ifndef __FACE_H__
#define __FACE_H__

#include <stdint.h>

void face_init(void);
void face_proc(void);

uint8_t face_is_zeng_detected(void);
uint8_t face_take_zeng_detected_event(void);
uint8_t face_get_online(void);
uint8_t face_get_score(void);

#endif
