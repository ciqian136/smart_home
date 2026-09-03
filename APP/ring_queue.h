#ifndef __RING_QUEUE_H__
#define __RING_QUEUE_H__

#include <stdint.h>

typedef struct {
    uint8_t *buffer;
    uint16_t size;
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    volatile uint32_t overflow;
} ring_queue_t;

void ring_queue_init(ring_queue_t *queue, uint8_t *buffer, uint16_t size);
void ring_queue_clear(ring_queue_t *queue);

uint8_t ring_queue_push(ring_queue_t *queue, uint8_t data);
uint16_t ring_queue_write(ring_queue_t *queue, const uint8_t *data, uint16_t len);

uint8_t ring_queue_pop(ring_queue_t *queue, uint8_t *data);
uint16_t ring_queue_read(ring_queue_t *queue, uint8_t *data, uint16_t len);
uint16_t ring_queue_drop(ring_queue_t *queue, uint16_t len);

uint8_t ring_queue_peek_at(const ring_queue_t *queue, uint16_t offset, uint8_t *data);
uint16_t ring_queue_available(const ring_queue_t *queue);
uint16_t ring_queue_free(const ring_queue_t *queue);
uint32_t ring_queue_overflow(const ring_queue_t *queue);

#endif
