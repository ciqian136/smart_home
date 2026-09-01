#include "device_state.h"

#include "BH1750.h"
#include "DHT11.h"
#include "config_store.h"
#include "fan.h"
#include "PM25.h"
#include "smoke.h"
#include "ws2812.h"

#include "stm32f1xx_hal.h"

#include <string.h>

#define MANUAL_OVERRIDE_MS 600000UL
#define DEFAULT_AUTO_LIGHT_R 125U
#define DEFAULT_AUTO_LIGHT_G 125U
#define DEFAULT_AUTO_LIGHT_B 125U

typedef struct {
    uint8_t rgb[DEVICE_STATE_STRIP_COUNT][3];
    uint8_t last_rgb[DEVICE_STATE_STRIP_COUNT][3];
    uint16_t fan_speed;
    device_fan_mode_t fan_mode;
    uint8_t auto_enabled;
    int16_t temp_low_c10;
    int16_t temp_mid_c10;
    int16_t temp_high_c10;
    uint16_t light_on_lux;
    uint16_t light_off_lux;
    uint16_t pm25_limit;
    uint16_t smoke_limit_ppm;
    uint8_t board_led_on;
    float temperature;
    float humidity;
    float pm25;
    float light;
    float smoke;
    uint8_t temperature_valid;
    uint8_t humidity_valid;
    uint8_t pm25_valid;
    uint8_t light_valid;
    uint8_t smoke_valid;
    uint8_t smoke_alarm;
    uint8_t save_pending;
    uint32_t save_request_tick;
    uint32_t manual_override_until;
} device_state_ctx_t;

static device_state_ctx_t state;

uint8_t device_state_strip_is_active(uint8_t strip_id)
{
    return (strip_id == DEVICE_STRIP_INDOOR_ID ||
            strip_id == DEVICE_STRIP_OUTDOOR_ID) ? 1U : 0U;
}

static uint8_t device_state_is_real_strip(uint8_t strip_id)
{
    return strip_id >= 1U && strip_id <= DEVICE_STATE_STRIP_COUNT &&
           strip_id <= ws2812_strip_get_count() &&
           device_state_strip_is_active(strip_id);
}

static void device_state_request_save(void)
{
    state.save_pending = 1U;
    state.save_request_tick = HAL_GetTick();
}

static void device_state_mark_manual(void)
{
    state.manual_override_until = HAL_GetTick() + MANUAL_OVERRIDE_MS;
}

static void device_state_make_config(config_record_t *record)
{
    memset(record, 0, sizeof(*record));
    memcpy(record->strip_rgb, state.rgb, sizeof(state.rgb));
    memcpy(record->strip_last_rgb, state.last_rgb, sizeof(state.last_rgb));
    record->fan_speed = state.fan_speed;
    record->fan_mode = (uint8_t)state.fan_mode;
    record->auto_enabled = state.auto_enabled;
    record->temp_low_c10 = (int16_t)state.temp_low_c10;
    record->temp_mid_c10 = (int16_t)state.temp_mid_c10;
    record->temp_high_c10 = (int16_t)state.temp_high_c10;
    record->light_on_lux = state.light_on_lux;
    record->light_off_lux = state.light_off_lux;
    record->pm25_limit = state.pm25_limit;
    record->smoke_limit_ppm = state.smoke_limit_ppm;
}

