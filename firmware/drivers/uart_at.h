/* AT UART: USART6 (PG9 RX / PG14 TX) + DMA2 Stream1 circular RX + IDLE interrupt.
 *
 * Same receive architecture as Quectel's hal_uart.c, but written against
 * registers because this firmware links CMSIS only (no ST HAL):
 *   DMA circular -> IDLE IRQ marks a frame boundary -> bytes copied into a
 *   ring buffer -> at_client parser task drains it via qosa_uart_read().
 *
 * DMA half/full and USART IDLE interrupts share priority 5 and drain one cursor.
 * Loss-free reception requires IRQ latency below half a buffer (~22 ms at
 * 115200) and a consumer that keeps up. Late DMA service and errors are counted.
 */
#ifndef UART_AT_H
#define UART_AT_H

#include <stdint.h>

/* DMA landing buffer; 512 bytes covers several AT lines or one long URC burst. */
#define AT_UART_DMA_SIZE   512u
/* Ring buffer; power of two. 2 KB absorbs a modem URC burst while the parser
 * task is busy printing. */
#define AT_UART_RB_SIZE    2048u

void at_uart_init(void);

/* Diagnostics for the console: bytes moved, ring overruns, error counters. */
typedef struct {
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t dma_wraps;
    uint32_t idle_events;
    uint32_t overrun_errors;
    uint32_t ring_dropped;
    uint32_t dma_errors;
    uint32_t dma_late_events;
} at_uart_stats_t;

void at_uart_get_stats(at_uart_stats_t *out);

#endif /* UART_AT_H */
