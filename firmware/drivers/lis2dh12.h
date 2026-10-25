#ifndef LIS2DH12_H
#define LIS2DH12_H
#include <stdint.h>
enum { LIS2DH12_OK, LIS2DH12_NOT_READY, LIS2DH12_IO, LIS2DH12_ID, LIS2DH12_CONFIG };
typedef struct { int16_t x_mg, y_mg, z_mg; } lis2dh12_sample_t;
int lis2dh12_init(void);
/* 50 Hz, high-resolution +/-2g, BDU on. Outputs remain unchanged on failure. */
int lis2dh12_read(lis2dh12_sample_t *sample);
#endif