static void device_state_load_config(const config_record_t *record)
{
    memcpy(state.rgb, record->strip_rgb, sizeof(state.rgb));
    memcpy(state.last_rgb, record->strip_last_rgb, sizeof(state.last_rgb));
    for (uint8_t i = 0U; i < DEVICE_STATE_STRIP_COUNT; i++) {
        if (state.last_rgb[i][0] == 0U && state.last_rgb[i][1] == 0U &&
            state.last_rgb[i][2] == 0U) {
            state.last_rgb[i][0] = DEFAULT_AUTO_LIGHT_R;
            state.last_rgb[i][1] = DEFAULT_AUTO_LIGHT_G;
            state.last_rgb[i][2] = DEFAULT_AUTO_LIGHT_B;
        }
    }
    state.fan_speed = record->fan_speed > 1000U ? 1000U : record->fan_speed;
    state.fan_mode = record->fan_mode <= DEVICE_FAN_AUTO
                         ? (device_fan_mode_t)record->fan_mode
                         : DEVICE_FAN_AUTO;
    state.auto_enabled = record->auto_enabled ? 1U : 0U;
    if (state.fan_mode == DEVICE_FAN_AUTO) {
        state.auto_enabled = 1U;
    } else if (state.fan_mode == DEVICE_FAN_MANUAL && state.auto_enabled) {
        /* Manual control is a temporary override and is not restored forever. */
        state.fan_mode = DEVICE_FAN_AUTO;
    } else if (state.fan_mode == DEVICE_FAN_OFF) {
        state.auto_enabled = 0U;
        state.fan_speed = 0U;
    }
    /* config_store_load() already rejects invalid values and supplies defaults. */
    state.temp_low_c10 = record->temp_low_c10;
    state.temp_mid_c10 = record->temp_mid_c10;
    state.temp_high_c10 = record->temp_high_c10;
    state.light_on_lux = record->light_on_lux;
    state.light_off_lux = record->light_off_lux;
    state.pm25_limit = record->pm25_limit;
    state.smoke_limit_ppm = record->smoke_limit_ppm;
    state.board_led_on = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_5) == GPIO_PIN_RESET) ? 1U : 0U;
}

void device_state_init(void)
{
    config_record_t record;
#if SMOKE_TEST_MODE
    uint8_t air_limits_changed = 0U;
#endif

    memset(&state, 0, sizeof(state));
    config_store_init();
    config_store_load(&record);
    device_state_load_config(&record);

#if SMOKE_TEST_MODE
    if (state.pm25_limit > CONFIG_DEFAULT_PM25_LIMIT) {
        state.pm25_limit = CONFIG_DEFAULT_PM25_LIMIT;
        air_limits_changed = 1U;
    }
    if (state.smoke_limit_ppm > CONFIG_DEFAULT_SMOKE_LIMIT_PPM) {
        state.smoke_limit_ppm = CONFIG_DEFAULT_SMOKE_LIMIT_PPM;
        air_limits_changed = 1U;
    }
    if (air_limits_changed) {
        device_state_request_save();
    }
#endif

    /* The WS2812 registry is populated by schedule_init before this function.
     * Always transmit one explicit OFF frame on boot, even when saved RGB is
     * non-zero. Last non-zero RGB is kept only for automatic restore.
     */
    for (uint8_t i = 0; i < DEVICE_STATE_STRIP_COUNT; i++) {
        uint8_t id = (uint8_t)(i + 1U);
        state.rgb[i][0] = 0U;
        state.rgb[i][1] = 0U;
        state.rgb[i][2] = 0U;
        ws2812_strip_set_all_force(id, 0U, 0U, 0U);
    }
    fan_set(state.fan_mode == DEVICE_FAN_OFF ? 0U : state.fan_speed);
}

void device_state_service(void)
{
    config_record_t record;

    state.temperature = DHT11_get_temp();
    state.humidity = DHT11_get_humi();
    state.pm25 = PM25_get_ugm3();
    state.light = bh1750_get_lux();
    state.smoke = smoke_get_ppm();
    state.temperature_valid = DHT11_is_ready();
    state.humidity_valid = DHT11_is_ready();
    state.pm25_valid = PM25_is_ready();
    state.light_valid = bh1750_is_ready();
    state.smoke_valid = smoke_is_ready();
    state.smoke_alarm = smoke_is_alarmed();

    if (state.manual_override_until != 0U &&
        (int32_t)(HAL_GetTick() - state.manual_override_until) >= 0) {
        state.manual_override_until = 0U;
        if (state.auto_enabled && state.fan_mode == DEVICE_FAN_MANUAL) {
            state.fan_mode = DEVICE_FAN_AUTO;
            device_state_request_save();
        }
    }

    if (state.save_pending &&
        HAL_GetTick() - state.save_request_tick >= 2000UL) {
        device_state_make_config(&record);
        if (config_store_save(&record)) {
            state.save_pending = 0U;
        }
    }
}

