#ifndef __WS2812_2_H__
#define __WS2812_2_H__

#include "headfile.h"

/* ── 灯带2参数 ──────────────────────────────── */
#define NUM_LEDS2       192             // 灯珠数量
#define BITS_PER_LED2   24
#define DATA_BITS2      (NUM_LEDS2 * BITS_PER_LED2)   // 4608
#define RESET_US2       80              // 复位时间 80µs（192 灯长链需充裕余量）
#define RESET_BITS2     ((uint16_t)(RESET_US2 / 1.25f) + 1)  // 80/1.25+1=65
#define TOTAL_BITS2     (DATA_BITS2 + RESET_BITS2)     // ~4673

/* TIM4_CH2 共用 ARR=89，不需要重新定义 */

extern uint16_t dma_buffer2[];          // DMA 缓冲区

/* ── API ────────────────────────────────────── */
void ws2812_2_set_all(uint8_t red, uint8_t green, uint8_t blue);

uint8_t ws2812_2_is_open(void);
uint8_t ws2812_2_get_base_r(void);
uint8_t ws2812_2_get_base_g(void);
uint8_t ws2812_2_get_base_b(void);

#endif
