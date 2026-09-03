#include "my_uart.h"
#include "esp32.h"
#include "ring_queue.h"
#include "usart.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UART1_RX_QUEUE_SIZE 512U
#define UART2_RX_QUEUE_SIZE 2048U
#define UART3_RX_QUEUE_SIZE 256U
#define UART4_RX_QUEUE_SIZE 256U

#define UART1_TX_QUEUE_SIZE 2048U
#define UART2_TX_QUEUE_SIZE 4096U
#define UART3_TX_QUEUE_SIZE 512U
#define UART4_TX_QUEUE_SIZE 1024U

#define UART1_RX_WORK_SIZE 128U
#define UART2_RX_WORK_SIZE 256U
#define UART3_RX_WORK_SIZE 64U
#define UART4_RX_WORK_SIZE 64U

#define UART1_TX_WORK_SIZE 128U
#define UART2_TX_WORK_SIZE 256U
#define UART3_TX_WORK_SIZE 64U
#define UART4_TX_WORK_SIZE 128U

#define UART_PRINTF_BUF_SIZE 512U

/* 调试时改为 1，正常使用保持 0 */
#define UART_DEBUG 0
#define UART_DEBUG_INTERVAL_MS 1000U

#if UART_DEBUG
#define UART_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define UART_DEBUG_PRINTF(...) ((void)0)
#endif

typedef struct {
    UART_HandleTypeDef *huart;
    ring_queue_t rx_queue;
    uint8_t *rx_storage;
    uint16_t rx_storage_size;
    uint8_t *rx_work;
    uint16_t rx_work_size;
    ring_queue_t tx_queue;
    uint8_t *tx_storage;
    uint16_t tx_storage_size;
    uint8_t *tx_work;
    uint16_t tx_work_size;
    volatile uint8_t tx_busy;
    volatile uint16_t tx_len;
    volatile uint32_t tx_overflow;
} uart_ring_port_t;

static uint8_t uart1_rx_storage[UART1_RX_QUEUE_SIZE];
static uint8_t uart2_rx_storage[UART2_RX_QUEUE_SIZE];
static uint8_t uart3_rx_storage[UART3_RX_QUEUE_SIZE];
static uint8_t uart4_rx_storage[UART4_RX_QUEUE_SIZE];

static uint8_t uart1_tx_storage[UART1_TX_QUEUE_SIZE];
static uint8_t uart2_tx_storage[UART2_TX_QUEUE_SIZE];
static uint8_t uart3_tx_storage[UART3_TX_QUEUE_SIZE];
static uint8_t uart4_tx_storage[UART4_TX_QUEUE_SIZE];

static uint8_t uart1_rx_work[UART1_RX_WORK_SIZE];
static uint8_t uart2_rx_work[UART2_RX_WORK_SIZE];
static uint8_t uart3_rx_work[UART3_RX_WORK_SIZE];
static uint8_t uart4_rx_work[UART4_RX_WORK_SIZE];

static uint8_t uart1_tx_work[UART1_TX_WORK_SIZE];
static uint8_t uart2_tx_work[UART2_TX_WORK_SIZE];
static uint8_t uart3_tx_work[UART3_TX_WORK_SIZE];
static uint8_t uart4_tx_work[UART4_TX_WORK_SIZE];

static uart_ring_port_t uart_ports[] = {
    {&huart1, {0}, uart1_rx_storage, sizeof(uart1_rx_storage), uart1_rx_work, sizeof(uart1_rx_work),
     {0}, uart1_tx_storage, sizeof(uart1_tx_storage), uart1_tx_work, sizeof(uart1_tx_work), 0U, 0U, 0U},
    {&huart2, {0}, uart2_rx_storage, sizeof(uart2_rx_storage), uart2_rx_work, sizeof(uart2_rx_work),
     {0}, uart2_tx_storage, sizeof(uart2_tx_storage), uart2_tx_work, sizeof(uart2_tx_work), 0U, 0U, 0U},
    {&huart3, {0}, uart3_rx_storage, sizeof(uart3_rx_storage), uart3_rx_work, sizeof(uart3_rx_work),
     {0}, uart3_tx_storage, sizeof(uart3_tx_storage), uart3_tx_work, sizeof(uart3_tx_work), 0U, 0U, 0U},
    {&huart4, {0}, uart4_rx_storage, sizeof(uart4_rx_storage), uart4_rx_work, sizeof(uart4_rx_work),
     {0}, uart4_tx_storage, sizeof(uart4_tx_storage), uart4_tx_work, sizeof(uart4_tx_work), 0U, 0U, 0U},
};

static uint32_t irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void irq_restore(uint32_t primask)
{
    if (primask == 0U) {
        __enable_irq();
    }
}

