#include "orientation.h"
#include "app_config.h"
#include <string.h>

static int32_t magnitude(int32_t x) { return x < 0 ? -x : x; }
_Static_assert(CFG_MOTION_X_AXIS >= 0 && CFG_MOTION_X_AXIS < 3 &&
               CFG_MOTION_Y_AXIS >= 0 && CFG_MOTION_Y_AXIS < 3 &&
               CFG_MOTION_Z_AXIS >= 0 && CFG_MOTION_Z_AXIS < 3 &&
               CFG_MOTION_X_AXIS != CFG_MOTION_Y_AXIS &&
               CFG_MOTION_X_AXIS != CFG_MOTION_Z_AXIS &&
               CFG_MOTION_Y_AXIS != CFG_MOTION_Z_AXIS, "Invalid motion axis map");
_Static_assert((CFG_MOTION_X_SIGN == 1 || CFG_MOTION_X_SIGN == -1) &&
               (CFG_MOTION_Y_SIGN == 1 || CFG_MOTION_Y_SIGN == -1), "Invalid motion axis signs");
void orientation_init(orientation_t *s) { memset(s, 0, sizeof *s); }
void orientation_invalidate(orientation_t *s)
{ s->pending = 0; s->initialized = 0; s->flat = 0; s->usable = 0; }

uint8_t orientation_update(orientation_t *s, int16_t x, int16_t y, int16_t z, uint32_t now)
{
    int32_t input[3] = {x, y, z};
    int64_t norm = (int64_t)x*x + (int64_t)y*y + (int64_t)z*z;
    if (norm < 700*700 || norm > 1300*1300) {
        orientation_invalidate(s);
        return s->rotation;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!s->initialized) s->filtered[i] = input[i];
        else s->filtered[i] += (input[i] - s->filtered[i]) / 4;
    }
    s->initialized = 1;
    int32_t gx = s->filtered[CFG_MOTION_X_AXIS] * CFG_MOTION_X_SIGN;
    int32_t gy = s->filtered[CFG_MOTION_Y_AXIS] * CFG_MOTION_Y_SIGN;
    int32_t gz = s->filtered[CFG_MOTION_Z_AXIS];
    int32_t ax = magnitude(gx), ay = magnitude(gy), az = magnitude(gz);
    int32_t major = ax > ay ? ax : ay, minor = ax > ay ? ay : ax;
    s->flat = az >= 800 || major < az + 100;
    if (s->flat || major < 650 || major - minor < 200) {
        s->pending = 0;
        s->usable = 0;
        return s->rotation;
    }
    s->usable = 1;
    uint8_t candidate = ax > ay ? (gx < 0 ? 1 : 3) : (gy > 0 ? 0 : 2);
    candidate = (candidate + CFG_MOTION_ROTATION_OFFSET) & 3u;
    if (candidate == s->rotation) s->pending = 0;
    else if (!s->pending || s->candidate != candidate) {
        s->candidate = candidate; s->candidate_since = now; s->pending = 1;
    } else if ((uint32_t)(now - s->candidate_since) >= CFG_MOTION_STABLE_MS) {
        s->rotation = candidate; s->pending = 0;
    }
    return s->rotation;
}
