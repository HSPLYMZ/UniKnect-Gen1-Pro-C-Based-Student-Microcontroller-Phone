#ifndef BOARD_H
#define BOARD_H

#include <stddef.h>
#include <stdint.h>

#include "stm32f4xx.h"

typedef enum {
    BOARD_LED_GREEN = 0,
    BOARD_LED_BLUE,
    BOARD_LED_RED,
} board_led_t;

void board_init(void);
void board_led_init(void);
void board_led_set(board_led_t led, int on);
void board_console_init(uint32_t baud);
void board_console_putc(char c);
void board_console_write(const char *s, size_t len);
void board_console_rx_start(void);
/* Single consumer: -1 empty, -2 RX loss (pending bytes discarded). */
int board_console_getc(void);
uint32_t board_console_rx_drops(void);
/* Optional task-context line sink; returns 1 when consumed (including dropped). */
int board_log_enqueue(const char *line);
void board_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void board_fault_report(uint32_t code);

#endif /* BOARD_H */
