#ifndef JOY_INPUT_H
#define JOY_INPUT_H

#include <stdint.h>
#include "joystick.h"

/* One owner: the 20 ms input task. Fixed ladder levels allow immediate use;
 * the existing five-step calibration remains available as a service fallback. */
typedef struct {
    uint16_t idle, levels[5], candidate;
    uint8_t stage, wait_release, stable, released;
    uint8_t held, pending, debounce;
    uint16_t held_ticks;
    uint8_t fixed_levels;
} joy_input_t;

void joy_input_reset(joy_input_t *state);
void joy_input_use_fixed(joy_input_t *state);
/* Returns one debounced/repeated press, or JOY_NONE. Invalid ADC samples never
 * generate keys. Stage 0=idle, 1..5=UP..CENTER, 6=ready. */
joy_dir_t joy_input_feed(joy_input_t *state, uint16_t raw, int valid);
/* Calibration labels are screen-relative at calibration_rotation. Re-map only
 * the difference when the panel later rotates. */
joy_dir_t joy_screen_direction(joy_dir_t direction, unsigned calibration_rotation,
                               unsigned current_rotation);

#endif
