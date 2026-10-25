/* ADC1 single-channel reader for the LDR (PC5 / IN15).
 *
 * Deliberately polling and single-channel: the bench test showed that switching
 * channels quickly makes the sample-and-hold bleed (PA0 read 20565 then 7601).
 * The shared adc_bus layer forces a long sample time and discards the first
 * conversion after a channel change, so both the LDR and the joystick can sit
 * on ADC1 without corrupting each other.
 */
#include "adc_ldr.h"

#include "adc_bus.h"
#include "board_pins.h"

void adc_ldr_init(void)
{
    static uint8_t done;
    if (!done) {
        done = 1;

        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
        (void)RCC->AHB1ENR;

        /* Analog mode for PC5. */
        uint32_t moder = GPIOC->MODER;
        moder |= (3u << (LDR_ADC_PIN * 2u));
        GPIOC->MODER = moder;

        adc_bus_init();
    }

}

int adc_ldr_read_checked(uint16_t *value)
{
    adc_ldr_init();
    return adc_bus_read_checked(LDR_ADC_CHANNEL, value);
}

uint16_t adc_ldr_read_raw(void)
{
    uint16_t value = 0;
    (void)adc_ldr_read_checked(&value);
    return value;
}
