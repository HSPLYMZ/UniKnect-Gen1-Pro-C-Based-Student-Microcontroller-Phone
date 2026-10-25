#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H
#include "app_logic.h"
#include "WouoUI_anim.h"

#define UI_COLS 27u /* ceil(160/6), last cell has four visible pixels */
#define UI_ROWS 20u /* maximum: portrait has 20 rows, landscape 16 */
enum { UI_WHITE, UI_CYAN, UI_GREEN, UI_YELLOW, UI_RED, UI_GRAY, UI_INK, UI_PAPER };
enum { UI_CELL_BITMAP=1, UI_THEME_BLACK=0, UI_THEME_RETRO=1 };
typedef struct { char ch; uint8_t color, flags, pixels[8]; } ui_cell_t;
typedef struct { uint8_t cols, rows, theme; ui_cell_t cells[UI_ROWS][UI_COLS]; } ui_frame_t;
uint8_t ui_ascii_row(char c, unsigned row);
uint32_t ui_clock_ms(void); /* RTOS clock; zero in standalone render tests */
void ui_compose_cost(uint32_t *graph_ms,uint32_t *convert_ms);
void ui_plot(ui_frame_t *frame, int x, int y, int ink);

void ui_compose(ui_frame_t *frame, const app_model_t *model,
                const app_task_info_t tasks[APP_TASK_COUNT]);
typedef struct {
    AnimPos carousel, cursor, viewport;
    uint32_t last_ms;
    uint8_t valid, page, rotation;
} ui_animation_t;
/* Real WouoUI nonlinear easing; all state is owned by the display task. */
int ui_compose_animated(ui_frame_t *frame, const app_model_t *model,
                       const app_task_info_t tasks[APP_TASK_COUNT],
                       ui_animation_t *animation, uint32_t now_ms);
/* Call only after a successful black panel flush or lcd_init(). */
void ui_cache_reset(void);
/* Display owner only. Returns successful cell count, -1 on bus error.
 * No cache entry is committed until the associated transfer succeeds. */
int ui_flush_step(const ui_frame_t *frame, unsigned cell_budget);
/* Compose the whole rotated page in RAM, then transfer once; commit on success. */
int ui_render_full(const ui_frame_t *frame);

#endif