static uart_ring_port_t *uart_get_port(UART_HandleTypeDef *huart)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(uart_ports) / sizeof(uart_ports[0])); i++) {
        if (uart_ports[i].huart == huart) {
            return &uart_ports[i];
        }
    }
    return 0;
}

static void uart_start_rx(uart_ring_port_t *port)
{
    if (port == 0 || port->huart == 0) return;
    (void)HAL_UARTEx_ReceiveToIdle_IT(port->huart, port->rx_work, port->rx_work_size);
}

static HAL_StatusTypeDef uart_tx_kick(uart_ring_port_t *port)
{
    uint16_t available;
    uint16_t tx_len;
    uint8_t ch;
    uint32_t primask;
    HAL_StatusTypeDef status;

    if (port == 0 || port->huart == 0 || port->tx_work == 0) return HAL_ERROR;

    primask = irq_save();
    if (port->tx_busy) {
        irq_restore(primask);
        return HAL_BUSY;
    }

    available = ring_queue_available(&port->tx_queue);
    if (available == 0U) {
        irq_restore(primask);
        return HAL_OK;
    }

    tx_len = (available > port->tx_work_size) ? port->tx_work_size : available;
    for (uint16_t i = 0U; i < tx_len; i++) {
        if (!ring_queue_peek_at(&port->tx_queue, i, &ch)) {
            tx_len = i;
            break;
        }
        port->tx_work[i] = ch;
    }

    if (tx_len == 0U) {
        irq_restore(primask);
        return HAL_OK;
    }

    port->tx_busy = 1U;
    port->tx_len = tx_len;
    status = HAL_UART_Transmit_IT(port->huart, port->tx_work, tx_len);
    if (status != HAL_OK) {
        port->tx_busy = 0U;
        port->tx_len = 0U;
    }
    irq_restore(primask);

    return status;
}

void uart_printf(UART_HandleTypeDef *huart, const char *format, ...)
{
    char buf[UART_PRINTF_BUF_SIZE];
    va_list ap;
    int len;

    if (huart == 0 || format == 0) return;

    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (len <= 0) return;
    if ((uint32_t)len >= sizeof(buf)) {
        len = (int)sizeof(buf) - 1;
    }

    (void)my_uart_write(huart, (const uint8_t *)buf, (uint16_t)len);
}

void my_uart_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(uart_ports) / sizeof(uart_ports[0])); i++) {
        ring_queue_init(&uart_ports[i].rx_queue,
                        uart_ports[i].rx_storage,
                        uart_ports[i].rx_storage_size);
        ring_queue_init(&uart_ports[i].tx_queue,
                        uart_ports[i].tx_storage,
                        uart_ports[i].tx_storage_size);
        memset(uart_ports[i].rx_work, 0, uart_ports[i].rx_work_size);
        memset(uart_ports[i].tx_work, 0, uart_ports[i].tx_work_size);
        uart_ports[i].tx_busy = 0U;
        uart_ports[i].tx_len = 0U;
        uart_ports[i].tx_overflow = 0U;
        uart_start_rx(&uart_ports[i]);
    }
}

uint16_t my_uart_write(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t written;
    uint32_t primask;

    if (port == 0 || data == 0 || len == 0U) return 0U;

    primask = irq_save();
    if (ring_queue_free(&port->tx_queue) < len) {
        port->tx_overflow += len;
        irq_restore(primask);
        (void)uart_tx_kick(port);
        return 0U;
    }

    written = ring_queue_write(&port->tx_queue, data, len);
    irq_restore(primask);

    (void)uart_tx_kick(port);
    return written;
}

uint16_t my_uart_tx_pending(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t count;
    uint32_t primask;

    if (port == 0) return 0U;

    primask = irq_save();
    count = ring_queue_available(&port->tx_queue);
    irq_restore(primask);

    return count;
}

uint16_t my_uart_tx_free(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t free_len;
    uint32_t primask;

    if (port == 0) return 0U;

    primask = irq_save();
    free_len = ring_queue_free(&port->tx_queue);
    irq_restore(primask);

    return free_len;
}

uint32_t my_uart_get_tx_overflow(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint32_t overflow;
    uint32_t primask;

    if (port == 0) return 0U;

    primask = irq_save();
    overflow = port->tx_overflow;
    irq_restore(primask);

    return overflow;
}

void my_uart_service_tx(void)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(uart_ports) / sizeof(uart_ports[0])); i++) {
        (void)uart_tx_kick(&uart_ports[i]);
    }

