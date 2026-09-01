#include "my_uart.h"
#include "esp32.h"
#include <stdint.h>

volatile uint8_t uart_rx_byte = 0;
char uart_rx_buf[512] = {0};
volatile uint16_t uart_rx_len = 0;

volatile uint8_t uart2_rx_byte = 0;
char uart2_rx_buf[512] = {0};
volatile uint16_t uart2_rx_len = 0;

/* ReceiveToIdle writes into these work buffers; the public buffers are stable
 * snapshots consumed by the cooperative application tasks. */
static uint8_t uart2_work_buf[sizeof(uart2_rx_buf)];
static uint8_t uart3_work_buf[sizeof(uart3_rx_buf)];
static uint8_t uart4_work_buf[sizeof(uart4_rx_buf)];
static uint8_t uart5_work_buf[sizeof(uart5_rx_buf)];

#define UART2_FRAME_QUEUE_DEPTH 3U
#define UART3_FRAME_QUEUE_DEPTH 6U
#define UART4_FRAME_QUEUE_DEPTH 4U
#define UART5_FRAME_QUEUE_DEPTH 6U

static uint8_t uart2_frame_queue[UART2_FRAME_QUEUE_DEPTH][sizeof(uart2_rx_buf)];
static uint16_t uart2_frame_len[UART2_FRAME_QUEUE_DEPTH];
static uint8_t uart2_frame_head = 0U;
static uint8_t uart2_frame_tail = 0U;
static uint8_t uart2_frame_count = 0U;
static uint32_t uart2_frame_drops = 0U;

static uint8_t uart3_frame_queue[UART3_FRAME_QUEUE_DEPTH][sizeof(uart3_rx_buf)];
static uint16_t uart3_frame_len[UART3_FRAME_QUEUE_DEPTH];
static uint8_t uart3_frame_head = 0U;
static uint8_t uart3_frame_tail = 0U;
static uint8_t uart3_frame_count = 0U;
static uint32_t uart3_frame_drops = 0U;

static uint8_t uart4_frame_queue[UART4_FRAME_QUEUE_DEPTH][sizeof(uart4_rx_buf)];
static uint16_t uart4_frame_len[UART4_FRAME_QUEUE_DEPTH];
static uint8_t uart4_frame_head = 0U;
static uint8_t uart4_frame_tail = 0U;
static uint8_t uart4_frame_count = 0U;
static uint32_t uart4_frame_drops = 0U;

static uint8_t uart5_frame_queue[UART5_FRAME_QUEUE_DEPTH][sizeof(uart5_rx_buf)];
static uint16_t uart5_frame_len[UART5_FRAME_QUEUE_DEPTH];
static uint8_t uart5_frame_head = 0U;
static uint8_t uart5_frame_tail = 0U;
static uint8_t uart5_frame_count = 0U;
static uint32_t uart5_frame_drops = 0U;

volatile uint8_t uart3_rx_byte = 0;
char uart3_rx_buf[125] = {0};
volatile uint8_t uart3_rx_len = 0;
char uart3_msg_buf[125] = {0};
volatile uint8_t uart3_msg_len = 0;
volatile uint8_t uart3_msg_pending = 0;

volatile uint8_t uart4_rx_byte = 0;
char uart4_rx_buf[125] = {0};
volatile uint8_t uart4_rx_len = 0;

volatile uint8_t uart5_rx_byte = 0;
char uart5_rx_buf[64] = {0};
volatile uint8_t uart5_rx_len = 0;
char uart5_msg_buf[64] = {0};
volatile uint8_t uart5_msg_len = 0;
volatile uint8_t uart5_msg_pending = 0;


/**
  * @brief  串口格式化打印函数（类似 printf，支持可变参数）
  *         内部使用 vsnprintf 格式化字符串，再通过 HAL_UART_Transmit 发送
  * @param huart  目标串口句柄
  * @param format 格式化字符串
  * @param ...    可变参数列表
  */
