#include "automation.h"

#include "device_state.h"
#include "face.h"
#include "voice.h"

#include "stm32f1xx_hal.h"

#define AUTO_FAN_PM25_SPEED  800U
#define AUTO_FAN_SMOKE_SPEED 1000U
#define FACE_TEST_DIRECT_CONTROL 1U
#define FACE_TEST_LIGHT_R 125U
#define FACE_TEST_LIGHT_G 125U
#define FACE_TEST_LIGHT_B 125U
#define FACE_TEST_FAN_SPEED 500U

static uint8_t lights_restored = 0U;
static uint8_t lights_turned_off = 0U;
static uint32_t last_face_seen_tick = 0U;
static uint16_t last_auto_fan_speed = 0xFFFFU;
static uint8_t pm25_auto_active = 0U;
static uint8_t smoke_auto_active = 0U;
static uint8_t face_action_pending = 0U;
static uint8_t face_action_welcome_gen = 0U;

static uint16_t automation_fan_speed(float temperature, uint16_t previous_speed)
{
    int16_t low = device_state_get_temp_low_c10();
    int16_t mid = device_state_get_temp_mid_c10();
    int16_t high = device_state_get_temp_high_c10();
    int16_t temp_c10 = (int16_t)(temperature * 10.0f);

    /* Keep a 1C boundary hysteresis around the current temperature band. */
    if (previous_speed == 0U) {
        if (temp_c10 <= low + 10) return 0U;
    } else if (previous_speed <= 300U) {
        if (temp_c10 <= low - 10) return 0U;
        if (temp_c10 <= mid + 10) return 300U;
    } else if (previous_speed <= 500U) {
        if (temp_c10 <= mid - 10) return 300U;
        if (temp_c10 <= high + 10) return 500U;
    } else if (temp_c10 <= high - 10) {
        return 500U;
    }

    if (temp_c10 <= low) return 0U;
    if (temp_c10 <= mid) return 300U;
    if (temp_c10 <= high) return 500U;
    return 800U;
}

static uint16_t automation_max_speed(uint16_t a, uint16_t b)
{
    return a > b ? a : b;
}

static uint8_t automation_pm25_needs_fan(void)
{
    uint16_t limit = device_state_get_pm25_limit();

    if (!device_state_pm25_valid() || limit == 0U) {
        return pm25_auto_active;
    }

    if (device_state_get_pm25() >= (float)limit) {
        pm25_auto_active = 1U;
    } else if (device_state_get_pm25() <= (float)(limit > 10U ? limit - 10U : 0U)) {
        pm25_auto_active = 0U;
    }

    return pm25_auto_active;
}

static uint8_t automation_smoke_needs_fan(void)
{
    uint16_t limit = device_state_get_smoke_limit_ppm();
    uint16_t clear_level = limit > 50U ? (uint16_t)(limit - 50U) : 0U;

    if (!device_state_smoke_valid() || limit == 0U) {
        return smoke_auto_active;
    }

    if (device_state_smoke_alarm()) {
        smoke_auto_active = 1U;
    } else if (device_state_get_smoke() <= (float)clear_level) {
        smoke_auto_active = 0U;
    }

    return smoke_auto_active;
}

static void automation_turn_off_lights(void)
{
    for (uint8_t i = 1U; i <= device_state_get_strip_count(); i++) {
        device_state_set_strip_rgb(i, 0U, 0U, 0U, DEVICE_SOURCE_AUTOMATION);
    }
}

#if FACE_TEST_DIRECT_CONTROL
static void automation_face_test_control(void)
{
    uint16_t current_speed = device_state_get_fan_speed();

    if (!device_state_strip_is_open(DEVICE_STRIP_INDOOR_ID)) {
        device_state_set_strip_rgb(DEVICE_STRIP_INDOOR_ID,
                                   FACE_TEST_LIGHT_R,
                                   FACE_TEST_LIGHT_G,
                                   FACE_TEST_LIGHT_B,
                                   DEVICE_SOURCE_AUTOMATION);
        lights_restored = device_state_strip_is_open(DEVICE_STRIP_INDOOR_ID);
        lights_turned_off = 0U;
    }

    if (current_speed != FACE_TEST_FAN_SPEED ||
        device_state_get_fan_mode() != DEVICE_FAN_AUTO) {
        device_state_set_fan(FACE_TEST_FAN_SPEED, DEVICE_FAN_AUTO,
                             DEVICE_SOURCE_AUTOMATION);
    }
    last_auto_fan_speed = FACE_TEST_FAN_SPEED;
}
#endif

void automation_init(void)
{
    lights_restored = 0U;
    lights_turned_off = 0U;
    last_face_seen_tick = 0U;
    last_auto_fan_speed = 0xFFFFU;
    pm25_auto_active = 0U;
    smoke_auto_active = 0U;
    face_action_pending = 0U;
    face_action_welcome_gen = 0U;
}

