#include "ws2812.h"
#include "my_uart.h"
#include <stdint.h>
extern DMA_HandleTypeDef hdma_tim4_up;
uint16_t dma_buffer[TOTAL_BITS];

/* 调试时改为 1，正常使用保持 0 */
#define WS2812_DEBUG 0
#define WS2812_DEBUG_INTERVAL_MS 1000U

#if WS2812_DEBUG
#define WS2812_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define WS2812_DEBUG_PRINTF(...) ((void)0)
#endif

/* 基础颜色 */
static uint8_t ws2812_base_r = 255;
static uint8_t ws2812_base_g = 200;
static uint8_t ws2812_base_b = 100;

/**
  * @brief 将颜色数据转换为 PWM 占空比序列，并在末尾追加复位位
  * @param colors: 数组，每 3 字节表示一个 LED：G,R,B
  * @param len: LED 数量
  */
void ws2812_color_to_buffer(uint8_t *colors, uint16_t len) {
    uint16_t idx = 0;
    for (uint16_t led = 0; led < len; led++) {
        uint8_t g = colors[led * 3 + 0];
        uint8_t r = colors[led * 3 + 1];
        uint8_t b = colors[led * 3 + 2];
        for (uint8_t byte = 0; byte < 3; byte++) {
            uint8_t data = (byte == 0) ? g : (byte == 1 ? r : b);
            for (int8_t bit = 7; bit >= 0; bit--) {
                dma_buffer[idx++] = (data & (1 << bit)) ? CODE_1 : CODE_0;
            }
        }
    }
    // 填充复位位（低电平）
    for (uint16_t i = 0; i < RESET_BITS; i++) {
        dma_buffer[idx++] = RESET_CODE;
    }
}



/**
  * @brief 发送数据到 WS2812 灯带，自动生成复位信号
  * @param colors: 颜色数据 (G,R,B 顺序)
  * @param len: LED 数量
  * @note  直接寄存器操作，只启停 CH1，不关闭 TIM4 计数器（避免干扰共用 TIM4 的 CH2）
  */
void ws2812_send(uint8_t *colors, uint16_t len) {
    ws2812_color_to_buffer(colors, len);

    /* 1. 仅关闭 CH1 输出，不停止定时器（避免 CH2 受干扰） */
    CLEAR_BIT(TIM4->CCER, TIM_CCER_CC1E);

    /* 2. 终止 DMA + 清除标志 */
    HAL_DMA_Abort(&hdma_tim4_up);
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_up, DMA_FLAG_TC7);
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_up, DMA_FLAG_HT7);
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_up, DMA_FLAG_TE7);

    /* 3. 启动 DMA（写 CCR1） */
    HAL_DMA_Start(&hdma_tim4_up, (uint32_t)dma_buffer, (uint32_t)&TIM4->CCR1, TOTAL_BITS);

    /* 4. 使能更新 DMA 请求 */
    SET_BIT(TIM4->DIER, TIM_DIER_UDE);

    /* 5. 确保定时器在运行 */
    SET_BIT(TIM4->CR1, TIM_CR1_CEN);

    /* 6. 重新使能 CH1 输出 → 首次更新事件触发 DMA 传输 */
    SET_BIT(TIM4->CCER, TIM_CCER_CC1E);

    /* 7. 等待 DMA 传输完成 */
    uint32_t timeout = 100000;
    while (__HAL_DMA_GET_FLAG(&hdma_tim4_up, DMA_FLAG_TC7) == RESET && --timeout) { }
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_up, DMA_FLAG_TC7);

    /* 8. 关闭更新 DMA 请求（防止误触发） */
    CLEAR_BIT(TIM4->DIER, TIM_DIER_UDE);

    /* 9. 等待复位脉冲完成 */
    for (volatile uint16_t d = 0; d < 200; d++) { __NOP(); }
}



void ws2812_set_all(uint8_t red, uint8_t green, uint8_t blue) {
#if WS2812_DEBUG
    static uint32_t debug_last_tick = 0U;
    static uint8_t debug_has_printed = 0U;
#endif

    ws2812_base_r = red;
    ws2812_base_g = green;
    ws2812_base_b = blue;

    static uint8_t color_data[NUM_LEDS * 3];
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        color_data[i * 3 + 0] = green;
        color_data[i * 3 + 1] = red;
        color_data[i * 3 + 2] = blue;
    }
    ws2812_send(color_data, NUM_LEDS);

#if WS2812_DEBUG
    uint32_t now = HAL_GetTick();
    if (!debug_has_printed || now - debug_last_tick >= WS2812_DEBUG_INTERVAL_MS) {
        debug_has_printed = 1U;
        debug_last_tick = now;
        WS2812_DEBUG_PRINTF("[WS2812] r=%u g=%u b=%u open=%u\r\n",
                            (unsigned int)ws2812_base_r,
                            (unsigned int)ws2812_base_g,
                            (unsigned int)ws2812_base_b,
                            (unsigned int)ws2812_is_open());
    }
#endif
}

/* ========== 状态查询函数 ========== */

/**
  * @brief  查询灯带是否处于开启状态
  * @return 1=已开启（任一基色 > 0），0=关闭
  */
uint8_t ws2812_is_open(void)
{
    return (ws2812_base_r > 0 || ws2812_base_g > 0 || ws2812_base_b > 0) ? 1 : 0;
}

/**
  * @brief  获取当前基础颜色
  */
uint8_t ws2812_get_base_r(void) { return ws2812_base_r; }
uint8_t ws2812_get_base_g(void) { return ws2812_base_g; }
uint8_t ws2812_get_base_b(void) { return ws2812_base_b; }
