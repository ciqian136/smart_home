#include "board_led.h"

#define BOARD_LED_GPIO GPIOE
#define BOARD_LED_PIN  GPIO_PIN_5

void board_led_set(uint8_t on)
{
    HAL_GPIO_WritePin(BOARD_LED_GPIO,
                      BOARD_LED_PIN,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void board_led_on(void)
{
    board_led_set(1U);
}

void board_led_off(void)
{
    board_led_set(0U);
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(BOARD_LED_GPIO, BOARD_LED_PIN);
}

uint8_t board_led_is_on(void)
{
    return (HAL_GPIO_ReadPin(BOARD_LED_GPIO, BOARD_LED_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}
