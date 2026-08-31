#ifndef SMART_HOME_DEVICE_STATE_H
#define SMART_HOME_DEVICE_STATE_H

#include <stdint.h>

#define DEVICE_STATE_STRIP_COUNT 3U
#define DEVICE_STRIP_INDOOR_ID 1U
#define DEVICE_STRIP_ENTRY_RESERVED_ID 2U
#define DEVICE_STRIP_OUTDOOR_ID 3U

typedef enum {
    DEVICE_SOURCE_LCD = 0,
    DEVICE_SOURCE_VOICE,
    DEVICE_SOURCE_CLOUD,
    DEVICE_SOURCE_AUTOMATION,
    DEVICE_SOURCE_RESTORE
} device_control_source_t;

typedef enum {
    DEVICE_FAN_OFF = 0,
    DEVICE_FAN_MANUAL = 1,
    DEVICE_FAN_AUTO = 2
} device_fan_mode_t;

void device_state_init(void);
void device_state_service(void);

uint8_t device_state_set_strip_rgb(uint8_t strip_id, uint8_t r, uint8_t g,
                                   uint8_t b, device_control_source_t source);
uint8_t device_state_restore_lights(device_control_source_t source);
uint8_t device_state_set_fan(uint16_t speed, device_fan_mode_t mode,
                             device_control_source_t source);
uint8_t device_state_set_fan_mode(device_fan_mode_t mode,
                                  device_control_source_t source);
void device_state_set_auto_enabled(uint8_t enabled,
                                   device_control_source_t source);
void device_state_set_temperature_thresholds(int16_t low_c10, int16_t mid_c10,
                                             int16_t high_c10,
                                             device_control_source_t source);
void device_state_set_light_thresholds(uint16_t on_lux, uint16_t off_lux,
                                       device_control_source_t source);
void device_state_set_air_limits(uint16_t pm25_limit, uint16_t smoke_limit_ppm,
                                 device_control_source_t source);
void device_state_set_board_led(uint8_t on, device_control_source_t source);
uint8_t device_state_get_board_led(void);

uint8_t device_state_get_strip_rgb(uint8_t strip_id, uint8_t *r, uint8_t *g,
                                   uint8_t *b);
uint8_t device_state_strip_is_active(uint8_t strip_id);
uint8_t device_state_strip_is_open(uint8_t strip_id);
uint8_t device_state_any_strip_is_open(void);
uint8_t device_state_get_strip_count(void);
uint16_t device_state_get_fan_speed(void);
device_fan_mode_t device_state_get_fan_mode(void);
uint8_t device_state_get_auto_enabled(void);
uint8_t device_state_manual_override_active(void);

float device_state_get_temperature(void);
float device_state_get_humidity(void);
float device_state_get_pm25(void);
float device_state_get_light(void);
float device_state_get_smoke(void);
uint8_t device_state_temperature_valid(void);
uint8_t device_state_humidity_valid(void);
uint8_t device_state_pm25_valid(void);
uint8_t device_state_light_valid(void);
uint8_t device_state_smoke_valid(void);
uint8_t device_state_smoke_alarm(void);
uint8_t device_state_pm25_alarm(void);

int16_t device_state_get_temp_low_c10(void);
int16_t device_state_get_temp_mid_c10(void);
int16_t device_state_get_temp_high_c10(void);
uint16_t device_state_get_light_on_lux(void);
uint16_t device_state_get_light_off_lux(void);
uint16_t device_state_get_pm25_limit(void);
uint16_t device_state_get_smoke_limit_ppm(void);

#endif
