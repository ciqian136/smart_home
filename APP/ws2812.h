#ifndef __WS28112_H__
#define __WS28112_H__

#include "headfile.h"
#include "tim.h"
#include "dma.h"

/* ========== 灯带1 硬件参数（48 LED, TIM4_CH1, PD12）========== */
#define NUM_LEDS    48               // 灯珠数量
#define BITS_PER_LED 24
#define DATA_BITS   (NUM_LEDS * BITS_PER_LED)

#define ARR         89              // 周期 = 90 个计数，对应 1.25µs
#define CODE_0      29              // 0 码高电平 ~0.4µs
#define CODE_1      58              // 1 码高电平 ~0.8µs
#define RESET_CODE  0               // 复位低电平

#define RESET_US    300               // 复位低电平最小时间 (µs)，48 灯长链需 300µs
#define T_US        1.25f             // 每个 PWM 周期时间 (µs)
#define RESET_BITS  ((uint16_t)(RESET_US / T_US) + 1)   // 至少 40 个周期，+1 保证足够

#define TOTAL_BITS  (DATA_BITS + RESET_BITS)

extern uint16_t dma_buffer[];   // 全部数据 + 复位位

/* ========== 灯带1 底层 API（保留兼容）========== */
void ws2812_color_to_buffer(uint8_t *colors, uint16_t len);
void ws2812_send(uint8_t *colors, uint16_t len);
void ws2812_set_all(uint8_t red, uint8_t green, uint8_t blue);

uint8_t ws2812_is_open(void);
uint8_t ws2812_get_base_r(void);
uint8_t ws2812_get_base_g(void);
uint8_t ws2812_get_base_b(void);

/* ================================================================
 * 统一多灯带管理层
 * ================================================================ */

#define MAX_STRIPS  4

/** 灯带硬件发送函数指针类型 */
typedef void (*ws2812_set_all_func_t)(uint8_t r, uint8_t g, uint8_t b);

typedef struct {
    uint8_t  id;                  /* 灯带编号 1~MAX_STRIPS */
    uint16_t num_leds;            /* LED 颗数 */
    uint8_t  cur_r, cur_g, cur_b; /* 当前颜色状态（单一数据源）*/
    ws2812_set_all_func_t set_all; /* 硬件发送函数 */
} ws2812_strip_t;

/* ---- 统一 API ---- */

/**
  * @brief  注册一条灯带
  * @param  id         灯带编号 1~MAX_STRIPS
  * @param  num_leds   LED 颗数
  * @param  set_all    硬件发送函数（如 ws2812_set_all / ws2812_2_set_all）
  */
void ws2812_strip_init(uint8_t id, uint16_t num_leds, ws2812_set_all_func_t set_all);

/**
  * @brief  设置指定灯带全部 LED 为同一颜色
  * @note   若 (r,g,b) 与当前状态相同则跳过 DMA 传输（防抖）
  */
void ws2812_strip_set_all(uint8_t id, uint8_t r, uint8_t g, uint8_t b);

/* ---- 状态查询 ---- */
uint8_t  ws2812_strip_is_open(uint8_t id);
uint8_t  ws2812_strip_get_r(uint8_t id);
uint8_t  ws2812_strip_get_g(uint8_t id);
uint8_t  ws2812_strip_get_b(uint8_t id);
uint16_t ws2812_strip_get_led_count(uint8_t id);
uint8_t  ws2812_strip_get_count(void);   /* 已注册的灯带数量 */

#endif
