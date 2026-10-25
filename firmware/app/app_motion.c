#include "app_runtime.h"
#include "app_config.h"
#include "board.h"
#include "lis2dh12.h"
#include "orientation.h"

void app_motion_task(void *arg)
{
    (void)arg;
    orientation_t state;
    orientation_init(&state);
    int ready = 0, previous_error = -1;
    TickType_t last_good = xTaskGetTickCount();
    for (;;) {
        app_task_beat(APP_TASK_MOTION);
        int error;
        if (!ready) {
            error = lis2dh12_init();
            if (!error) {
                ready = 1;
                last_good = xTaskGetTickCount();
                vTaskDelay(pdMS_TO_TICKS(80));
            }
        } else error = LIS2DH12_OK;
        lis2dh12_sample_t sample;
        if (!error) error = lis2dh12_read(&sample);
        TickType_t now = xTaskGetTickCount();
        if (error == LIS2DH12_NOT_READY &&
            (TickType_t)(now-last_good) <= pdMS_TO_TICKS(1000)) {
            /* Never confirm a new direction across a missing sample. */
            state.pending = 0;
            vTaskDelay(pdMS_TO_TICKS(CFG_MOTION_POLL_MS));
            continue;
        }
        app_event_t event = {.kind = APP_EVENT_MOTION};
        event.value.motion.error = (uint8_t)error;
        if (!error) {
            last_good = now;
            event.value.motion.x_mg = sample.x_mg;
            event.value.motion.y_mg = sample.y_mg;
            event.value.motion.z_mg = sample.z_mg;
            event.value.motion.valid = 1;
            event.value.motion.rotation = orientation_update(&state, sample.x_mg,
                sample.y_mg, sample.z_mg, (uint32_t)(now * portTICK_PERIOD_MS));
            event.value.motion.flat = state.flat;
            event.value.motion.pending = state.pending;
            event.value.motion.usable = state.usable;
        } else {
            ready = 0;
            orientation_invalidate(&state);
            event.value.motion.rotation = state.rotation;
        }
        (void)app_event_post(&event);
        if (error != previous_error) {
            if (error) board_log("[motion] LIS2DH12 error=%d; retain LCD direction; retry in 1s", error);
            else board_log("[motion] LIS2DH12 ready: 50Hz HR +/-2g; gravity samples available");
        }
        previous_error = error;
        app_task_beat(APP_TASK_MOTION);
        vTaskDelay(pdMS_TO_TICKS(error ? 1000u : CFG_MOTION_POLL_MS));
    }
}
