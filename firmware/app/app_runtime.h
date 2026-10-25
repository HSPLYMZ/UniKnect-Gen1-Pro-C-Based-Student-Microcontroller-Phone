#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H
#include "app_logic.h"
#include "FreeRTOS.h"
#include "task.h"

void app_framework_start(void); /* ordered components, then static task registry */
int app_event_post(const app_event_t *event); /* task context, never blocks */
void app_model_snapshot(app_model_t *model);
uint8_t app_rotation_snapshot(void); /* input path needs only one byte */
void app_task_beat(unsigned id);
void app_task_snapshot(app_task_info_t info[APP_TASK_COUNT]);
unsigned app_task_active_count(void);
/* Refresh the costly FreeRTOS stack watermarks from the 1 Hz health task. */
void app_task_measure_stacks(void);
uint32_t app_event_drops(void);
uint32_t app_idle_counter(void);

void app_control_task(void *arg);
void app_light_task(void *arg);
void app_input_task(void *arg);
void app_display_task(void *arg);
void app_console_task(void *arg);
void app_health_task(void *arg);
void app_motion_task(void *arg);
void app_storage_task(void *arg);
void app_voice_task(void *arg);
void app_console_init(void);
uint32_t app_log_drops(void);
int app_input_restart_calibration(void);

#endif
