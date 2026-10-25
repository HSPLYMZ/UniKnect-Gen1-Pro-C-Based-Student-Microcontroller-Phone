/* M1 step 1: prove the AT engine works against the real EC200U.
 *
 * The vendored RT-Thread at_client owns the USART6 parser task; this module is
 * the application side: initialise the client, run a fixed command sequence,
 * print every response line, then summarise. Nothing here is production logic —
 * it exists to make an AT wiring/pin/baud problem obvious on the console.
 */
#ifndef AT_PROBE_H
#define AT_PROBE_H

/* Creates the probe task. Call after board_init(). */
void at_probe_start(void);

#endif /* AT_PROBE_H */
