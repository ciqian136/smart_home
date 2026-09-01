#include "health_monitor.h"

#include "my_uart.h"
#include "usart.h"

#include "stm32f1xx_hal.h"

#define HEALTH_UART1_LOG_DEFAULT 0U

#define HEALTH_UART1_LOG(...)                                      \
    do {                                                           \
        if (HEALTH_UART1_LOG_DEFAULT) uart_printf(&huart1, __VA_ARGS__); \
    } while (0)

static volatile uint32_t task_beat_mask = 0U;
static volatile uint32_t expected_task_mask = 0U;
static uint32_t last_complete_tick = 0U;

void health_monitor_init(void)
{
    task_beat_mask = 0U;
    expected_task_mask = 0U;
    last_complete_tick = HAL_GetTick();
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) {
        HEALTH_UART1_LOG("[HEALTH] watchdog reset\r\n");
        __HAL_RCC_CLEAR_RESET_FLAGS();
    }

    /* Four-second independent watchdog. Keep this opt-in at compile time. */
#ifdef SMART_HOME_ENABLE_IWDG
    IWDG->KR = 0x5555U;
    IWDG->PR = 6U;
    IWDG->RLR = 625U;
    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0U) {
    }
    IWDG->KR = 0xAAAAU;
    IWDG->KR = 0xCCCCU;
#endif
}

void health_monitor_feed(void)
{
#ifdef SMART_HOME_ENABLE_IWDG
    IWDG->KR = 0xAAAAU;
#endif
}

void health_monitor_set_expected_mask(uint32_t expected_mask)
{
    expected_task_mask = expected_mask;
}

void health_monitor_service(void)
{
    uint32_t completed = task_beat_mask;

    if (expected_task_mask == 0U ||
        (completed & expected_task_mask) == expected_task_mask) {
        task_beat_mask = 0U;
        last_complete_tick = HAL_GetTick();
        health_monitor_feed();
    } else if (HAL_GetTick() - last_complete_tick < 3000UL) {
        /* Allow startup and long sensor transactions to settle. */
        health_monitor_feed();
    }
}

void health_monitor_task_beat(uint8_t task_id)
{
    if (task_id < 32U) task_beat_mask |= (1UL << task_id);
}

uint32_t health_monitor_get_task_mask(void)
{
    return task_beat_mask;
}
