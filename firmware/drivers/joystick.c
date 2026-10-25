#include "joystick.h"

#include "adc_bus.h"
#include "board_pins.h"

static const char *const s_names[JOY_DIR_COUNT] = {
    "none", "up", "down", "left", "right", "center",
};

void joy_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;

    uint32_t moder = GPIOC->MODER;
    moder &= ~(3u << (BUTTON_ADC_PIN * 2u));
    moder |= (3u << (BUTTON_ADC_PIN * 2u)); /* analogue */
    GPIOC->MODER = moder;

    adc_bus_init();
}

uint16_t joy_raw(void)
{
    return adc_bus_read(BUTTON_ADC_CHANNEL);
}

uint16_t joy_capture_idle(void)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 16u; ++i) {
        sum += joy_raw();
    }
    return (uint16_t)(sum / 16u);
}

const char *joy_dir_name(joy_dir_t dir)
{
    if ((unsigned)dir >= JOY_DIR_COUNT) {
        return "?";
    }
    return s_names[dir];
}