uint8_t device_state_set_strip_rgb(uint8_t strip_id, uint8_t r, uint8_t g,
                                   uint8_t b, device_control_source_t source)
{
    uint8_t index;
    if (!device_state_is_real_strip(strip_id)) return 0U;
    index = (uint8_t)(strip_id - 1U);
    if (state.rgb[index][0] == r && state.rgb[index][1] == g && state.rgb[index][2] == b) {
        if (source != DEVICE_SOURCE_AUTOMATION && source != DEVICE_SOURCE_RESTORE) {
            device_state_mark_manual();
        }
        return 1U;
    }
    ws2812_strip_set_all(strip_id, r, g, b);
    state.rgb[index][0] = r;
    state.rgb[index][1] = g;
    state.rgb[index][2] = b;
    if (r != 0U || g != 0U || b != 0U) {
        state.last_rgb[index][0] = r;
        state.last_rgb[index][1] = g;
        state.last_rgb[index][2] = b;
    }
    if (source != DEVICE_SOURCE_AUTOMATION && source != DEVICE_SOURCE_RESTORE) {
        device_state_mark_manual();
    }
    device_state_request_save();
    return 1U;
}

uint8_t device_state_restore_lights(device_control_source_t source)
{
    uint8_t result = 1U;
    for (uint8_t i = 0; i < DEVICE_STATE_STRIP_COUNT; i++) {
        uint8_t id = (uint8_t)(i + 1U);
        if (!device_state_strip_is_active(id)) continue;
        result = device_state_set_strip_rgb(id, state.last_rgb[i][0],
                                            state.last_rgb[i][1],
                                            state.last_rgb[i][2], source) && result;
    }
    return result;
}

uint8_t device_state_set_fan(uint16_t speed, device_fan_mode_t mode,
                             device_control_source_t source)
{
    if (speed > 1000U) speed = 1000U;
    if (mode > DEVICE_FAN_AUTO) mode = DEVICE_FAN_MANUAL;
    if (mode == DEVICE_FAN_OFF) speed = 0U;
    fan_set(speed);
    state.fan_speed = speed;
    state.fan_mode = mode <= DEVICE_FAN_AUTO ? mode : DEVICE_FAN_MANUAL;
    if (state.fan_mode == DEVICE_FAN_AUTO) {
        state.auto_enabled = 1U;
        state.manual_override_until = 0U;
    } else if (state.fan_mode == DEVICE_FAN_OFF) {
        state.auto_enabled = 0U;
        state.manual_override_until = 0U;
    } else if (source != DEVICE_SOURCE_AUTOMATION &&
               source != DEVICE_SOURCE_RESTORE) {
        /* A manual speed change temporarily suspends automatic fan control. */
        device_state_mark_manual();
    }
    device_state_request_save();
    return 1U;
}

uint8_t device_state_set_fan_mode(device_fan_mode_t mode,
                                  device_control_source_t source)
{
    if (mode > DEVICE_FAN_AUTO) return 0U;
    state.fan_mode = mode;
    if (mode == DEVICE_FAN_AUTO) {
        state.auto_enabled = 1U;
        state.manual_override_until = 0U;
    }
    if (mode == DEVICE_FAN_MANUAL) {
        if (source != DEVICE_SOURCE_AUTOMATION &&
            source != DEVICE_SOURCE_RESTORE) {
            device_state_mark_manual();
        }
    }
    if (mode == DEVICE_FAN_OFF) {
        state.auto_enabled = 0U;
        state.manual_override_until = 0U;
        fan_set(0U);
        state.fan_speed = 0U;
    }
    device_state_request_save();
    return 1U;
}

void device_state_set_auto_enabled(uint8_t enabled, device_control_source_t source)
{
    state.auto_enabled = enabled ? 1U : 0U;
    if (state.auto_enabled) {
        state.fan_mode = DEVICE_FAN_AUTO;
        state.manual_override_until = 0U;
    } else {
        state.fan_mode = DEVICE_FAN_MANUAL;
        if (source != DEVICE_SOURCE_AUTOMATION) {
            device_state_mark_manual();
        }
    }
    device_state_request_save();
}

void device_state_set_temperature_thresholds(int16_t low_c10, int16_t mid_c10,
                                             int16_t high_c10,
                                             device_control_source_t source)
{
    if (low_c10 < CONFIG_TEMP_MIN_C10 || high_c10 > CONFIG_TEMP_MAX_C10 ||
        mid_c10 <= low_c10 || high_c10 <= mid_c10) return;
    state.temp_low_c10 = low_c10;
    state.temp_mid_c10 = mid_c10;
    state.temp_high_c10 = high_c10;
    if (source != DEVICE_SOURCE_AUTOMATION) device_state_mark_manual();
    device_state_request_save();
}

