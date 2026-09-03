#include "ring_queue.h"

void ring_queue_init(ring_queue_t *queue, uint8_t *buffer, uint16_t size)
{
    if (queue == 0) return;

    queue->buffer = buffer;
    queue->size = size;
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;
    queue->overflow = 0U;
}

void ring_queue_clear(ring_queue_t *queue)
{
    if (queue == 0) return;

    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;
}

uint8_t ring_queue_push(ring_queue_t *queue, uint8_t data)
{
    if (queue == 0 || queue->buffer == 0 || queue->size == 0U) return 0U;

    if (queue->count >= queue->size) {
        queue->overflow++;
        return 0U;
    }

    queue->buffer[queue->head] = data;
    queue->head++;
    if (queue->head >= queue->size) queue->head = 0U;
    queue->count++;

    return 1U;
}

uint16_t ring_queue_write(ring_queue_t *queue, const uint8_t *data, uint16_t len)
{
    uint16_t written = 0U;

    if (queue == 0 || data == 0) return 0U;

    while (written < len) {
        if (!ring_queue_push(queue, data[written])) break;
        written++;
    }

    if (written < len && queue != 0) {
        uint16_t remaining = (uint16_t)(len - written);
        if (remaining > 1U) {
            queue->overflow += (uint32_t)(remaining - 1U);
        }
    }

    return written;
}

uint8_t ring_queue_pop(ring_queue_t *queue, uint8_t *data)
{
    if (queue == 0 || queue->buffer == 0 || data == 0) return 0U;
    if (queue->count == 0U) return 0U;

    *data = queue->buffer[queue->tail];
    queue->tail++;
    if (queue->tail >= queue->size) queue->tail = 0U;
    queue->count--;

    return 1U;
}

uint16_t ring_queue_read(ring_queue_t *queue, uint8_t *data, uint16_t len)
{
    uint16_t read_len = 0U;

    if (queue == 0 || data == 0) return 0U;

    while (read_len < len) {
        if (!ring_queue_pop(queue, &data[read_len])) break;
        read_len++;
    }

    return read_len;
}

uint16_t ring_queue_drop(ring_queue_t *queue, uint16_t len)
{
    uint16_t dropped = 0U;
    uint8_t dummy;

    if (queue == 0) return 0U;
    while (dropped < len) {
        if (!ring_queue_pop(queue, &dummy)) break;
        dropped++;
    }

    return dropped;
}

uint8_t ring_queue_peek_at(const ring_queue_t *queue, uint16_t offset, uint8_t *data)
{
    uint16_t index;

    if (queue == 0 || queue->buffer == 0 || data == 0) return 0U;
    if (offset >= queue->count) return 0U;

    index = queue->tail + offset;
    while (index >= queue->size) index -= queue->size;
    *data = queue->buffer[index];

    return 1U;
}

uint16_t ring_queue_available(const ring_queue_t *queue)
{
    if (queue == 0) return 0U;
    return queue->count;
}

uint16_t ring_queue_free(const ring_queue_t *queue)
{
    if (queue == 0 || queue->size < queue->count) return 0U;
    return (uint16_t)(queue->size - queue->count);
}

uint32_t ring_queue_overflow(const ring_queue_t *queue)
{
    if (queue == 0) return 0U;
    return queue->overflow;
}
