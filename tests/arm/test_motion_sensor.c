#include "common.h"
#include <string.h>
#include "../../firmware/drivers/lis2dh12.c"
static uint8_t regs[128];
static int fail_io, corrupt_config;
static unsigned burst_reads;
int sensor_i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t length)
{
    CHECK(addr == MOTION_I2C_ADDRESS);
    if (fail_io) return SENSOR_I2C_NACK;
    if (length > 1) { CHECK(reg & 0x80); ++burst_reads; }
    for (size_t i = 0; i < length; ++i) data[i] = regs[(reg & 0x7f)+i];
    return 0;
}
int sensor_i2c_write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t length)
{
    CHECK(addr == MOTION_I2C_ADDRESS && length == 1);
    if (fail_io) return SENSOR_I2C_NACK;
    regs[reg] = corrupt_config ? 0 : *data;
    return 0;
}
void test_main(void)
{
    lis2dh12_sample_t sample = {123,456,789}, before;
    CHECK(lis2dh12_read(&sample) == LIS2DH12_CONFIG);
    CHECK(lis2dh12_init() == LIS2DH12_ID);
    regs[0x0f] = 0x33;
    regs[0x24] = 0x40; /* previous firmware left FIFO enabled */
    fail_io = 1; CHECK(lis2dh12_init() == LIS2DH12_IO);
    fail_io = 0; corrupt_config = 1; CHECK(lis2dh12_init() == LIS2DH12_CONFIG);
    corrupt_config = 0; CHECK(lis2dh12_init() == 0);
    CHECK(regs[0x20] == 0x47 && regs[0x23] == 0x88 && regs[0x21] == 0);
    CHECK(regs[0x22] == 0 && regs[0x24] == 0 && regs[0x25] == 0);
    before = sample;
    CHECK(lis2dh12_read(&sample) == LIS2DH12_NOT_READY && !memcmp(&sample,&before,sizeof sample));
    regs[0x27] = 0x08;
    uint8_t axes[6] = {0x80,0x3e,0x80,0xc1,0x00,0x80}; /* +1000,-1000,-2048 mg */
    memcpy(regs+0x28,axes,6);
    CHECK(lis2dh12_read(&sample) == 0 && burst_reads == 1);
    CHECK(sample.x_mg == 1000 && sample.y_mg == -1000 && sample.z_mg == -2048);
    before = sample; fail_io = 1;
    CHECK(lis2dh12_read(&sample) == LIS2DH12_IO && !memcmp(&sample,&before,sizeof sample));
    test_finish(0);
}
