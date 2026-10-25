#include "lis2dh12.h"
#include "sensor_i2c.h"
#include "board_pins.h"

#define WHO_AM_I 0x0fu
#define CTRL1 0x20u
#define CTRL2 0x21u
#define CTRL3 0x22u
#define CTRL4 0x23u
#define CTRL5 0x24u
#define CTRL6 0x25u
#define STATUS 0x27u
#define OUT_X_L 0x28u
static uint8_t s_sensor_ready;

static int write_reg(uint8_t reg, uint8_t value)
{ return sensor_i2c_write(MOTION_I2C_ADDRESS, reg, &value, 1); }

int lis2dh12_init(void)
{
    uint8_t value;
    s_sensor_ready = 0;
    if (sensor_i2c_read(MOTION_I2C_ADDRESS, WHO_AM_I, &value, 1)) return LIS2DH12_IO;
    if (value != 0x33) return LIS2DH12_ID;
    /* Power down before selecting normal DC/gravity outputs and HR +/-2g.
     * BDU prevents half-updated 16-bit axis values; burst read uses auto increment. */
    if (write_reg(CTRL1, 0x07) || write_reg(CTRL2, 0) || write_reg(CTRL3, 0) ||
        write_reg(CTRL4, 0x88) || write_reg(CTRL5, 0) || write_reg(CTRL6, 0) ||
        write_reg(CTRL1, 0x47)) return LIS2DH12_IO;
    if (sensor_i2c_read(MOTION_I2C_ADDRESS, CTRL1, &value, 1)) return LIS2DH12_IO;
    if (value != 0x47) return LIS2DH12_CONFIG;
    if (sensor_i2c_read(MOTION_I2C_ADDRESS, CTRL4, &value, 1)) return LIS2DH12_IO;
    if (value != 0x88) return LIS2DH12_CONFIG;
    s_sensor_ready = 1;
    return LIS2DH12_OK;
}

int lis2dh12_read(lis2dh12_sample_t *out)
{
    if (!s_sensor_ready || !out) return LIS2DH12_CONFIG;
    uint8_t status, raw[6];
    if (sensor_i2c_read(MOTION_I2C_ADDRESS, STATUS, &status, 1)) return LIS2DH12_IO;
    if (!(status & 0x08)) return LIS2DH12_NOT_READY;
    if (sensor_i2c_read(MOTION_I2C_ADDRESS, OUT_X_L | 0x80u, raw, sizeof raw)) return LIS2DH12_IO;
    lis2dh12_sample_t sample;
    /* In 12-bit HR +/-2g mode each effective LSB is 1 mg, left-aligned. */
    sample.x_mg = (int16_t)((uint16_t)raw[0] | (uint16_t)raw[1] << 8) / 16;
    sample.y_mg = (int16_t)((uint16_t)raw[2] | (uint16_t)raw[3] << 8) / 16;
    sample.z_mg = (int16_t)((uint16_t)raw[4] | (uint16_t)raw[5] << 8) / 16;
    *out = sample;
    return LIS2DH12_OK;
}
