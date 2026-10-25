/* Five-way joystick on the OYF034301 shield, read through the PC1 resistor
 * ladder (ADC1_IN11).
 *
 * This driver reads the raw ADC. The input task's joy_input state machine owns
 * guided calibration and directional classification; no fixed board-specific
 * ladder values are assumed.
 */
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdint.h>

typedef enum {
    JOY_NONE = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
    JOY_CENTER,
    JOY_DIR_COUNT,
    JOY_LONG_CENTER = 7, /* input actions, never ADC platforms */
    JOY_LONG_LEFT,
    JOY_LONG_RIGHT
} joy_dir_t;

/* Configures PC1 as analogue and brings up the shared ADC. */
void joy_init(void);

/* One raw 12-bit sample. Cheap enough for a 20 ms poll. */
uint16_t joy_raw(void);

/* Diagnostic helper: averages 16 samples; caller must ensure joystick released. */
uint16_t joy_capture_idle(void);

const char *joy_dir_name(joy_dir_t dir);

#endif /* JOYSTICK_H */
