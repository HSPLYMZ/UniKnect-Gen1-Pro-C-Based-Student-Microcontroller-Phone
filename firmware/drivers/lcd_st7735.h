/* ST7735S physical panel, fixed 160x128 transport, GPIO SPI by default.
 * Logical drawing can rotate 0/90/180/270 degrees through the same framebuffer.
 *
 * Framebuffer-first: every drawing call writes into a 40 KB RGB565 buffer in
 * SRAM1 and nothing touches the panel until lcd_flush*(). That keeps the panel
 * off the critical path and makes partial refresh a matter of one window call.
 *
 * The panel's visible area is not aligned with the controller's RAM origin;
 * Offsets remain configurable until the border test confirms panel alignment.
 * Use one display-owner task; drawing and bus operations are not reentrant.
 */
#ifndef LCD_ST7735_H
#define LCD_ST7735_H

#include <stdint.h>

/* OYF034301 = LCD7735 (ST7735S), 1.8", 128x160 native, driven landscape.
 *
 * Geometry is not free: the controller's frame memory is 132 columns x 162
 * rows, so with MADCTL = 0x00 (MV clear) the X axis can only reach column 131.
 * A 160 px wide window is impossible there and the address counter wraps --
 * that is what produced the narrow snow band on the left edge. Landscape needs
 * MV set, which exchanges the axes: X spans the 162-row direction (160 usable)
 * and Y spans the 132-column direction (128 usable). Hence MADCTL 0x60 and the
 * 160x128 pair below, together or not at all. */
#define LCD_W  160
#define LCD_H  128
/* Physical transfer geometry stays fixed, including MADCTL=0x60. Drawing uses
 * a logical canvas rotated in RAM; quarter turns expose a 128x160 canvas. */
int lcd_width(void);
int lcd_height(void);
unsigned lcd_get_rotation(void);
/* Owner task only: clears RAM framebuffer; performs no panel transfer. */
int lcd_set_rotation(unsigned quarter_turns);

/* Controller RAM origin offset, landscape. 0/0 is the red-tab default; the
 * greentab variant wants 1/2. The 1 px cyan frame in the test pattern makes a
 * wrong value obvious (a clipped edge plus a blank stripe), so start at 0/0. */
#define LCD_COL_OFFSET  0
#define LCD_ROW_OFFSET  0

/* Some ST7735S panels ship inverted: a black fill shows up white. Flip this if
 * a red fill comes out cyan. */
#define LCD_INVERT  0

/* No dedicated LCD reset GPIO. The shield uses Arduino RESET, and this driver
 * uses SWRESET. PD15/D9 controls the EC200U reset circuit on the carrier. */

/* SPI prescaler is set at runtime with lcd_set_spi_div(); see the prototype
 * below. APB2 = 100 MHz, so BR=3 gives 6.25 MHz and BR=5 gives 1.56 MHz. */

/* RGB565 helpers. */
#define LCD_RGB(r, g, b) \
    ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

#define LCD_BLACK   LCD_RGB(0, 0, 0)
#define LCD_WHITE   LCD_RGB(255, 255, 255)
#define LCD_RED     LCD_RGB(255, 0, 0)
#define LCD_GREEN   LCD_RGB(0, 255, 0)
#define LCD_BLUE    LCD_RGB(0, 0, 255)
#define LCD_CYAN    LCD_RGB(0, 255, 255)
#define LCD_YELLOW  LCD_RGB(255, 255, 0)
#define LCD_MAGENTA LCD_RGB(255, 0, 255)
#define LCD_GRAY    LCD_RGB(128, 128, 128)
#define LCD_DARK    LCD_RGB(16, 24, 32)

void lcd_init(void);

enum { LCD_ERROR_NONE = 0, LCD_ERROR_TIMEOUT = 1, LCD_ERROR_DMA = 2 };
/* Sticky error until lcd_init(); the owner can recover/reinitialize explicitly. */
int lcd_get_error(void);

/* Select GPIO SPI (nonzero, default) or hardware SPI (zero). Applied by the
 * next lcd_init(), which must complete before using the new transport. */
void lcd_set_transport(int bitbang);

/* SPI prescaler field (CR1 BR[2:0]): 0=/2, 3=/16 (6.25 MHz), 5=/64 (1.56 MHz),
 * 7=/256. Takes effect on the next lcd_init(). */
void lcd_set_spi_div(uint32_t br_field);
/* Owner only; choose polling or DMA pixels before lcd_init(). */
void lcd_set_dma_enabled(int enabled);
void lcd_set_gpio_delay(uint32_t iterations);
typedef struct { uint32_t pixels, transfers, timeouts, dma_errors, dma_flags; } lcd_stats_t;
lcd_stats_t lcd_stats(void);

/* DISPON / DISPOFF. These change nothing about GRAM -- they only gate whether
 * the panel shows it. That makes them the cleanest possible probe of "are my
 * commands arriving at all", independent of address window, colour depth and
 * orientation. A live panel visibly flips between its off appearance (uniform
 * backlight) and whatever GRAM holds. */
void lcd_display_on(void);
void lcd_display_off(void);

/* GPIO mode has no readback implementation and returns 0 without touching SPI.
 * Hardware mode returns raw RDDID bytes; a floating MISO is not a valid ID. */
uint32_t lcd_read_id(void);

/* Writes a solid colour with the shortest possible path, bypassing the
 * framebuffer. Useful to separate "SPI works" from "framebuffer logic works". */
void lcd_fill_direct(uint16_t color, int w, int h);

/* Compatibility diagnostics: select GPIO SPI, run the shared lcd_init(), then
 * fill directly. Use software reset and leave GPIO SPI active.
 * The slow variant increases the bit delay from 12 to 100 loop iterations. */
void lcd_bitbang_fill(uint16_t color);
void lcd_bitbang_fill_slow(uint16_t color);

void lcd_fill(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_frame_rect(int x, int y, int w, int h, uint16_t color);
void lcd_draw_pixel(int x, int y, uint16_t color);
/* Owner only; complete 6x8 cell must fit the logical canvas. */
void lcd_draw_cell(int x, int y, const uint8_t rows[8], uint16_t fg, uint16_t bg);

/* Push the whole buffer, or just a rectangle of it. */
void lcd_flush(void);
void lcd_flush_rect(int x, int y, int w, int h);

#endif /* LCD_ST7735_H */
