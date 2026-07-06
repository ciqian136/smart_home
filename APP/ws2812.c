#include "ws2812.h"
#include <stdint.h>
extern DMA_HandleTypeDef hdma_tim4_up;
uint16_t dma_buffer[TOTAL_BITS];

/* 当前灯带颜色状态 */
static uint8_t ws2812_cur_r = 0;
static uint8_t ws2812_cur_g = 0;
static uint8_t ws2812_cur_b = 0;

/* 基础颜色（亮度缩放前的原始色值）和当前亮度等级 */
static uint8_t ws2812_base_r = 255;
static uint8_t ws2812_base_g = 200;
static uint8_t ws2812_base_b = 100;
static uint8_t ws2812_brightness = 0;   /* 0~100，0=关闭 */

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
  * @note  该函数阻塞等待 DMA 传输完成（或采用中断）
  */
void ws2812_send(uint8_t *colors, uint16_t len) {
    // 1. 转换数据，包含复位位
    ws2812_color_to_buffer(colors, len);

    // 2. 停止 TIM4 和 DMA（防止冲突）
    HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
    HAL_DMA_Abort(&hdma_tim4_up);

    // 4. 启动 DMA（不开启中断，若需要回调可开启）
    HAL_DMA_Start(&hdma_tim4_up, (uint32_t)dma_buffer, (uint32_t)&TIM4->CCR1, TOTAL_BITS);

    // 5. 使能 TIM4 的 DMA 请求（更新事件）
    __HAL_TIM_ENABLE_DMA(&htim4, TIM_DMA_UPDATE);

    // 6. 启动 PWM 输出
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);

    // 7. 等待 DMA 传输完成（阻塞方式）
    while (__HAL_DMA_GET_FLAG(&hdma_tim4_up, DMA_FLAG_TC7) == RESET) { }
    __HAL_DMA_CLEAR_FLAG(&hdma_tim4_up, DMA_FLAG_TC7);
}



void ws2812_set_all(uint8_t red, uint8_t green, uint8_t blue) {
    /* 记录实际输出颜色 */
    ws2812_cur_r = red;
    ws2812_cur_g = green;
    ws2812_cur_b = blue;

    /* 同时记录为基础颜色和满亮度（直接调用时不经过亮度缩放）*/
    ws2812_base_r = red;
    ws2812_base_g = green;
    ws2812_base_b = blue;
    ws2812_brightness = (red == 0 && green == 0 && blue == 0) ? 0 : 100;

    static uint8_t color_data[NUM_LEDS * 3];
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        color_data[i * 3 + 0] = green;
        color_data[i * 3 + 1] = red;
        color_data[i * 3 + 2] = blue;
    }
    ws2812_send(color_data, NUM_LEDS);
}


/**
  * @brief 设置所有 LED 颜色，亮度等级 0~100
  * @param bright_level 亮度等级 (0~100)，0 为关闭，100 为最亮
  */
void ws2812_set_all_brightness_level(uint8_t red, uint8_t green, uint8_t blue, uint8_t bright_level) {
    if (bright_level > 100) bright_level = 100;

    /* 保存基础颜色和亮度（缩放前）*/
    ws2812_base_r = red;
    ws2812_base_g = green;
    ws2812_base_b = blue;
    ws2812_brightness = bright_level;

    // 采用整数计算：结果 = (原值 * 等级) / 100
    uint8_t r_scaled = (uint16_t)red * bright_level / 100;
    uint8_t g_scaled = (uint16_t)green * bright_level / 100;
    uint8_t b_scaled = (uint16_t)blue * bright_level / 100;

    ws2812_set_all(r_scaled, g_scaled, b_scaled);
}


/**
  * @brief  仅调整亮度，保持当前基础颜色不变
  * @param  bright_level 亮度等级 (0~100)，0 为关闭
  */
void ws2812_set_brightness(uint8_t bright_level) {
    if (bright_level > 100) bright_level = 100;
    ws2812_set_all_brightness_level(ws2812_base_r, ws2812_base_g, ws2812_base_b, bright_level);
}


/* ========== 状态查询函数 ========== */

/**
  * @brief  查询灯带是否处于开启状态
  * @return 1=已开启（亮度 > 0 且任一基色 > 0），0=关闭
  */
uint8_t ws2812_is_open(void)
{
    return (ws2812_brightness > 0
            && (ws2812_base_r > 0 || ws2812_base_g > 0 || ws2812_base_b > 0)) ? 1 : 0;
}

/**
  * @brief  获取当前亮度等级
  * @return 亮度 0~100
  */
uint8_t ws2812_get_brightness(void)
{
    return ws2812_brightness;
}

/**
  * @brief  获取当前基础颜色（亮度缩放前）
  */
uint8_t ws2812_get_base_r(void) { return ws2812_base_r; }
uint8_t ws2812_get_base_g(void) { return ws2812_base_g; }
uint8_t ws2812_get_base_b(void) { return ws2812_base_b; }