void device_state_set_light_thresholds(uint16_t on_lux, uint16_t off_lux,
                                       device_control_source_t source)
{
    if (off_lux <= on_lux || off_lux > CONFIG_LIGHT_MAX_LUX) return;
    state.light_on_lux = on_lux;
    state.light_off_lux = off_lux;
    if (source != DEVICE_SOURCE_AUTOMATION) device_state_mark_manual();
    device_state_request_save();
}

void device_state_set_air_limits(uint16_t pm25_limit, uint16_t smoke_limit_ppm,
                                 device_control_source_t source)
{
    if (pm25_limit == 0U || pm25_limit > CONFIG_PM25_MAX ||
        smoke_limit_ppm == 0U || smoke_limit_ppm > CONFIG_SMOKE_MAX_PPM) return;
    state.pm25_limit = pm25_limit;
    state.smoke_limit_ppm = smoke_limit_ppm;
    if (source != DEVICE_SOURCE_AUTOMATION) device_state_mark_manual();
    device_state_request_save();
}

void device_state_set_board_led(uint8_t on, device_control_source_t source)
{
    state.board_led_on = on ? 1U : 0U;
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5,
                      state.board_led_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    if (source != DEVICE_SOURCE_AUTOMATION) device_state_mark_manual();
}

uint8_t device_state_get_board_led(void)
{
    return state.board_led_on;
}

uint8_t device_state_get_strip_rgb(uint8_t strip_id, uint8_t *r, uint8_t *g,
                                   uint8_t *b)
{
    uint8_t i;
    if (!device_state_is_real_strip(strip_id) || r == NULL || g == NULL || b == NULL) return 0U;
    i = (uint8_t)(strip_id - 1U);
    *r = state.rgb[i][0]; *g = state.rgb[i][1]; *b = state.rgb[i][2];
    return 1U;
}

uint8_t device_state_strip_is_open(uint8_t strip_id)
{
    uint8_t r, g, b;
    if (!device_state_get_strip_rgb(strip_id, &r, &g, &b)) return 0U;
    return (r || g || b) ? 1U : 0U;
}

uint8_t device_state_any_strip_is_open(void)
{
    for (uint8_t i = 1U; i <= DEVICE_STATE_STRIP_COUNT; i++) {
        if (!device_state_strip_is_active(i)) continue;
        if (device_state_strip_is_open(i)) return 1U;
    }
    return 0U;
}

uint8_t device_state_get_strip_count(void) { return DEVICE_STATE_STRIP_COUNT; }
uint16_t device_state_get_fan_speed(void) { return state.fan_speed; }
device_fan_mode_t device_state_get_fan_mode(void) { return state.fan_mode; }
uint8_t device_state_get_auto_enabled(void) { return state.auto_enabled; }

uint8_t device_state_manual_override_active(void)
{
    return state.manual_override_until != 0U ? 1U : 0U;
}

float device_state_get_temperature(void) { return state.temperature; }
float device_state_get_humidity(void) { return state.humidity; }
float device_state_get_pm25(void) { return state.pm25; }
float device_state_get_light(void) { return state.light; }
float device_state_get_smoke(void) { return state.smoke; }
uint8_t device_state_temperature_valid(void) { return state.temperature_valid; }
uint8_t device_state_humidity_valid(void) { return state.humidity_valid; }
uint8_t device_state_pm25_valid(void) { return state.pm25_valid; }
uint8_t device_state_light_valid(void) { return state.light_valid; }
uint8_t device_state_smoke_valid(void) { return state.smoke_valid; }
uint8_t device_state_smoke_alarm(void)
{
    return state.smoke_valid &&
           (state.smoke_alarm || state.smoke >= (float)state.smoke_limit_ppm) ?
           1U : 0U;
}
uint8_t device_state_pm25_alarm(void)
{
    return state.pm25_valid && state.pm25 >= (float)state.pm25_limit ? 1U : 0U;
}
int16_t device_state_get_temp_low_c10(void) { return state.temp_low_c10; }
int16_t device_state_get_temp_mid_c10(void) { return state.temp_mid_c10; }
int16_t device_state_get_temp_high_c10(void) { return state.temp_high_c10; }
uint16_t device_state_get_light_on_lux(void) { return state.light_on_lux; }
uint16_t device_state_get_light_off_lux(void) { return state.light_off_lux; }
uint16_t device_state_get_pm25_limit(void) { return state.pm25_limit; }
uint16_t device_state_get_smoke_limit_ppm(void) { return state.smoke_limit_ppm; }
