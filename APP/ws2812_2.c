#include "ws2812_2.h"
#include <stdint.h>

extern DMA_HandleTypeDef hdma_tim4_ch2;       /* DMA1_Channel4, TIM4_CH2 compare */
extern TIM_HandleTypeDef htim4;               /* 共用 TIM4 */

uint16_t dma_buffer2[TOTAL_BITS2];

/* 灯带2 基础颜色（向后兼容）*/
static uint8_t ws2812_2_base_r = 0;
static uint8_t ws2812_2_base_g = 0;
static uint8_t ws2812_2_base_b = 0;

/* ── 内部函数 ─────────────────────────────────── */

static void ws2812_2_color_to_buffer(uint8_t *colors, uint16_t len)
{
    uint16_t idx = 0;
    for (uint16_t led = 0; led < len; led++) {
        uint8_t g = colors[led * 3 + 0];
        uint8_t r = colors[led * 3 + 1];
        uint8_t b = colors[led * 3 + 2];
        for (uint8_t byte = 0; byte < 3; byte++) {
            uint8_t data = (byte == 0) ? g : (byte == 1 ? r : b);
            for (int8_t bit = 7; bit >= 0; bit--) {
                dma_buffer2[idx++] = (data & (1 << bit)) ? 58 : 29;  /* CODE_1 / CODE_0 */
            }
        }
    }
    /* 复位位 */
    for (uint16_t i = 0; i < RESET_BITS2; i++) {
        dma_buffer2[idx++] = 0;
    }
}

/**
  * @brief 发送数据到 WS2812 灯带2，自动生成复位信号
  * @note  直接寄存器操作，只启停 CH2，不关闭 TIM4 计数器（避免干扰共用 TIM4 的 CH1）
  */
static void ws2812_2_send(uint8_t *colors, uint16_t len)
{
    ws2812_2_color_to_buffer(colors, len);

    /* 1. 仅关闭 CH2 输出，不停止定时器 */
    CLEAR_BIT(TIM4->CCER, TIM_CCER_CC2E);

    /* 2. 终止 DMA + 清除标志 */
    HAL_DMA_Abort(&hdma_tim4_ch2);
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_ch2, DMA_FLAG_TC4);
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_ch2, DMA_FLAG_HT4);
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_ch2, DMA_FLAG_TE4);

    /* 3. CCR2=1 触发首次比较事件 → DMA 写入第一条真实数据 */
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 1);

    /* 4. 启动 DMA（写 CCR2） */
    HAL_DMA_Start(&hdma_tim4_ch2, (uint32_t)dma_buffer2,
                  (uint32_t)&TIM4->CCR2, TOTAL_BITS2);

    /* 5. 使能 CC2 DMA 请求 */
    SET_BIT(TIM4->DIER, TIM_DIER_CC2DE);

    /* 6. 确保定时器在运行 */
    SET_BIT(TIM4->CR1, TIM_CR1_CEN);

    /* 7. 重新使能 CH2 输出 → 首次比较匹配触发 DMA 传输 */
    SET_BIT(TIM4->CCER, TIM_CCER_CC2E);

    /* 8. 等待 DMA 传输完成 */
    uint32_t timeout = 100000;
    while (__HAL_DMA_GET_FLAG(&hdma_tim4_ch2, DMA_FLAG_TC4) == RESET && --timeout) { }
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_ch2, DMA_FLAG_TC4);

    /* 9. 关闭 CC2 DMA 请求（防止误触发） */
    CLEAR_BIT(TIM4->DIER, TIM_DIER_CC2DE);

    /* 10. 等待复位脉冲完成 */
    for (volatile uint16_t d = 0; d < 200; d++) { __NOP(); }
}

/* ── 公共 API ─────────────────────────────────── */

void ws2812_2_set_all(uint8_t red, uint8_t green, uint8_t blue)
{
    ws2812_2_base_r = red;
    ws2812_2_base_g = green;
    ws2812_2_base_b = blue;

    static uint8_t color_data[NUM_LEDS2 * 3];
    for (uint16_t i = 0; i < NUM_LEDS2; i++) {
        color_data[i * 3 + 0] = green;
        color_data[i * 3 + 1] = red;
        color_data[i * 3 + 2] = blue;
    }
    ws2812_2_send(color_data, NUM_LEDS2);
}

/* ── 状态查询 ─────────────────────────────────── */

uint8_t ws2812_2_is_open(void)
{
    return (ws2812_2_base_r > 0 || ws2812_2_base_g > 0 || ws2812_2_base_b > 0) ? 1 : 0;
}

uint8_t ws2812_2_get_base_r(void)      { return ws2812_2_base_r; }
uint8_t ws2812_2_get_base_g(void)      { return ws2812_2_base_g; }
uint8_t ws2812_2_get_base_b(void)      { return ws2812_2_base_b; }