#if UART_DEBUG
    static uint32_t debug_last_tick = 0U;
    uint32_t now = HAL_GetTick();
    if (now - debug_last_tick >= UART_DEBUG_INTERVAL_MS) {
        debug_last_tick = now;
        UART_DEBUG_PRINTF("[UART] U1 tx=%u free=%u rx=%u txov=%lu rxov=%lu | U2 tx=%u free=%u rx=%u txov=%lu rxov=%lu\r\n",
                          (unsigned int)my_uart_tx_pending(&huart1),
                          (unsigned int)my_uart_tx_free(&huart1),
                          (unsigned int)my_uart_available(&huart1),
                          (unsigned long)my_uart_get_tx_overflow(&huart1),
                          (unsigned long)my_uart_get_rx_overflow(&huart1),
                          (unsigned int)my_uart_tx_pending(&huart2),
                          (unsigned int)my_uart_tx_free(&huart2),
                          (unsigned int)my_uart_available(&huart2),
                          (unsigned long)my_uart_get_tx_overflow(&huart2),
                          (unsigned long)my_uart_get_rx_overflow(&huart2));
        UART_DEBUG_PRINTF("[UART] U3 tx=%u free=%u rx=%u txov=%lu rxov=%lu | U4 tx=%u free=%u rx=%u txov=%lu rxov=%lu\r\n",
                          (unsigned int)my_uart_tx_pending(&huart3),
                          (unsigned int)my_uart_tx_free(&huart3),
                          (unsigned int)my_uart_available(&huart3),
                          (unsigned long)my_uart_get_tx_overflow(&huart3),
                          (unsigned long)my_uart_get_rx_overflow(&huart3),
                          (unsigned int)my_uart_tx_pending(&huart4),
                          (unsigned int)my_uart_tx_free(&huart4),
                          (unsigned int)my_uart_available(&huart4),
                          (unsigned long)my_uart_get_tx_overflow(&huart4),
                          (unsigned long)my_uart_get_rx_overflow(&huart4));
    }
#endif
}

uint16_t my_uart_available(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t count;
    uint32_t primask;

    if (port == 0) return 0U;

    primask = irq_save();
    count = ring_queue_available(&port->rx_queue);
    irq_restore(primask);

    return count;
}

uint16_t my_uart_read(UART_HandleTypeDef *huart, uint8_t *dst, uint16_t capacity)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t read_len;
    uint32_t primask;

    if (port == 0 || dst == 0 || capacity == 0U) return 0U;

    primask = irq_save();
    read_len = ring_queue_read(&port->rx_queue, dst, capacity);
    irq_restore(primask);

    return read_len;
}

uint16_t my_uart_read_line(UART_HandleTypeDef *huart, char *dst, uint16_t capacity)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t available;
    uint16_t line_len = 0U;
    uint8_t ch;
    uint32_t primask;

    if (port == 0 || dst == 0 || capacity < 2U) return 0U;

    primask = irq_save();
    available = ring_queue_available(&port->rx_queue);
    if (available > (uint16_t)(capacity - 1U)) {
        available = (uint16_t)(capacity - 1U);
    }

    for (uint16_t i = 0U; i < available; i++) {
        if (ring_queue_peek_at(&port->rx_queue, i, &ch) && ch == '\n') {
            line_len = (uint16_t)(i + 1U);
            break;
        }
    }

    if (line_len > 0U) {
        (void)ring_queue_read(&port->rx_queue, (uint8_t *)dst, line_len);
    }
    irq_restore(primask);

    if (line_len == 0U) return 0U;
    dst[line_len] = '\0';
    return line_len;
}

void my_uart_clear_rx(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint32_t primask;

    if (port == 0) return;

    primask = irq_save();
    ring_queue_clear(&port->rx_queue);
    irq_restore(primask);

    if (huart == &huart2) {
        esp32_rx_pending = 0U;
    }
}

uint32_t my_uart_get_rx_overflow(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint32_t overflow;
    uint32_t primask;

    if (port == 0) return 0U;

    primask = irq_save();
    overflow = ring_queue_overflow(&port->rx_queue);
    irq_restore(primask);

    return overflow;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t copy_len;

    if (port == 0) return;
    copy_len = (Size < port->rx_work_size) ? Size : port->rx_work_size;

    if (copy_len > 0U) {
        (void)ring_queue_write(&port->rx_queue, port->rx_work, copy_len);
        if (huart == &huart2) {
            esp32_rx_pending = 1U;
        }
    }

    memset(port->rx_work, 0, port->rx_work_size);
    uart_start_rx(port);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint16_t done_len;
    uint32_t primask;

    if (port == 0) return;

    primask = irq_save();
    done_len = port->tx_len;
    if (done_len > 0U) {
        (void)ring_queue_drop(&port->tx_queue, done_len);
    }
    port->tx_len = 0U;
    port->tx_busy = 0U;
    irq_restore(primask);

    (void)uart_tx_kick(port);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    uart_ring_port_t *port = uart_get_port(huart);
    uint32_t primask;

    if (port == 0) return;
    uart_start_rx(port);

    primask = irq_save();
    port->tx_busy = 0U;
    port->tx_len = 0U;
    irq_restore(primask);

    (void)uart_tx_kick(port);
}
