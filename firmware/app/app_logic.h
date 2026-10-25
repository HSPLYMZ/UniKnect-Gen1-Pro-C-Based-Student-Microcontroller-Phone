#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include "phone_logic.h"

enum { APP_PAGE_STATUS, APP_PAGE_TASKS, APP_PAGE_IO, APP_PAGE_HOME, APP_PAGE_MENU,
       APP_PAGE_DIALER, APP_PAGE_INBOX, APP_PAGE_MESSAGE, APP_PAGE_CALL,
       APP_PAGE_INCOMING, APP_PAGE_JOYSTICK, APP_PAGE_CONTACT_ENTRY,
       APP_PAGE_SMS_ENTRY, APP_PAGE_CONTACTS, APP_PAGE_SIGNAL_ENTRY, APP_PAGE_SIGNAL_INFO, APP_PAGE_SETTINGS_ENTRY, APP_PAGE_SETTINGS_MENU, APP_PAGE_ROTATION, APP_PAGE_CAL_CONFIRM, APP_PAGE_COUNT };
enum { APP_TASK_CONTROL, APP_TASK_LIGHT, APP_TASK_INPUT, APP_TASK_DISPLAY,
       APP_TASK_CONSOLE, APP_TASK_HEALTH, APP_TASK_MOTION, APP_TASK_STORAGE,
       APP_TASK_VOICE, APP_TASK_COUNT };
enum { APP_SD_WAITING, APP_SD_READY, APP_SD_LIMIT, APP_SD_ERROR, APP_SD_DISABLED };
typedef enum {
    APP_EVENT_LIGHT, APP_EVENT_INPUT, APP_EVENT_PAGE, APP_EVENT_LCD,
    APP_EVENT_HEALTH, APP_EVENT_LCD_RETRY, APP_EVENT_LIGHT_FAULT, APP_EVENT_INPUT_FAULT,
    APP_EVENT_MOTION, APP_EVENT_ROTATION_MODE, APP_EVENT_PHONE, APP_EVENT_JOY,
    APP_EVENT_STORAGE, APP_EVENT_LCD_CONFIG, APP_EVENT_LCD_BENCH
} app_event_kind_t;

typedef struct {
    app_event_kind_t kind;
    union {
        uint16_t raw;
        uint8_t page; /* APP_PAGE_COUNT means next page. */
        struct { uint8_t ready, error, rotation; uint32_t flushed_cells; } lcd;
        struct {
            int16_t x_mg, y_mg, z_mg;
            uint8_t valid, error, rotation, flat, pending, usable;
        } motion;
        struct { uint8_t automatic, rotation; } rotation_mode;
        phone_event_t phone;
        struct { uint16_t idle, levels[5]; uint8_t stage, wait_release; } joy;
        struct { uint32_t free_mib,total_mib,writes; uint8_t state; } storage;
        struct {
            uint32_t heap_free, heap_min, stale_mask, stack_low_mask;
            uint32_t event_drops, log_drops, rx_drops, idle_loops;
        } health;
    } value;
} app_event_t;

typedef struct {
    uint64_t uptime_ms;
    uint32_t heap_free, heap_min, stale_mask, stack_low_mask;
    uint32_t event_drops, log_drops, rx_drops, idle_loops;
    uint32_t light_samples, input_samples, lcd_flushed_cells, lcd_retry_generation;
    uint16_t light_raw, input_raw;
    uint8_t light_valid, input_valid, light_exposed, light_streak;
    uint8_t light_error, input_error;
    uint8_t page, lcd_ready, lcd_error;
    uint8_t lcd_profile, lcd_bench;
    int16_t accel_x_mg, accel_y_mg, accel_z_mg;
    uint32_t motion_samples;
    uint8_t motion_valid, motion_error, motion_flat, motion_pending, motion_usable;
    uint8_t rotation_auto, motion_rotation, rotation, lcd_rotation;
    uint16_t joy_idle, joy_levels[5];
    uint8_t joy_stage, joy_wait_release;
    uint32_t sd_free_mib,sd_total_mib,sd_writes;
    uint8_t sd_state;
    phone_state_t phone;
} app_model_t;

typedef struct {
    const char *name;
    uint32_t age_ms, beats;
    uint16_t stack_free_words, stack_words;
    uint8_t priority, stale;
    uint8_t active;
} app_task_info_t;

typedef enum {
    APP_CMD_INVALID, APP_CMD_HELP, APP_CMD_STATUS, APP_CMD_TASKS,
    APP_CMD_PAGE, APP_CMD_LCD_RETRY, APP_CMD_MOTION, APP_CMD_ROTATE, APP_CMD_PHONE,
    APP_CMD_JOY_CALIBRATE, APP_CMD_LCD_CONFIG, APP_CMD_LCD_BENCH, APP_CMD_SIGNAL_STATUS, APP_CMD_SETTINGS_STATUS
} app_command_kind_t;
typedef struct { app_command_kind_t kind; uint8_t page; phone_event_t phone; } app_command_t;
typedef struct { char text[64]; uint8_t used, dropping; } app_line_t;

void app_model_init(app_model_t *model);
/* Owner task only. Returns zero for an invalid event without changing state. */
int app_model_apply(app_model_t *model, const app_event_t *event);
app_command_t app_command_parse(const char *line);
/* UART byte or -2 loss marker: 1 complete line, -1 discarded line, 0 pending.
 * Completed text remains available until the next byte. CRLF produces one line. */
int app_line_feed(app_line_t *line, int byte);
/* Unsigned subtraction works across the 32-bit tick wrap. */
int app_heartbeat_late(uint32_t now, uint32_t last, uint32_t deadline);

#endif
