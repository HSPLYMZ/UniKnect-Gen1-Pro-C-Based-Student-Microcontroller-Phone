#ifndef SENSOR_I2C_H
#define SENSOR_I2C_H
#include <stddef.h>
#include <stdint.h>
enum { SENSOR_I2C_OK, SENSOR_I2C_NACK, SENSOR_I2C_TIMEOUT, SENSOR_I2C_STUCK,
       SENSOR_I2C_ARGUMENT, SENSOR_I2C_LOCK };
/* PB8/PB9 open-drain software I2C. Single master; S502 must select ARDU.
 * Polling with bounded stretching and recovery; task context only. */
void sensor_i2c_init(void);
int sensor_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, size_t length);
int sensor_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data, size_t length);
#endif
