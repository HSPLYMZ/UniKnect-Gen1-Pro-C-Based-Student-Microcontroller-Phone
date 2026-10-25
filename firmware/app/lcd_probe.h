/* M2.1 display bring-up: draw a pattern that makes every wiring/init mistake
 * visible at a glance.
 *
 *   Five bars: red / green / blue / white / black; cyan outer border.
 *   Corners: top-left yellow, top-right magenta, bottom-left white, bottom-right blue.
 *   Bottom yellow marker moves two pixels every 40 ms using two narrow updates.
 */
#ifndef LCD_PROBE_H
#define LCD_PROBE_H

void lcd_probe_start(void);
void lcd_probe_run(void *arg);
void lcd_probe_progress(int ready);

#endif /* LCD_PROBE_H */
