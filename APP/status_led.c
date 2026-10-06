#include "status_led.h"
#include "esp32_backend.h" /* esp32_xiaozhi_link_ok() */

/* PB5：这块板子上第二颗可用的 GPIO LED，PE5 留给用户/云端控制。 */
#define STATUS_LED_GPIO GPIOB
#define STATUS_LED_PIN GPIO_PIN_5

/* 1 = 引脚拉高点亮；实测接反了就改成 0（只改这一处）。 */
#define STATUS_LED_ACTIVE_HIGH 1

/* 一次"闪"的亮/灭时长 */
#define STATUS_LED_PULSE_ON_MS 120U
#define STATUS_LED_PULSE_GAP_MS 120U
/* "发布异常"用的长闪，和短闪区分得很明显 */
#define STATUS_LED_PULSE_LONG_MS 500U
/* 未连上 / 未订阅 / 发布异常时，提示灯语的重复周期 */
#define STATUS_LED_HINT_PERIOD_MS 2000U
/* 链路建立后多久还没成功发布过，就认为卡在发布环节 */
#define STATUS_LED_STALE_MS 10000U

#define STATUS_LED_HINT_DOUBLE 2U /* 未连上 */
#define STATUS_LED_HINT_TRIPLE 3U /* 链路在、home/cmd 没订阅上 */

static uint8_t led_on = 0U;
static uint8_t pulse_left = 0U; /* 还要闪几下 */
static uint16_t pulse_on_ms = STATUS_LED_PULSE_ON_MS;
static uint32_t pulse_next_tick = 0U;
static uint32_t hint_tick = 0U;

static uint8_t link_up = 0U;
static uint8_t subscribed = 0U;
static uint32_t link_up_tick = 0U;
static uint32_t last_ok_tick = 0U;
static uint8_t pub_failed = 0U;

static uint8_t tick_reached(uint32_t tick)
{
    return ((int32_t)(HAL_GetTick() - tick) >= 0) ? 1U : 0U;
}

static void status_led_write(uint8_t on)
{
#if STATUS_LED_ACTIVE_HIGH
    HAL_GPIO_WritePin(STATUS_LED_GPIO, STATUS_LED_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(STATUS_LED_GPIO, STATUS_LED_PIN,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
    led_on = on;
}

/* 请求播 n 个短闪。正在播的时候不打断：发布单闪只有 240ms，
 * 不会和 2s 一组的提示灯语撞在一起。 */
static void status_led_request_pulse(uint8_t n, uint16_t on_ms)
{
    if (pulse_left > 0U) return;

    pulse_left = n;
    pulse_on_ms = on_ms;
    pulse_next_tick = HAL_GetTick();
}

void status_led_init(void)
{
    status_led_write(0U);
    pulse_left = 0U;
    led_on = 0U;
    link_up = 0U;
    subscribed = 0U;
    pub_failed = 0U;
    last_ok_tick = 0U;
    link_up_tick = HAL_GetTick();
    hint_tick = HAL_GetTick() + STATUS_LED_HINT_PERIOD_MS;
}

void status_led_note_publish_ok(void)
{
    last_ok_tick = HAL_GetTick();
    pub_failed = 0U;
    status_led_request_pulse(1U, STATUS_LED_PULSE_ON_MS); /* 单闪 = 这一条真的发上去了 */
}

void status_led_note_publish_fail(void)
{
    pub_failed = 1U;
    hint_tick = HAL_GetTick(); /* 立刻开始提示，不用再等一个周期 */
}

void status_led_note_subscribe(uint8_t ok)
{
    subscribed = (ok != 0U) ? 1U : 0U;
    if (subscribed == 0U) {
        hint_tick = HAL_GetTick(); /* 立刻开始三闪 */
    }
}

void status_led_poll(void)
{
    uint32_t now = HAL_GetTick();
    /* 注意用 link_ok 而不是 esp32_initialized：broker 掉线时初始化状态机
     * 仍然是 DONE，只看 initialized 会一直显示"正常"。 */
    uint8_t now_up = (esp32_xiaozhi_link_ok() != 0U) ? 1U : 0U;

    if (now_up && (link_up == 0U)) {
        /* 链路刚恢复：重新计时，并把提示周期归零 */
        link_up_tick = now;
        hint_tick = now;
    }
    link_up = now_up;

    if (pulse_left > 0U) {
        if (!tick_reached(pulse_next_tick)) return;

        if (led_on) {
            status_led_write(0U);
            pulse_next_tick = now + STATUS_LED_PULSE_GAP_MS;
            pulse_left--;
        } else {
            status_led_write(1U);
            pulse_next_tick = now + pulse_on_ms;
        }
        return;
    }

    /* 空闲：按当前状态补一次常态提示 */
    if (link_up == 0U) {
        if (tick_reached(hint_tick)) {
            hint_tick = now + STATUS_LED_HINT_PERIOD_MS;
            status_led_request_pulse(STATUS_LED_HINT_DOUBLE, STATUS_LED_PULSE_ON_MS);
        }
        return;
    }

    /* 链路在，但 home/cmd 没订阅上：控制指令收不到，小智那边就是"点不动" */
    if (subscribed == 0U) {
        if (tick_reached(hint_tick)) {
            hint_tick = now + STATUS_LED_HINT_PERIOD_MS;
            status_led_request_pulse(STATUS_LED_HINT_TRIPLE, STATUS_LED_PULSE_ON_MS);
        }
        return;
    }

    {
        /* 以"最近一次发布成功"为基准；一次都没成功过就用链路建立的时刻 */
        uint32_t reference = (last_ok_tick != 0U) ? last_ok_tick : link_up_tick;
        uint8_t stale = (uint8_t)((pub_failed != 0U) ||
                                  ((uint32_t)(now - reference) > STATUS_LED_STALE_MS));

        if (stale && tick_reached(hint_tick)) {
            hint_tick = now + STATUS_LED_HINT_PERIOD_MS;
            status_led_request_pulse(1U, STATUS_LED_PULSE_LONG_MS); /* 一记长闪 */
        }
    }
}
