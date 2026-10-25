/* M2.0 calibration probe for the five-way joystick.
 *
 * Prints one line per press so the ADC platforms can be read off the console
 * and pasted into the calibration table. Nothing else runs on PC1, so this is
 * the only thing that can corrupt the numbers.
 */
#ifndef JOY_PROBE_H
#define JOY_PROBE_H

void joy_probe_start(void);

#endif /* JOY_PROBE_H */
