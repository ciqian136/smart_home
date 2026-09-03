#ifndef __BOARD_LED_H__
#define __BOARD_LED_H__

#include "main.h"
#include <stdint.h>

void board_led_set(uint8_t on);
void board_led_on(void);
void board_led_off(void);
void board_led_toggle(void);
uint8_t board_led_is_on(void);

#endif
