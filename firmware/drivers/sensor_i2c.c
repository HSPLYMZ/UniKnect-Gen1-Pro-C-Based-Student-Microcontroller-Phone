#include "sensor_i2c.h"
#include "board_pins.h"
#include "FreeRTOS.h"
#include "semphr.h"

static StaticSemaphore_t s_i2c_lock_cb;
static SemaphoreHandle_t s_i2c_lock;

static void pause_bus(void)
{
    /* Conservative minimum delay; interrupts can only lengthen I2C phases.
     * This is a bring-up bus, not a calibrated hardware clock generator. */
    for (volatile uint32_t n = 0; n < SystemCoreClock / 500000u; ++n) __NOP();
}
static void sda(int release)
{
    SENSOR_SDA_PORT->BSRR = BOARD_PIN(SENSOR_SDA_PIN) << (release ? 0u : 16u);
    pause_bus();
}
static void scl_low(void)
{
    SENSOR_SCL_PORT->BSRR = BOARD_PIN(SENSOR_SCL_PIN) << 16u;
    pause_bus();
}
static int scl_high(void)
{
    SENSOR_SCL_PORT->BSRR = BOARD_PIN(SENSOR_SCL_PIN);
    uint32_t limit = SystemCoreClock / 1000u;
    while (!(SENSOR_SCL_PORT->IDR & BOARD_PIN(SENSOR_SCL_PIN)))
        if (--limit == 0) return SENSOR_I2C_TIMEOUT;
    pause_bus();
    return SENSOR_I2C_OK;
}
static int sda_value(void) { return (SENSOR_SDA_PORT->IDR & BOARD_PIN(SENSOR_SDA_PIN)) != 0; }
static int stop_bus(void)
{
    scl_low(); sda(0);
    int result = scl_high();
    sda(1);
    return result;
}
static int start_bus(int repeated)
{
    sda(1);
    int error = scl_high();
    if (error) return error;
    if (!repeated && !sda_value()) {
        /* A slave interrupted mid-byte can be released with up to nine clocks. */
        for (unsigned i = 0; i < 9 && !sda_value(); ++i) {
            scl_low();
            error = scl_high();
            if (error) return error;
        }
        error = stop_bus();
        if (error) return error;
        if (!sda_value()) return SENSOR_I2C_STUCK;
    }
    sda(0); scl_low();
    return SENSOR_I2C_OK;
}
static int put_byte(uint8_t byte)
{
    for (unsigned i = 0; i < 8; ++i) {
        sda(byte & 0x80u); byte <<= 1;
        int error = scl_high();
        if (error) return error;
        scl_low();
    }
    sda(1);
    int error = scl_high();
    if (error) return error;
    int acknowledged = !sda_value();
    scl_low();
    return acknowledged ? SENSOR_I2C_OK : SENSOR_I2C_NACK;
}
static int get_byte(uint8_t *out, int last)
{
    uint8_t byte = 0;
    sda(1);
    for (unsigned i = 0; i < 8; ++i) {
        int error = scl_high();
        if (error) return error;
        byte = (uint8_t)((byte << 1) | sda_value());
        scl_low();
    }
    sda(last); /* NACK the final byte, ACK all preceding bytes. */
    int error = scl_high();
    scl_low(); sda(1);
    if (!error) *out = byte;
    return error;
}

void sensor_i2c_init(void)
{
    if (s_i2c_lock) return;
    s_i2c_lock = xSemaphoreCreateMutexStatic(&s_i2c_lock_cb);
    configASSERT(s_i2c_lock);
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;
    uint32_t pins = BOARD_PIN(SENSOR_SCL_PIN) | BOARD_PIN(SENSOR_SDA_PIN);
    GPIOB->BSRR = pins;
    GPIOB->OTYPER |= pins;
    for (unsigned pin = SENSOR_SCL_PIN; pin <= SENSOR_SDA_PIN; ++pin) {
        GPIOB->MODER = (GPIOB->MODER & ~(3u << (pin*2u))) | (1u << (pin*2u));
        GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << (pin*2u))) | (1u << (pin*2u));
        GPIOB->OSPEEDR &= ~(3u << (pin*2u));
    }
}

static int transaction(uint8_t addr, uint8_t reg, uint8_t *rx, const uint8_t *tx, size_t length)
{
    if (addr > 0x7f || !length || length > 32 || (!rx && !tx)) return SENSOR_I2C_ARGUMENT;
    if (!s_i2c_lock || xSemaphoreTake(s_i2c_lock, pdMS_TO_TICKS(20)) != pdPASS) return SENSOR_I2C_LOCK;
    int result = start_bus(0);
    if (!result) result = put_byte((uint8_t)(addr << 1));
    if (!result) result = put_byte(reg);
    if (!result && rx) result = start_bus(1);
    if (!result && rx) result = put_byte((uint8_t)((addr << 1) | 1u));
    for (size_t i = 0; !result && i < length; ++i)
        result = rx ? get_byte(&rx[i], i+1u == length) : put_byte(tx[i]);
    int stopped = stop_bus();
    if (!result) result = stopped;
    (void)xSemaphoreGive(s_i2c_lock);
    return result;
}
int sensor_i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{ return transaction(addr, reg, data, NULL, len); }
int sensor_i2c_write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{ return transaction(addr, reg, NULL, data, len); }
