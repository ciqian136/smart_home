#ifndef __WS2812_3_H__
#define __WS2812_3_H__

#include "headfile.h"

/* ── 灯带3参数 ──────────────────────────────── */
#define NUM_LEDS3       192             // 灯珠数量
#define BITS_PER_LED3   24
#define DATA_BITS3      (NUM_LEDS3 * BITS_PER_LED3)   // 4608
#define RESET_US3       80              // 复位时间 80us
#define RESET_BITS3     ((uint16_t)(RESET_US3 / 1.25f) + 1)  // 80/1.25+1=65
#define TOTAL_BITS3     (DATA_BITS3 + RESET_BITS3)     // ~4673

/* TIM4_CH3 共用 ARR=89，不需要重新定义 */

extern uint16_t dma_buffer3[];          // DMA 缓冲区

/* ── API ────────────────────────────────────── */
void ws2812_3_set_all(uint8_t red, uint8_t green, uint8_t blue);

uint8_t ws2812_3_is_open(void);
uint8_t ws2812_3_get_base_r(void);
uint8_t ws2812_3_get_base_g(void);
uint8_t ws2812_3_get_base_b(void);

#endif
