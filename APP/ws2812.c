#include "ws2812.h"
#include <stdint.h>
extern DMA_HandleTypeDef hdma_tim4_up;
uint16_t dma_buffer[TOTAL_BITS];

/* 灯带1 基础颜色（向后兼容）*/

/* 基础颜色 */
static uint8_t ws2812_base_r = 0;
static uint8_t ws2812_base_g = 0;
static uint8_t ws2812_base_b = 0;

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

/* ================================================================
 * 统一多灯带管理层
 * ================================================================ */

static ws2812_strip_t ws2812_strips[MAX_STRIPS];
static uint8_t         ws2812_strip_count = 0;

/**
  * @brief  注册一条灯带
  */
void ws2812_strip_init(uint8_t id, uint16_t num_leds, ws2812_set_all_func_t set_all)
{
    if (ws2812_strip_count >= MAX_STRIPS) return;

    ws2812_strip_t *s = &ws2812_strips[ws2812_strip_count++];
    s->id        = id;
    s->num_leds  = num_leds;
    s->cur_r     = 0;
    s->cur_g     = 0;
    s->cur_b     = 0;
    s->set_all   = set_all;
}

/**
  * @brief  设置指定灯带全部 LED 为同一颜色（防抖）
  */
void ws2812_strip_set_all(uint8_t id, uint8_t r, uint8_t g, uint8_t b)
{
    /* 查找灯带 */
    ws2812_strip_t *s = NULL;
    for (uint8_t i = 0; i < ws2812_strip_count; i++) {
        if (ws2812_strips[i].id == id) { s = &ws2812_strips[i]; break; }
    }
    if (s == NULL || s->set_all == NULL) return;

    /* 防抖：同值跳过，避免 HMI 反馈回环造成的重复 DMA */
    if (r == s->cur_r && g == s->cur_g && b == s->cur_b) return;

    s->cur_r = r;
    s->cur_g = g;
    s->cur_b = b;

    /* 直接通过函数指针调用硬件驱动，无需 switch/if-else */
    s->set_all(r, g, b);
}

void ws2812_strip_set_all_force(uint8_t id, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_strip_t *s = NULL;
    for (uint8_t i = 0; i < ws2812_strip_count; i++) {
        if (ws2812_strips[i].id == id) {
            s = &ws2812_strips[i];
            break;
        }
    }
    if (s == NULL || s->set_all == NULL) return;
    s->cur_r = r;
    s->cur_g = g;
    s->cur_b = b;
    s->set_all(r, g, b);
}

/* ---- 状态查询 ---- */

uint8_t ws2812_strip_is_open(uint8_t id)
{
    for (uint8_t i = 0; i < ws2812_strip_count; i++) {
        if (ws2812_strips[i].id == id) {
            ws2812_strip_t *s = &ws2812_strips[i];
            return (s->cur_r > 0 || s->cur_g > 0 || s->cur_b > 0) ? 1 : 0;
        }
    }
    return 0;
}

uint8_t ws2812_strip_get_r(uint8_t id)
{
    for (uint8_t i = 0; i < ws2812_strip_count; i++)
        if (ws2812_strips[i].id == id) return ws2812_strips[i].cur_r;
    return 0;
}

uint8_t ws2812_strip_get_g(uint8_t id)
{
    for (uint8_t i = 0; i < ws2812_strip_count; i++)
        if (ws2812_strips[i].id == id) return ws2812_strips[i].cur_g;
    return 0;
}

uint8_t ws2812_strip_get_b(uint8_t id)
{
    for (uint8_t i = 0; i < ws2812_strip_count; i++)
        if (ws2812_strips[i].id == id) return ws2812_strips[i].cur_b;
    return 0;
}

uint16_t ws2812_strip_get_led_count(uint8_t id)
{
    for (uint8_t i = 0; i < ws2812_strip_count; i++)
        if (ws2812_strips[i].id == id) return ws2812_strips[i].num_leds;
    return 0;
}

uint8_t ws2812_strip_get_count(void)
{
    return ws2812_strip_count;
}
