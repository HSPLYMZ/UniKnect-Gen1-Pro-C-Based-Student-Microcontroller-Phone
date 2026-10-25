#ifndef ORIENTATION_H
#define ORIENTATION_H
#include <stdint.h>
typedef struct {
    int32_t filtered[3];
    uint32_t candidate_since;
    uint8_t initialized, rotation, candidate, pending, flat, usable;
} orientation_t;
void orientation_init(orientation_t *state);
void orientation_invalidate(orientation_t *state);
/* Rotation 0/1/2/3 means clockwise 0/90/180/270 relative to verified landscape.
 * Holds direction when flat, ambiguous, moving, invalid, or not yet stable. */
uint8_t orientation_update(orientation_t *state, int16_t x, int16_t y, int16_t z, uint32_t now_ms);
#endif