void automation_service(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t recognized = face_is_zeng_detected();
    uint8_t owner_event = face_take_owner_detected_event();
    uint8_t auto_enabled = device_state_get_auto_enabled();
    uint8_t override_active = device_state_manual_override_active();
    uint8_t notify_mask = 0U;

    if (recognized) {
        last_face_seen_tick = now;
        lights_turned_off = 0U;
    }

#if FACE_TEST_DIRECT_CONTROL
    /* 人脸确认后先等欢迎播报事件被 voice 处理（开始播报或按冷却丢弃），
       再执行开灯/开风扇。无论温度、湿度、光照如何，都强制打开室内灯和风扇。 */
    if (owner_event) {
        face_action_pending = 1U;
        face_action_welcome_gen = face_get_event_seq();
    }

    if (face_action_pending &&
        voice_face_welcome_event_gen() == face_action_welcome_gen &&
        !voice_face_welcome_pending() &&
        voice_face_welcome_sent()) {
        automation_face_test_control();
        face_action_pending = 0U;
    }
#endif

    /* 按需求：除人脸识别自动开启灯光/风扇外，不再执行任何其他自动化逻辑。
       风扇和灯光只允许 app/云端、LCD、语音手动控制。
       欢迎播报已包含“已为您打开风扇/室内灯”，这里不再额外播报自动通知。 */
    return;

    /* 以下常规自动化代码保留但已禁用，不再执行 */
    /* 人脸直接控制可能已把模式切回自动，重新读取最新状态 */
    auto_enabled = device_state_get_auto_enabled();
    override_active = device_state_manual_override_active();
    if (!auto_enabled || override_active) return;

    if (!device_state_any_strip_is_open()) {
        uint8_t should_restore_lights = 0U;

        if (device_state_light_valid() &&
            device_state_get_light() <= (float)device_state_get_light_on_lux()) {
            should_restore_lights = 1U;
        } else if (owner_event &&
                   (!device_state_light_valid() ||
                    device_state_get_light() <= (float)device_state_get_light_off_lux())) {
            should_restore_lights = 1U;
        }

        if (should_restore_lights) {
            device_state_restore_lights(DEVICE_SOURCE_AUTOMATION);
            lights_restored = device_state_any_strip_is_open();
            lights_turned_off = 0U;
            if (lights_restored) notify_mask |= VOICE_AUTO_EVENT_LIGHT_ON;
        }
    }

    if (device_state_light_valid() && device_state_any_strip_is_open() &&
        lights_restored &&
        device_state_get_light() >= (float)device_state_get_light_off_lux()) {
        automation_turn_off_lights();
        lights_turned_off = 1U;
        lights_restored = 0U;
        notify_mask |= VOICE_AUTO_EVENT_LIGHT_OFF;
    }

    if (!recognized && lights_restored && last_face_seen_tick != 0U &&
        now - last_face_seen_tick >= 10000UL && !lights_turned_off &&
        (!device_state_light_valid() ||
         device_state_get_light() >= (float)device_state_get_light_on_lux())) {
        automation_turn_off_lights();
        lights_turned_off = 1U;
        lights_restored = 0U;
        notify_mask |= VOICE_AUTO_EVENT_LIGHT_OFF;
    }

    if (device_state_get_fan_mode() == DEVICE_FAN_AUTO) {
        uint16_t current_speed = device_state_get_fan_speed();
        uint16_t target_speed = 0U;
        uint8_t fan_signal_valid = 0U;

        if (device_state_temperature_valid()) {
            target_speed = automation_fan_speed(device_state_get_temperature(),
                                                current_speed);
            fan_signal_valid = 1U;
        }
        uint8_t pm25_needs_fan = automation_pm25_needs_fan();
        uint8_t smoke_needs_fan = automation_smoke_needs_fan();

        if (device_state_pm25_valid() || pm25_needs_fan) {
            fan_signal_valid = 1U;
        }
        if (device_state_smoke_valid() || smoke_needs_fan) {
            fan_signal_valid = 1U;
        }
        if (pm25_needs_fan) {
            target_speed = automation_max_speed(target_speed, AUTO_FAN_PM25_SPEED);
        }
        if (smoke_needs_fan) {
            target_speed = automation_max_speed(target_speed, AUTO_FAN_SMOKE_SPEED);
        }

        if (fan_signal_valid &&
            (last_auto_fan_speed == 0xFFFFU || target_speed == 0U || current_speed == 0U ||
             (target_speed > last_auto_fan_speed &&
              target_speed - last_auto_fan_speed >= 100U) ||
             (last_auto_fan_speed > target_speed &&
              last_auto_fan_speed - target_speed >= 100U))) {
            if (current_speed != target_speed) {
                device_state_set_fan(target_speed, DEVICE_FAN_AUTO,
                                     DEVICE_SOURCE_AUTOMATION);
                if (current_speed == 0U && target_speed > 0U) {
                    notify_mask |= VOICE_AUTO_EVENT_FAN_ON;
                } else if (current_speed > 0U && target_speed == 0U) {
                    notify_mask |= VOICE_AUTO_EVENT_FAN_OFF;
                } else if (target_speed > 0U) {
                    notify_mask |= VOICE_AUTO_EVENT_FAN_SPEED;
                }
            }
            last_auto_fan_speed = target_speed;
        }
    }

    if (notify_mask != 0U) {
        voice_notify_automation(notify_mask);
    }
}
