/* Byte-stream interface between at_client and the AT UART.
 *
 * The vendored at_client calls exactly these four functions. Our implementation
 * lives in firmware/drivers/uart_at.c and uses USART6 + DMA2 + IDLE interrupt
 * with a ring buffer, mirroring the pattern in Quectel's hal_uart.c but written
 * against registers instead of the ST HAL (this firmware still links CMSIS only).
 */
#ifndef HAL_UART_H
#define HAL_UART_H

#include <stddef.h>

#include "qosa_def.h"

typedef int (*event_callback_t)(void);

/* Prepare the UART. Idempotent: at_client_init() calls it. */
int qosa_uart_open(void);

/* Blocking transmit. Returns bytes accepted (== size on success, 0 on error). */
int qosa_uart_write(int pos, const char *buffer, size_t size);

/* Non-blocking receive: returns the number of bytes actually copied (0..size). */
int qosa_uart_read(int pos, const char *buffer, size_t size);

/* Register the "data arrived" hook. Called from the UART ISR context, so the
 * callback must be ISR-safe (at_client uses it to release a semaphore). */
void qosa_uart_register(event_callback_t event_cb);

#endif /* HAL_UART_H */
