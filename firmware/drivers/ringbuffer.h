/* Single-producer / single-consumer byte ring buffer.
 *
 * Producer is the UART DMA-IDLE ISR, consumer is the at_client parser task, so
 * head is only written by the ISR and tail only by the task. That makes the
 * 16-bit indices safe without disabling interrupts; the size must be a power of
 * two so the mask replaces a modulo.
 */
#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    uint16_t size;   /* power of two */
    uint16_t mask;   /* size - 1 */
    volatile uint16_t head;  /* written by producer only */
    volatile uint16_t tail;  /* written by consumer only */
} ringbuffer_t;

void     rb_init(ringbuffer_t *rb, uint8_t *storage, uint16_t size);
uint16_t rb_length(const ringbuffer_t *rb);
uint16_t rb_free(const ringbuffer_t *rb);
/* Returns bytes actually written (may be less than len when full). */
uint16_t rb_put(ringbuffer_t *rb, const uint8_t *data, uint16_t len);
/* Returns bytes actually read (may be less than len when empty). */
uint16_t rb_get(ringbuffer_t *rb, uint8_t *data, uint16_t len);
void     rb_reset(ringbuffer_t *rb);

#endif /* RINGBUFFER_H */
