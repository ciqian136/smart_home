#ifndef __FACE_H__
#define __FACE_H__

#include <stdint.h>

#define FACE_UART1_STATUS_REPORT_DEFAULT 1U
#define FACE_UART1_STATUS_REPORT_INTERVAL_MS 1000U

void face_init(void);
void face_proc(void);
uint8_t face_is_zeng_detected(void);
/* Backward-compatible alias for existing application code. */
uint8_t face_is_owner_detected(void);
/* Return 1 once for the automation recognition edge. */
uint8_t face_take_owner_detected_event(void);
/* Return 1 once for the voice welcome recognition edge. */
uint8_t face_take_welcome_event(void);
uint8_t face_get_score(void);
uint8_t face_get_online(void);

extern volatile uint8_t face_uart1_status_report_enabled;

#endif
