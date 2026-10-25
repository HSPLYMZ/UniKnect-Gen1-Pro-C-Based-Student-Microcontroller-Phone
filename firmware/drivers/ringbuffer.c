#include "ringbuffer.h"

#include <string.h>

void rb_init(ringbuffer_t *rb, uint8_t *storage, uint16_t size)
{
    rb->buf = storage;
    rb->size = size;
    rb->mask = (uint16_t)(size - 1u);
    rb->head = 0;
    rb->tail = 0;
}

uint16_t rb_length(const ringbuffer_t *rb)
{
    return (uint16_t)((rb->head - rb->tail) & rb->mask);
}

uint16_t rb_free(const ringbuffer_t *rb)
{
    /* One slot is always left empty so head == tail unambiguously means empty. */
    return (uint16_t)(rb->mask - rb_length(rb));
}

uint16_t rb_put(ringbuffer_t *rb, const uint8_t *data, uint16_t len)
{
    uint16_t space = rb_free(rb);
    if (len > space) {
        len = space;
    }

    uint16_t head = rb->head;
    for (uint16_t i = 0; i < len; ++i) {
        rb->buf[(uint16_t)((head + i) & rb->mask)] = data[i];
    }
    rb->head = (uint16_t)((head + len) & rb->mask);
    return len;
}

uint16_t rb_get(ringbuffer_t *rb, uint8_t *data, uint16_t len)
{
    uint16_t have = rb_length(rb);
    if (len > have) {
        len = have;
    }

    uint16_t tail = rb->tail;
    for (uint16_t i = 0; i < len; ++i) {
        data[i] = rb->buf[(uint16_t)((tail + i) & rb->mask)];
    }
    rb->tail = (uint16_t)((tail + len) & rb->mask);
    return len;
}

void rb_reset(ringbuffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
}
