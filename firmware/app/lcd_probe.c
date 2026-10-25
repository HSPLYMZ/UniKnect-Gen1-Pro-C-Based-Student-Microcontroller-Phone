#include "lcd_probe.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "lcd_st7735.h"

#define LCD_TASK_STACK_WORDS  512u
#define LCD_TASK_PRIO         2u

#define ANIM_X_MIN            12
#define ANIM_X_MAX            124
#define ANIM_Y                108
#define ANIM_W                24
#define ANIM_H                16
#define ANIM_STEP             2
#define ANIM_PERIOD_MS        40u
#define ANIM_LOG_FRAMES       125u

static uint32_t elapsed_ms(TickType_t start)
{
    return (uint32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
}

static void draw_test_pattern(void)
{
    static const uint16_t colors[] = {
        LCD_RED, LCD_GREEN, LCD_BLUE, LCD_WHITE, LCD_BLACK
    };

    lcd_fill(LCD_BLACK);
    for (int i = 0; i < 5; ++i) {
        lcd_fill_rect(i * 32, 0, 32, 100, colors[i]);
    }
    lcd_frame_rect(0, 0, LCD_W, LCD_H, LCD_CYAN);
    /* Asymmetric corner marks make rotation/mirroring visible. */
    lcd_fill_rect(2, 2, 10, 10, LCD_YELLOW);
    lcd_fill_rect(LCD_W - 12, 2, 10, 10, LCD_MAGENTA);
    lcd_fill_rect(2, LCD_H - 12, 10, 10, LCD_WHITE);
    lcd_fill_rect(LCD_W - 12, LCD_H - 12, 10, 10, LCD_BLUE);
    lcd_fill_rect(ANIM_X_MIN, ANIM_Y, ANIM_W, ANIM_H, LCD_YELLOW);
}

__attribute__((weak)) void lcd_probe_progress(int ready) { (void)ready; }

void lcd_probe_run(void *arg)
{
    (void)arg;
    lcd_probe_progress(0);
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* Preserve the tested GPIO speed. The driver uses software reset only:
     * D9/PD15 resets the EC200U and is not a display reset pin. */
    lcd_set_transport(1);
    board_log("[lcd] GPIO SPI baseline: init once, 160x128 RGB565");
    lcd_init();
    lcd_probe_progress(1);

    /* First verify the direct pixel path, then the framebuffer path. */
    TickType_t start = xTaskGetTickCount();
    lcd_fill_direct(LCD_RED, LCD_W, LCD_H);
    lcd_probe_progress(1);
    board_log("[lcd] direct RED fill=%lu ms; hold 2 s", (unsigned long)elapsed_ms(start));
    vTaskDelay(pdMS_TO_TICKS(2000));

    draw_test_pattern();
    start = xTaskGetTickCount();
    lcd_flush();
    lcd_probe_progress(1);
    board_log("[lcd] full flush=%lu ms; bars left->right RED GREEN BLUE WHITE BLACK",
              (unsigned long)elapsed_ms(start));
    board_log("[lcd] cyan border; yellow top-left; magenta top-right");

    int x = ANIM_X_MIN;
    int direction = 1;
    uint32_t frames = 0;
    uint32_t flush_max_ms = 0;
    uint32_t late_frames = 0;
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t sample_start = last_wake;
    board_log("[lcd] animation: 2px/40ms, bounce, two 2x16 edges (64 pixels/frame)");
    for (;;) {
        lcd_probe_progress(1);
        if (xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ANIM_PERIOD_MS)) == pdFALSE) {
            ++late_frames;
        }
        int next_x = x + direction * ANIM_STEP;
        if (next_x > ANIM_X_MAX || next_x < ANIM_X_MIN) {
            direction = -direction;
            next_x = x + direction * ANIM_STEP;
        }

        /* Compose the final frame in RAM before sending anything. The overlap
         * stays yellow, so only the departing/entering two-pixel edges differ.
         * Do not clear the whole strip on the panel between animation frames. */
        lcd_fill_rect(x, ANIM_Y, ANIM_W, ANIM_H, LCD_BLACK);
        lcd_fill_rect(next_x, ANIM_Y, ANIM_W, ANIM_H, LCD_YELLOW);
        int clear_x = direction > 0 ? x : next_x + ANIM_W;
        int draw_x = direction > 0 ? x + ANIM_W : next_x;
        start = xTaskGetTickCount();
        lcd_flush_rect(clear_x, ANIM_Y, ANIM_STEP, ANIM_H);
        lcd_flush_rect(draw_x, ANIM_Y, ANIM_STEP, ANIM_H);
        uint32_t flush_ms = elapsed_ms(start);
        if (flush_ms > flush_max_ms) {
            flush_max_ms = flush_ms;
        }
        x = next_x;
        if (++frames == ANIM_LOG_FRAMES) {
            uint32_t sample_ms = elapsed_ms(sample_start);
            uint32_t fps_x10 = sample_ms != 0u ? frames * 10000u / sample_ms : 0u;
            board_log("[lcd] anim fps=%lu.%lu flush=%lu max=%lu ms late=%lu stack_free=%u words",
                      (unsigned long)(fps_x10 / 10u),
                      (unsigned long)(fps_x10 % 10u),
                      (unsigned long)flush_ms,
                      (unsigned long)flush_max_ms,
                      (unsigned long)late_frames,
                      (unsigned)uxTaskGetStackHighWaterMark(NULL));
            if (lcd_get_error() != LCD_ERROR_NONE)
                board_log("[lcd] transport error=%d; reinitialize before retrying", lcd_get_error());
            sample_start = xTaskGetTickCount();
            frames = 0;
            flush_max_ms = 0;
            late_frames = 0;
        }
    }
}

void lcd_probe_start(void)
{
    BaseType_t ok = xTaskCreate(lcd_probe_run, "lcd", LCD_TASK_STACK_WORDS,
                                NULL, LCD_TASK_PRIO, NULL);
    configASSERT(ok == pdPASS);
}
