/*
 * 风扇控制模块
 * TIM4 PWM 模式2，通道3 → PD14
 * TIM4 ARR=89（与 WS2812 共享），PWM 模式2 极性反转
 * API 范围：0=停转，1000=满速
 * 内部映射：val(0~1000) → CCR(89~0)
 */

#include "fan.h"
#include "my_uart.h"

#define FAN_CHANNEL    TIM_CHANNEL_3
#define FAN_TIM        htim4
#define FAN_MAX        1000
#define FAN_TIM_ARR    89          /* TIM4 ARR，与 WS2812 共享 */
#define FAN_SPEED_STEP 200         /* 加减速步进值 */

/* 调试时改为 1，正常使用保持 0 */
#define FAN_DEBUG 0
#define FAN_DEBUG_INTERVAL_MS 1000U

#if FAN_DEBUG
#define FAN_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define FAN_DEBUG_PRINTF(...) ((void)0)
#endif

extern TIM_HandleTypeDef htim4;

/* 当前转速状态 */
static uint16_t fan_cur_speed = 0;

/**
  * @brief  风扇初始化
  * @note   PWM 模式2：CCR=ARR 输出 0% 占空比（停转）
  */
void fan_init(void)
{
    fan_cur_speed = 0;
    __HAL_TIM_SET_COMPARE(&FAN_TIM, FAN_CHANNEL, FAN_TIM_ARR);
    HAL_TIM_PWM_Start(&FAN_TIM, FAN_CHANNEL);
}

/**
  * @brief  设置风扇转速
  * @param  val  转速值 (0~1000)
  *             0    = 停转 (CCR=89, 0% 占空比)
  *             1000 = 满速 (CCR=0,  100% 占空比)
  *         PWM 模式2 反相：CCR = ARR - val*ARR/FAN_MAX
  */
void fan_set(uint16_t val)
{
#if FAN_DEBUG
    static uint32_t debug_last_tick = 0U;
    static uint8_t debug_has_printed = 0U;
#endif

    if (val > FAN_MAX) val = FAN_MAX;
    fan_cur_speed = val;

    /* API 0~1000 线性映射到 CCR 89~0（PWM 模式2：低 CCR = 高占空比）*/
    uint16_t ccr = FAN_TIM_ARR - (uint32_t)val * FAN_TIM_ARR / FAN_MAX;
    __HAL_TIM_SET_COMPARE(&FAN_TIM, FAN_CHANNEL, ccr);

#if FAN_DEBUG
    uint32_t now = HAL_GetTick();
    if (!debug_has_printed || now - debug_last_tick >= FAN_DEBUG_INTERVAL_MS) {
        debug_has_printed = 1U;
        debug_last_tick = now;
        FAN_DEBUG_PRINTF("[FAN] speed=%u ccr=%u open=%u\r\n",
                         (unsigned int)fan_cur_speed,
                         (unsigned int)ccr,
                         (unsigned int)fan_is_open());
    }
#endif
}

/**
  * @brief  风扇加速（+FAN_SPEED_STEP）
  */
void fan_speed_up(void)
{
    uint16_t new_speed = fan_cur_speed + FAN_SPEED_STEP;
    if (new_speed > FAN_MAX) new_speed = FAN_MAX;
    fan_set(new_speed);
}

/**
  * @brief  风扇减速（-FAN_SPEED_STEP）
  */
void fan_speed_down(void)
{
    uint16_t new_speed = (fan_cur_speed < FAN_SPEED_STEP)
                         ? 0 : fan_cur_speed - FAN_SPEED_STEP;
    fan_set(new_speed);
}

/**
  * @brief  按档位设置风扇（1~4 档）
  * @param  gear  档位 1~4，超出范围无效
  */
void fan_set_gear(uint8_t gear)
{
    /* 档位 → API 值映射（内部自动转换为 CCR）*/
    /* 1档=250(25%)  2档=500(50%)  3档=750(75%)  4档=1000(100%) */
    static const uint16_t gear_table[] = {0, 250, 500, 750, 1000};

    if (gear >= 1 && gear <= 4) {
        fan_set(gear_table[gear]);
    }
}

/* ========== 状态查询 ========== */

/**
  * @brief  查询风扇是否开启
  * @return 1=开启，0=关闭
  */
uint8_t fan_is_open(void)
{
    return (fan_cur_speed > 0) ? 1 : 0;
}

/**
  * @brief  获取当前转速
  * @return PWM 值 0~1000
  */
uint16_t fan_get_speed(void)
{
    return fan_cur_speed;
}
