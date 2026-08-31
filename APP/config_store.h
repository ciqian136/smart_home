#ifndef SMART_HOME_CONFIG_STORE_H
#define SMART_HOME_CONFIG_STORE_H

#include <stdint.h>

#define CONFIG_STORE_STRIP_COUNT 3U

/* Bounds reject CRC-valid but nonsensical records from accidental writes. */
#define CONFIG_TEMP_MIN_C10      (-400)
#define CONFIG_TEMP_MAX_C10      800
#define CONFIG_LIGHT_MAX_LUX     10000U
#define CONFIG_PM25_MAX          1000U
#define CONFIG_SMOKE_MAX_PPM     10000U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint8_t strip_rgb[CONFIG_STORE_STRIP_COUNT][3];
    uint8_t strip_last_rgb[CONFIG_STORE_STRIP_COUNT][3];
    uint16_t fan_speed;
    uint8_t fan_mode;
    uint8_t auto_enabled;
    int16_t temp_low_c10;
    int16_t temp_mid_c10;
    int16_t temp_high_c10;
    uint16_t light_on_lux;
    uint16_t light_off_lux;
    uint16_t pm25_limit;
    uint16_t smoke_limit_ppm;
    uint32_t crc32;
} config_record_t;

void config_store_init(void);
uint8_t config_store_load(config_record_t *record);
uint8_t config_store_save(const config_record_t *record);

#endif