void uart_printf(UART_HandleTypeDef *huart, const char *format, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, format);
  int len = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  if (len > 0)
    HAL_UART_Transmit(huart, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

void my_uart_init(void)
{
  uart2_frame_head = uart2_frame_tail = uart2_frame_count = 0U;
  uart3_frame_head = uart3_frame_tail = uart3_frame_count = 0U;
  uart4_frame_head = uart4_frame_tail = uart4_frame_count = 0U;
  uart5_frame_head = uart5_frame_tail = uart5_frame_count = 0U;
  uart2_frame_drops = 0U;
  uart3_frame_drops = 0U;
  uart4_frame_drops = 0U;
  uart5_frame_drops = 0U;
  HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)uart_rx_buf,  sizeof(uart_rx_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart2, uart2_work_buf, sizeof(uart2_work_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart3, uart3_work_buf, sizeof(uart3_work_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart4, uart4_work_buf, sizeof(uart4_work_buf));
  HAL_UARTEx_ReceiveToIdle_IT(&huart5, uart5_work_buf, sizeof(uart5_work_buf));
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == &huart1) {
        uart_rx_len = Size < sizeof(uart_rx_buf) ? Size : sizeof(uart_rx_buf) - 1U;
        uart_rx_buf[uart_rx_len] = '\0';
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)uart_rx_buf, sizeof(uart_rx_buf));
    } else if (huart == &huart2) {
        uint16_t copy_len = Size < sizeof(uart2_work_buf) ? Size : sizeof(uart2_work_buf);
        if (copy_len > 0U && uart2_frame_count < UART2_FRAME_QUEUE_DEPTH) {
            memcpy(uart2_frame_queue[uart2_frame_head], uart2_work_buf, copy_len);
            uart2_frame_len[uart2_frame_head] = copy_len;
            uart2_frame_head = (uint8_t)((uart2_frame_head + 1U) % UART2_FRAME_QUEUE_DEPTH);
            uart2_frame_count++;
        } else if (copy_len > 0U) {
            uart2_frame_drops++;
        }
        esp32_rx_pending = uart2_frame_count ? 1U : 0U;
        memset(uart2_work_buf, 0, sizeof(uart2_work_buf));
        HAL_UARTEx_ReceiveToIdle_IT(&huart2, uart2_work_buf, sizeof(uart2_work_buf));
    } else if (huart == &huart3) {
        uint16_t copy_len = Size < sizeof(uart3_work_buf) ? Size : sizeof(uart3_work_buf);
        if (copy_len > 0U && uart3_frame_count < UART3_FRAME_QUEUE_DEPTH) {
            memcpy(uart3_frame_queue[uart3_frame_head], uart3_work_buf, copy_len);
            uart3_frame_len[uart3_frame_head] = copy_len;
            uart3_frame_head = (uint8_t)((uart3_frame_head + 1U) % UART3_FRAME_QUEUE_DEPTH);
            uart3_frame_count++;
        } else if (copy_len > 0U) {
            uart3_frame_drops++;
        }
        uart3_msg_pending = uart3_frame_count ? 1U : 0U;
        uart3_rx_len = uart3_frame_count ? 1U : 0U;
        memset(uart3_work_buf, 0, sizeof(uart3_work_buf));
        HAL_UARTEx_ReceiveToIdle_IT(&huart3, uart3_work_buf, sizeof(uart3_work_buf));
    } else if (huart == &huart4) {
        uint16_t copy_len = Size < sizeof(uart4_work_buf) ? Size : sizeof(uart4_work_buf);
        if (copy_len > 0U && uart4_frame_count < UART4_FRAME_QUEUE_DEPTH) {
            memcpy(uart4_frame_queue[uart4_frame_head], uart4_work_buf, copy_len);
            uart4_frame_len[uart4_frame_head] = copy_len;
            uart4_frame_head = (uint8_t)((uart4_frame_head + 1U) % UART4_FRAME_QUEUE_DEPTH);
            uart4_frame_count++;
        } else if (copy_len > 0U) {
            uart4_frame_drops++;
        }
        uart4_rx_len = uart4_frame_count ? 1U : 0U;
        memset(uart4_work_buf, 0, sizeof(uart4_work_buf));
        HAL_UARTEx_ReceiveToIdle_IT(&huart4, uart4_work_buf, sizeof(uart4_work_buf));
    } else if (huart == &huart5) {
        uint16_t copy_len = Size < sizeof(uart5_work_buf) ? Size : sizeof(uart5_work_buf);
        if (copy_len > 0U && uart5_frame_count < UART5_FRAME_QUEUE_DEPTH) {
            memcpy(uart5_frame_queue[uart5_frame_head], uart5_work_buf, copy_len);
            uart5_frame_len[uart5_frame_head] = copy_len;
            uart5_frame_head = (uint8_t)((uart5_frame_head + 1U) % UART5_FRAME_QUEUE_DEPTH);
            uart5_frame_count++;
        } else if (copy_len > 0U) {
            uart5_frame_drops++;
        }
        uart5_msg_pending = uart5_frame_count ? 1U : 0U;
        uart5_rx_len = uart5_frame_count ? 1U : 0U;
        memset(uart5_work_buf, 0, sizeof(uart5_work_buf));
        HAL_UARTEx_ReceiveToIdle_IT(&huart5, uart5_work_buf, sizeof(uart5_work_buf));
    }
}

