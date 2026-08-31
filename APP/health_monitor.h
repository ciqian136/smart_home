#ifndef SMART_HOME_HEALTH_MONITOR_H
#define SMART_HOME_HEALTH_MONITOR_H

#include <stdint.h>

void health_monitor_init(void);
void health_monitor_feed(void);
void health_monitor_service(void);
void health_monitor_task_beat(uint8_t task_id);
void health_monitor_set_expected_mask(uint32_t expected_mask);
uint32_t health_monitor_get_task_mask(void);

#endif
