/* Shared ADC1 access.
 *
 * Both the LDR (PC5 / IN15) and the joystick (PC1 / IN11) sit on ADC1 and are
 * read from different tasks, so every read must be serialised: the sample time
 * register, the sequence register and the data register are all shared state.
 *
 * Two rules from the bench notes are enforced here:
 *  - sample time 480 cycles for every channel (high-impedance sources),
 *  - discard the first conversion after a channel switch.
 */
#ifndef ADC_BUS_H
#define ADC_BUS_H

#include <stdint.h>

/* Idempotent. Creates the lock with static storage, so it is safe to call
 * before the scheduler starts. */
void adc_bus_init(void);

/* Blocking, task context only. Returns a 12-bit raw value (0..4095). */
uint16_t adc_bus_read(uint8_t channel);
/* Returns 0 on success, -1 for invalid arguments, lock or conversion failure. */
int adc_bus_read_checked(uint8_t channel, uint16_t *value);

#endif /* ADC_BUS_H */
