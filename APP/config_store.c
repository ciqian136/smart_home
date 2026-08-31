#include "config_store.h"

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_flash.h"
#include "stm32f1xx_hal_flash_ex.h"

#include <string.h>

#define CONFIG_MAGIC              0x53484F4DU
#define CONFIG_VERSION            1U
#define CONFIG_SLOT0_ADDRESS      0x0807E000UL
#define CONFIG_SLOT1_ADDRESS      0x0807F000UL
#define CONFIG_PAGE_SIZE          0x800UL

static uint8_t config_record_values_valid(const config_record_t *record);

static uint32_t config_crc32(const config_record_t *record)
{
    const uint8_t *data = (const uint8_t *)record;
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;

    for (i = 0; i < (uint16_t)(sizeof(config_record_t) - sizeof(uint32_t)); i++) {
        uint8_t bit;
        crc ^= data[i];
        for (bit = 0; bit < 8U; bit++) {
            crc = (crc & 1UL) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void config_defaults(config_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->magic = CONFIG_MAGIC;
    record->version = CONFIG_VERSION;
    record->size = (uint16_t)sizeof(*record);
    record->fan_mode = 2U;
    record->auto_enabled = 1U;
    record->temp_low_c10 = 260;
    record->temp_mid_c10 = 280;
    record->temp_high_c10 = 300;
    record->light_on_lux = 120U;
    record->light_off_lux = 200U;
    record->pm25_limit = 75U;
    record->smoke_limit_ppm = 300U;
    for (uint8_t i = 0U; i < CONFIG_STORE_STRIP_COUNT; i++) {
        record->strip_last_rgb[i][0] = 125U;
        record->strip_last_rgb[i][1] = 125U;
        record->strip_last_rgb[i][2] = 125U;
    }
}

static uint8_t config_record_valid(uint32_t address, config_record_t *record)
{
    const config_record_t *flash_record = (const config_record_t *)address;

    memcpy(record, flash_record, sizeof(*record));
    if (record->magic != CONFIG_MAGIC ||
        record->version != CONFIG_VERSION ||
        record->size != sizeof(*record)) {
        return 0U;
    }
    return (config_crc32(record) == record->crc32) &&
           config_record_values_valid(record);
}

static uint8_t config_sequence_is_newer(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b) > 0) ? 1U : 0U;
}

static uint8_t config_record_values_valid(const config_record_t *record)
{
    if (record->fan_speed > 1000U || record->fan_mode > 2U ||
        record->auto_enabled > 1U) {
        return 0U;
    }
    if (record->fan_mode == 0U &&
        (record->fan_speed != 0U || record->auto_enabled != 0U)) {
        return 0U;
    }
    if (record->fan_mode == 2U && record->auto_enabled == 0U) {
        return 0U;
    }
    if (record->temp_low_c10 < CONFIG_TEMP_MIN_C10 ||
        record->temp_high_c10 > CONFIG_TEMP_MAX_C10 ||
        record->temp_low_c10 >= record->temp_mid_c10 ||
        record->temp_mid_c10 >= record->temp_high_c10) {
        return 0U;
    }
    if (record->light_on_lux >= record->light_off_lux ||
        record->light_off_lux > CONFIG_LIGHT_MAX_LUX ||
        record->pm25_limit == 0U || record->pm25_limit > CONFIG_PM25_MAX ||
        record->smoke_limit_ppm == 0U ||
        record->smoke_limit_ppm > CONFIG_SMOKE_MAX_PPM) {
        return 0U;
    }
    return 1U;
}

void config_store_init(void)
{
}

uint8_t config_store_load(config_record_t *record)
{
    config_record_t first;
    config_record_t second;
    uint8_t first_valid;
    uint8_t second_valid;

    if (record == NULL) return 0U;

    first_valid = config_record_valid(CONFIG_SLOT0_ADDRESS, &first);
    second_valid = config_record_valid(CONFIG_SLOT1_ADDRESS, &second);

    if (!first_valid && !second_valid) {
        config_defaults(record);
        return 0U;
    }
    if (first_valid && (!second_valid ||
                        config_sequence_is_newer(first.sequence, second.sequence))) {
        *record = first;
    } else {
        *record = second;
    }
    return 1U;
}

static HAL_StatusTypeDef config_erase_page(uint32_t address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = address;
    erase.NbPages = 1U;
    return HAL_FLASHEx_Erase(&erase, &page_error);
}

static HAL_StatusTypeDef config_program_record(uint32_t address,
                                                const config_record_t *record)
{
    const uint32_t *words = (const uint32_t *)record;
    uint32_t offset;

    for (offset = 0; offset < sizeof(*record); offset += 4U) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              address + offset, words[offset / 4U]) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}

uint8_t config_store_save(const config_record_t *record)
{
    config_record_t next;
    config_record_t current;
    uint32_t address;
    uint8_t loaded;

    if (record == NULL) return 0U;
    next = *record;
    if (!config_record_values_valid(&next)) return 0U;
    loaded = config_store_load(&current);
    next.magic = CONFIG_MAGIC;
    next.version = CONFIG_VERSION;
    next.size = (uint16_t)sizeof(next);
    next.sequence = loaded ? current.sequence + 1UL : 1UL;
    next.crc32 = 0UL;
    next.crc32 = config_crc32(&next);

    if (!loaded) {
        address = CONFIG_SLOT0_ADDRESS;
    } else {
        address = (current.sequence & 1UL) ? CONFIG_SLOT1_ADDRESS
                                           : CONFIG_SLOT0_ADDRESS;
    }

    HAL_FLASH_Unlock();
    if (config_erase_page(address) != HAL_OK ||
        config_program_record(address, &next) != HAL_OK) {
        HAL_FLASH_Lock();
        return 0U;
    }
    HAL_FLASH_Lock();
    return 1U;
}
