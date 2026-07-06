#ifndef __WS28112_H__
#define __WS28112_H__

#include "headfile.h"
#include "tim.h"
#include "dma.h"

#define NUM_LEDS    48               // 灯珠数量
#define BITS_PER_LED 24
#define DATA_BITS   (NUM_LEDS * BITS_PER_LED)

#define ARR         89              // 周期 = 90 个计数，对应 1.25µs
#define CODE_0      29              // 0 码高电平 ~0.4µs
#define CODE_1      58              // 1 码高电平 ~0.8µs
#define RESET_CODE  0               // 复位低电平

#define RESET_US    50                // 复位低电平最小时间 (µs)
#define T_US        1.25f             // 每个 PWM 周期时间 (µs)
#define RESET_BITS  ((uint16_t)(RESET_US / T_US) + 1)   // 至少 40 个周期，+1 保证足够

#define TOTAL_BITS  (DATA_BITS + RESET_BITS)

extern uint16_t dma_buffer[];   // 全部数据 + 复位位

void ws2812_color_to_buffer(uint8_t *colors, uint16_t len);
void ws2812_send(uint8_t *colors, uint16_t len);
void ws2812_set_all(uint8_t red, uint8_t green, uint8_t blue);
void ws2812_set_all_brightness_level(uint8_t red, uint8_t green, uint8_t blue, uint8_t bright_level);
void ws2812_set_brightness(uint8_t bright_level);

uint8_t ws2812_is_open(void);
uint8_t ws2812_get_brightness(void);
uint8_t ws2812_get_base_r(void);
uint8_t ws2812_get_base_g(void);
uint8_t ws2812_get_base_b(void);

#endif
