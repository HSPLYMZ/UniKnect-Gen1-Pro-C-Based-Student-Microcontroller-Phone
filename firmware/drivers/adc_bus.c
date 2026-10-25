#include "adc_bus.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "board_pins.h"

/* Bound on the EOC spin so a wedged ADC cannot hang a task forever. */
#define ADC_EOC_SPIN_LIMIT  200000u

static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;
static uint8_t s_current_channel;
static uint8_t s_have_channel;

static void adc_set_sample_time(uint8_t ch)
{
    /* SMPR1 covers channels 10..18, SMPR2 covers 0..9; 3 bits each, 7 = 480
     * cycles. Every channel gets the slow setting, not just the one in use. */
    if (ch >= 10u) {
        uint32_t shift = 3u * (uint32_t)(ch - 10u);
        ADC1->SMPR1 = (ADC1->SMPR1 & ~(7u << shift)) | (7u << shift);
    } else {
        uint32_t shift = 3u * (uint32_t)ch;
        ADC1->SMPR2 = (ADC1->SMPR2 & ~(7u << shift)) | (7u << shift);
    }
}

static int adc_convert(uint16_t *out)
{
    uint32_t spin = 0;

    ADC1->CR2 |= ADC_CR2_SWSTART;
    while ((ADC1->SR & ADC_SR_EOC) == 0u) {
        if (++spin > ADC_EOC_SPIN_LIMIT) {
            return -1;
        }
    }
    *out = (uint16_t)(ADC1->DR & 0x0FFFu);
    return 0;
}

void adc_bus_init(void)
{
    if (s_lock == NULL) {
        /* Static allocation: no heap needed, safe before vTaskStartScheduler. */
        s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_buf);
        configASSERT(s_lock);
    }

    static uint8_t hw_done;
    if (hw_done) {
        return;
    }
    hw_done = 1;

    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    /* PCLK2 = 100 MHz, ADC clock must stay <= 36 MHz: /4 -> 25 MHz. */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;

    ADC1->CR1 = 0;            /* 12-bit, scan off */
    ADC1->CR2 = ADC_CR2_EOCS; /* one conversion per start */
    ADC1->SQR1 = 0;           /* sequence length 1 */
    ADC1->CR2 |= ADC_CR2_ADON;

    s_have_channel = 0;
}

int adc_bus_read_checked(uint8_t channel, uint16_t *out)
{
    uint16_t value = 0;
    int result = -1;
    if (!out || channel > 18u || !s_lock) return -1;
    if (xSemaphoreTakeRecursive(s_lock, portMAX_DELAY) != pdPASS) return -1;

    if (!s_have_channel || s_current_channel != channel) {
        adc_set_sample_time(channel);
        ADC1->SQR3 = channel;
        s_current_channel = channel;
        s_have_channel = 1;
        /* First conversion after a channel switch is thrown away. */
        if (adc_convert(&value) != 0) goto complete;
    }

    if (adc_convert(&value) == 0) { *out = value; result = 0; }
complete:
    if (result != 0) s_have_channel = 0;
    (void)xSemaphoreGiveRecursive(s_lock);
    return result;
}

uint16_t adc_bus_read(uint8_t channel)
{
    uint16_t value = 0;
    (void)adc_bus_read_checked(channel, &value);
    return value;
}
