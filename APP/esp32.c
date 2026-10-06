#include "esp32.h"
#include "esp32_backend.h"

#include "esp32_onenet.h"
#include "esp32_xiaozhi.h"
#include "my_uart.h"

#include <string.h>

#define ESP32_RX_DRAIN_CHUNK 128U
#define ESP32_AT_CMD_TIMEOUT_MS 5000U

#define ESP32_DEBUG 0

#if ESP32_DEBUG
#define ESP32_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define ESP32_DEBUG_PRINTF(...) ((void)0)
#endif

volatile uint8_t esp32_rx_pending = 0U;
volatile uint8_t at_cmd_busy = 0U;
volatile uint8_t esp32_initialized = 0U;
volatile uint8_t need_send_reply = 0U;

uint32_t esp32_at_cmd_start_tick = 0U;
uint8_t esp32_at_cmd_timeout_logged = 0U;
uint8_t esp32_got_ok = 0U;

char esp32_rx_buf[ESP32_RX_BUF_SIZE] = {0};
uint16_t esp32_rx_len = 0U;

void esp32_rx_update_pending(void)
{
  esp32_rx_pending = (my_uart_available(&huart2) > 0U) ? 1U : 0U;
}

void esp32_rx_clear(void)
{
  esp32_rx_len = 0U;
  esp32_rx_buf[0] = '\0';
  my_uart_clear_rx(&huart2);
  esp32_rx_pending = 0U;
}

void esp32_rx_drain(void)
{
  uint16_t n;

  do {
    uint16_t space = (uint16_t)(sizeof(esp32_rx_buf) - 1U - esp32_rx_len);
    if (space == 0U) break;
    if (space > ESP32_RX_DRAIN_CHUNK) space = ESP32_RX_DRAIN_CHUNK;

    n = my_uart_read(&huart2,
                     (uint8_t *)&esp32_rx_buf[esp32_rx_len],
                     space);
    esp32_rx_len = (uint16_t)(esp32_rx_len + n);
  } while (n > 0U);

  esp32_rx_buf[esp32_rx_len] = '\0';
  esp32_rx_update_pending();
}

void esp32_buf_consume(char *buf, uint16_t *len, uint16_t consumed)
{
  if (buf == NULL || len == NULL) return;

  if (consumed >= *len) {
    *len = 0U;
    buf[0] = '\0';
  } else {
    uint16_t remaining = (uint16_t)(*len - consumed);
    memmove(buf, buf + consumed, remaining);
    *len = remaining;
    buf[remaining] = '\0';
  }
}

uint8_t esp32_rx_consume_expected(const char *expected)
{
  char *match;
  char *line_end;
  uint16_t consumed;

  if (expected == NULL) return 0U;
  match = strstr(esp32_rx_buf, expected);
  if (match == NULL) return 0U;

  line_end = strpbrk(match, "\n");
  if (line_end != NULL) {
    consumed = (uint16_t)(line_end - esp32_rx_buf + 1U);
  } else {
    consumed = (uint16_t)(match - esp32_rx_buf + strlen(expected));
  }

  esp32_buf_consume(esp32_rx_buf, &esp32_rx_len, consumed);
  esp32_rx_update_pending();
  return 1U;
}

void esp32_init(void)
{
#if ESP32_MQTT_BACKEND_XIAOZHI
  esp32_xiaozhi_init();
#else
  esp32_onenet_init();
#endif
}

void esp32_init_nonblock(void)
{
#if ESP32_MQTT_BACKEND_XIAOZHI
  esp32_xiaozhi_init_nonblock();
#else
  esp32_onenet_init_nonblock();
#endif
}

void esp32_run_send(void)
{
#if ESP32_MQTT_BACKEND_XIAOZHI
  esp32_xiaozhi_run_send();
#else
  esp32_onenet_run_send();
#endif
}

void esp32_run_recv(void)
{
  esp32_rx_drain();
  if (esp32_rx_len == 0U) return;

#if ESP32_MQTT_BACKEND_XIAOZHI
  esp32_xiaozhi_process_rx();
#else
  esp32_onenet_process_rx();
#endif

  esp32_rx_update_pending();
}

void esp32_check_online(void)
{
#if ESP32_MQTT_BACKEND_XIAOZHI
  esp32_xiaozhi_check_online();
#else
  esp32_onenet_check_online();
#endif
}

void esp32_flush_reply(void)
{
#if ESP32_MQTT_BACKEND_XIAOZHI
  /* xiaozhi 后端不需要 OneNET 的 set_reply：控制指令由 home/cmd 下行，
   * 执行结果直接体现在紧随其后的一条 home/state 里（见 esp32_xiaozhi.c）。 */
#else
  esp32_onenet_flush_reply();
#endif
}

void esp32_check_cmd_timeout(void)
{
  uint32_t now = HAL_GetTick();

  if (at_cmd_busy &&
      now - esp32_at_cmd_start_tick > ESP32_AT_CMD_TIMEOUT_MS) {
#if ESP32_MQTT_BACKEND_XIAOZHI
    /* 等 AT+MQTTPUBRAW 的 '>' 期间不按通用超时收尾：ESP-AT 这时可能已经进到
     * "还差 length 个字节"的数据阶段，把 at_cmd_busy 清掉会让后面的 AT 指令
     * 被当成 MQTT 负载吃掉。这条命令由后端自己计时（见 esp32_xiaozhi.c）。 */
    if (esp32_xiaozhi_at_busy_guard()) return;
#endif
    at_cmd_busy = 0U;
#if ESP32_MQTT_BACKEND_XIAOZHI
    esp32_xiaozhi_reset_publish();
#endif
    if (!esp32_at_cmd_timeout_logged) {
      ESP32_DEBUG_PRINTF("[ESP32] AT command timeout, reset busy\r\n");
      esp32_at_cmd_timeout_logged = 1U;
    }
  }
}

int8_t send_cmd_wait_resp_it(UART_HandleTypeDef *huart,
                             char *cmd,
                             char *expected_resp,
                             uint32_t time_out_ms,
                             uint8_t max_retries)
{
  uint8_t retry_count = 0U;

  while (retry_count < max_retries) {
    uint32_t start_time;

    esp32_rx_clear();
    uart_printf(huart, "%s", cmd);
    start_time = HAL_GetTick();

    while (HAL_GetTick() - start_time < time_out_ms) {
      esp32_rx_drain();
      if (strstr(esp32_rx_buf, expected_resp) != NULL) {
        (void)esp32_rx_consume_expected(expected_resp);
        return 0;
      }
      HAL_Delay(10U);
    }

    retry_count++;
    HAL_Delay(500U);
  }

  return -1;
}
