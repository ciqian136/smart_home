#include "home_cmd.h"

#include "board_led.h"
#include "fan.h"
#include "json_parser.h"
#include "my_uart.h"
#include "ws2812.h"
#include "ws2812_2.h"

#include <string.h>

/* 调试时改为 1，正常使用保持 0 */
#define HOME_CMD_DEBUG 0

#if HOME_CMD_DEBUG
#define HOME_CMD_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define HOME_CMD_DEBUG_PRINTF(...) ((void)0)
#endif

/* 标准命名一轮的字段个数，顺序必须和下面 parse_onenet_params 的键一致 */
#define HOME_CMD_PARAM_COUNT 8U

#define HOME_CMD_FAN_MAX 1000
#define HOME_CMD_RGB_MAX 255

static int clamp_int(int value, int low, int high)
{
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

/* {"op":"query"}（容忍冒号后有一个空格）。只做精确串匹配，
 * 不走 parse_onenet_params —— 那个的 's' 分支要求输出缓冲至少 64 字节。 */
static uint8_t json_is_query(const char *json)
{
  static const char *const forms[] = {"\"op\":\"query\"", "\"op\": \"query\""};

  for (uint8_t i = 0U; i < (uint8_t)(sizeof(forms) / sizeof(forms[0])); i++) {
    if (strstr(json, forms[i]) != NULL) return 1U;
  }
  return 0U;
}

home_cmd_result_t home_cmd_apply(const char *json)
{
  int led = 0;
  int fan_value = 0;
  int r1 = 0;
  int g1 = 0;
  int b1 = 0;
  int r2 = 0;
  int g2 = 0;
  int b2 = 0;

  /* OneNET 旧命名，单独一轮解析，最后和标准命名合并 */
  int led_alias = 0;
  int ar1 = 0;
  int ag1 = 0;
  int ab1 = 0;
  int ar2 = 0;
  int ag2 = 0;
  int ab2 = 0;

  uint8_t found[HOME_CMD_PARAM_COUNT] = {0U};
  uint8_t alias[7] = {0U};

  uint8_t applied = 0U;

  uint8_t has_led;
  uint8_t has_r1;
  uint8_t has_g1;
  uint8_t has_b1;
  uint8_t has_r2;
  uint8_t has_g2;
  uint8_t has_b2;
  int v_led;
  int v_r1;
  int v_g1;
  int v_b1;
  int v_r2;
  int v_g2;
  int v_b2;

  if (json == NULL) return HOME_CMD_NONE;

  /* 标准命名：和 home/state 里的字段同名，两端对照时不用换算 */
  (void)parse_onenet_params(json, HOME_CMD_PARAM_COUNT, found,
                            "led", 'b', &led,
                            "fan", 'i', &fan_value,
                            "rgb1_r", 'i', &r1,
                            "rgb1_g", 'i', &g1,
                            "rgb1_b", 'i', &b1,
                            "rgb2_r", 'i', &r2,
                            "rgb2_g", 'i', &g2,
                            "rgb2_b", 'i', &b2);

  /* OneNET 旧命名，兼容早期云端下发的 JSON */
  (void)parse_onenet_params(json, 7U, alias,
                            "LED", 'b', &led_alias,
                            "RGB1_RAD", 'i', &ar1,
                            "RGB1_GREEN", 'i', &ag1,
                            "RGB1_BLUE", 'i', &ab1,
                            "RGB2_RAD", 'i', &ar2,
                            "RGB2_GREEN", 'i', &ag2,
                            "RGB2_BLUE", 'i', &ab2);

  has_led = (uint8_t)(found[0] || alias[0]);
  v_led = found[0] ? led : led_alias;

  has_r1 = (uint8_t)(found[2] || alias[1]);
  has_g1 = (uint8_t)(found[3] || alias[2]);
  has_b1 = (uint8_t)(found[4] || alias[3]);
  v_r1 = found[2] ? r1 : ar1;
  v_g1 = found[3] ? g1 : ag1;
  v_b1 = found[4] ? b1 : ab1;

  has_r2 = (uint8_t)(found[5] || alias[4]);
  has_g2 = (uint8_t)(found[6] || alias[5]);
  has_b2 = (uint8_t)(found[7] || alias[6]);
  v_r2 = found[5] ? r2 : ar2;
  v_g2 = found[6] ? g2 : ag2;
  v_b2 = found[7] ? b2 : ab2;

  if (has_led) {
    board_led_set((uint8_t)((v_led != 0) ? 1U : 0U));
    applied = 1U;
  }

  if (found[1]) {
    fan_set((uint16_t)clamp_int(fan_value, 0, HOME_CMD_FAN_MAX));
    applied = 1U;
  }

  /* 只改了某个通道时，另外两个通道保持当前值：指令可以只发 "rgb1_r" */
  if (has_r1 || has_g1 || has_b1) {
    uint8_t r = (uint8_t)clamp_int(has_r1 ? v_r1 : (int)ws2812_get_base_r(),
                                   0, HOME_CMD_RGB_MAX);
    uint8_t g = (uint8_t)clamp_int(has_g1 ? v_g1 : (int)ws2812_get_base_g(),
                                   0, HOME_CMD_RGB_MAX);
    uint8_t b = (uint8_t)clamp_int(has_b1 ? v_b1 : (int)ws2812_get_base_b(),
                                   0, HOME_CMD_RGB_MAX);
    ws2812_set_all(r, g, b);
    applied = 1U;
    HOME_CMD_DEBUG_PRINTF("[CMD] rgb1=%u,%u,%u\r\n",
                          (unsigned int)r, (unsigned int)g, (unsigned int)b);
  }

  if (has_r2 || has_g2 || has_b2) {
    uint8_t r = (uint8_t)clamp_int(has_r2 ? v_r2 : (int)ws2812_2_get_base_r(),
                                   0, HOME_CMD_RGB_MAX);
    uint8_t g = (uint8_t)clamp_int(has_g2 ? v_g2 : (int)ws2812_2_get_base_g(),
                                   0, HOME_CMD_RGB_MAX);
    uint8_t b = (uint8_t)clamp_int(has_b2 ? v_b2 : (int)ws2812_2_get_base_b(),
                                   0, HOME_CMD_RGB_MAX);
    ws2812_2_set_all(r, g, b);
    applied = 1U;
    HOME_CMD_DEBUG_PRINTF("[CMD] rgb2=%u,%u,%u\r\n",
                          (unsigned int)r, (unsigned int)g, (unsigned int)b);
  }

  if (applied) {
    HOME_CMD_DEBUG_PRINTF("[CMD] applied led=%u fan=%u\r\n",
                          (unsigned int)has_led, (unsigned int)found[1]);
    return HOME_CMD_APPLIED;
  }

  if (json_is_query(json)) return HOME_CMD_QUERY;

  HOME_CMD_DEBUG_PRINTF("[CMD] nothing recognised\r\n");
  return HOME_CMD_NONE;
}
