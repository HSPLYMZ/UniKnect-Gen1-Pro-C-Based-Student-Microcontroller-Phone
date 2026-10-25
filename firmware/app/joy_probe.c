#include "joy_probe.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "joystick.h"

#define PROBE_TASK_STACK_WORDS  512u
#define PROBE_TASK_PRIO         3u
#define PROBE_POLL_MS           20u

/* A press is reported after the value has been stable for this long. */
#define PROBE_HOLD_REPORT_MS    1200u
/* Release needs this many consecutive quiet samples to avoid chattering. */
#define PROBE_RELEASE_SAMPLES   3u

typedef struct {
    uint8_t  active;
    uint8_t  announced;
    uint8_t  quiet;
    uint32_t down_at;
    uint32_t sum;
    uint32_t count;
    uint16_t vmin;
    uint16_t vmax;
} probe_state_t;

static void joy_probe_task(void *arg)
{
    (void)arg;

    joy_init();
    uint16_t idle = joy_capture_idle();

    board_log("[joy] calib start: idle=%u  channel=ADC1_IN11 (PC1)  th=120",
              (unsigned)idle);
    board_log("[joy] press  up / down / left / right / centre, one at a time,");
    board_log("[joy] release fully between presses. one line per platform.");

    probe_state_t st = {0};
    unsigned seq = 0;

    for (;;) {
        uint16_t v = joy_raw();
        uint32_t now = xTaskGetTickCount();

        int delta = (int)v - (int)idle;
        if (delta < 0) {
            delta = -delta;
        }

        if (!st.active) {
            if (delta > 120) {
                st.active = 1;
                st.announced = 0;
                st.quiet = 0;
                st.down_at = now;
                st.sum = v;
                st.count = 1;
                st.vmin = v;
                st.vmax = v;
            }
        } else {
            st.sum += v;
            st.count++;
            if (v < st.vmin) {
                st.vmin = v;
            }
            if (v > st.vmax) {
                st.vmax = v;
            }

            uint32_t held_ms = (now - st.down_at) * (uint32_t)portTICK_PERIOD_MS;

            if (!st.announced && held_ms >= PROBE_HOLD_REPORT_MS) {
                st.announced = 1;
                board_log("[joy] #%u holding raw=%u [%u..%u]",
                          seq + 1u, (unsigned)(st.sum / st.count),
                          (unsigned)st.vmin, (unsigned)st.vmax);
            }

            if (delta <= 120) {
                if (++st.quiet >= PROBE_RELEASE_SAMPLES) {
                    seq++;
                    board_log("[joy] #%u raw=%u [%u..%u] held=%lums",
                              seq, (unsigned)(st.sum / st.count),
                              (unsigned)st.vmin, (unsigned)st.vmax,
                              (unsigned long)held_ms);
                    st.active = 0;
                    st.quiet = 0;
                }
            } else {
                st.quiet = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(PROBE_POLL_MS));
    }
}

void joy_probe_start(void)
{
    BaseType_t ok = xTaskCreate(joy_probe_task, "joy", PROBE_TASK_STACK_WORDS,
                                NULL, PROBE_TASK_PRIO, NULL);
    configASSERT(ok == pdPASS);
}
