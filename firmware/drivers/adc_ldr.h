#ifndef ADC_LDR_H
#define ADC_LDR_H

#include <stdint.h>

/* Raw 12-bit reading of the LDR channel (PC5). Returns 0..4095. */
uint16_t adc_ldr_read_raw(void);
void adc_ldr_init(void);
int adc_ldr_read_checked(uint16_t *value);

#endif /* ADC_LDR_H */