uint16_t my_uart2_take_frame(uint8_t *dst, uint16_t capacity)
{
    uint16_t len;
    uint32_t primask;

    if (dst == NULL || capacity == 0U) return 0U;
    primask = __get_PRIMASK();
    __disable_irq();
    if (uart2_frame_count == 0U) {
        if (!primask) __enable_irq();
        return 0U;
    }
    len = uart2_frame_len[uart2_frame_tail];
    if (len > capacity) len = capacity;
    memcpy(dst, uart2_frame_queue[uart2_frame_tail], len);
    uart2_frame_tail = (uint8_t)((uart2_frame_tail + 1U) % UART2_FRAME_QUEUE_DEPTH);
    uart2_frame_count--;
    esp32_rx_pending = uart2_frame_count ? 1U : 0U;
    if (!primask) __enable_irq();
    return len;
}

void my_uart2_clear_frames(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uart2_frame_head = uart2_frame_tail = uart2_frame_count = 0U;
    esp32_rx_pending = 0U;
    if (!primask) __enable_irq();
}

uint32_t my_uart2_get_drop_count(void) { return uart2_frame_drops; }

uint16_t my_uart3_take_frame(uint8_t *dst, uint16_t capacity)
{
    uint16_t len;
    uint32_t primask;

    if (dst == NULL || capacity == 0U) return 0U;
    primask = __get_PRIMASK();
    __disable_irq();
    if (uart3_frame_count == 0U) {
        uart3_rx_len = 0U;
        uart3_msg_len = 0U;
        uart3_msg_pending = 0U;
        if (!primask) __enable_irq();
        return 0U;
    }
    len = uart3_frame_len[uart3_frame_tail];
    if (len > capacity) len = capacity;
    memcpy(dst, uart3_frame_queue[uart3_frame_tail], len);
    uart3_frame_tail = (uint8_t)((uart3_frame_tail + 1U) % UART3_FRAME_QUEUE_DEPTH);
    uart3_frame_count--;
    uart3_rx_len = uart3_frame_count ? uart3_frame_len[uart3_frame_tail] : 0U;
    uart3_msg_len = uart3_rx_len > UINT8_MAX ? UINT8_MAX : (uint8_t)uart3_rx_len;
    uart3_msg_pending = uart3_frame_count ? 1U : 0U;
    if (!primask) __enable_irq();
    return len;
}

uint32_t my_uart3_get_drop_count(void) { return uart3_frame_drops; }

uint16_t my_uart4_take_frame(uint8_t *dst, uint16_t capacity)
{
    uint16_t len;
    uint32_t primask;

    if (dst == NULL || capacity == 0U) return 0U;
    primask = __get_PRIMASK();
    __disable_irq();
    if (uart4_frame_count == 0U) {
        uart4_rx_len = 0U;
        if (!primask) __enable_irq();
        return 0U;
    }
    len = uart4_frame_len[uart4_frame_tail];
    if (len > capacity) len = capacity;
    memcpy(dst, uart4_frame_queue[uart4_frame_tail], len);
    uart4_frame_tail = (uint8_t)((uart4_frame_tail + 1U) % UART4_FRAME_QUEUE_DEPTH);
    uart4_frame_count--;
    uart4_rx_len = uart4_frame_count ? uart4_frame_len[uart4_frame_tail] : 0U;
    if (!primask) __enable_irq();
    return len;
}

uint32_t my_uart4_get_drop_count(void) { return uart4_frame_drops; }

uint16_t my_uart5_take_frame(uint8_t *dst, uint16_t capacity)
{
    uint16_t len;
    uint32_t primask;

    if (dst == NULL || capacity == 0U) return 0U;
    primask = __get_PRIMASK();
    __disable_irq();
    if (uart5_frame_count == 0U) {
        uart5_rx_len = 0U;
        uart5_msg_pending = 0U;
        if (!primask) __enable_irq();
        return 0U;
    }
    len = uart5_frame_len[uart5_frame_tail];
    if (len > capacity) len = capacity;
    memcpy(dst, uart5_frame_queue[uart5_frame_tail], len);
    uart5_frame_tail = (uint8_t)((uart5_frame_tail + 1U) % UART5_FRAME_QUEUE_DEPTH);
    uart5_frame_count--;
    uart5_rx_len = uart5_frame_count ? uart5_frame_len[uart5_frame_tail] : 0U;
    uart5_msg_pending = uart5_frame_count ? 1U : 0U;
    if (!primask) __enable_irq();
    return len;
}

uint32_t my_uart5_get_drop_count(void) { return uart5_frame_drops; }


