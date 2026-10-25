#include "common.h"
#include "../../firmware/drivers/adc_bus.c"
static unsigned takes, gives;
QueueHandle_t xQueueCreateMutexStatic(uint8_t type, StaticQueue_t *cb)
{ (void)type; return (QueueHandle_t)cb; }
BaseType_t xQueueTakeMutexRecursive(QueueHandle_t q, TickType_t ticks)
{ (void)q; (void)ticks; ++takes; return pdTRUE; }
BaseType_t xQueueGiveMutexRecursive(QueueHandle_t q)
{ (void)q; ++gives; return pdTRUE; }
void test_main(void)
{
    uint16_t value = 0xbeef;
    CHECK(adc_bus_read_checked(15, &value) != 0); /* not initialized */
    adc_bus_init();
    CHECK(adc_bus_read_checked(19, &value) != 0);
    CHECK(adc_bus_read_checked(15, NULL) != 0);
    ADC1->SR = 0;
    CHECK(adc_bus_read_checked(15, &value) != 0 && value == 0xbeef);
    CHECK(takes == gives && !s_have_channel);
    ADC1->SR = ADC_SR_EOC; ADC1->DR = 1234;
    CHECK(adc_bus_read_checked(15, &value) == 0 && value == 1234);
    ADC1->DR = 0;
    CHECK(adc_bus_read_checked(11, &value) == 0 && value == 0); /* zero is valid */
    CHECK(takes == gives && s_current_channel == 11);
    ADC1->SR = 0;
    CHECK(adc_bus_read_checked(11, &value) != 0 && !s_have_channel);
    CHECK(takes == gives);
    test_finish(0);
}
