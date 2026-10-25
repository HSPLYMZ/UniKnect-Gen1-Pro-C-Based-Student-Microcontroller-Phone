#include "app_runtime.h"
#include "app_config.h"
#include "board.h"
#include "queue.h"
#include <string.h>
#include <stdio.h>

#define LOG_LENGTH 192u
#define LOG_COUNT 16u
static StaticQueue_t s_log_cb;
static uint8_t s_log_storage[LOG_COUNT * LOG_LENGTH];
static QueueHandle_t s_logs;
static uint32_t s_log_drops;

void app_console_init(void)
{
    s_logs = xQueueCreateStatic(LOG_COUNT, LOG_LENGTH, s_log_storage, &s_log_cb);
    configASSERT(s_logs);
    vQueueAddToRegistry(s_logs, "console-logs");
    board_console_rx_start();
}

/* Strong implementation of the BSP's optional whole-line log sink.
 * A full queue consumes/drops the record: never fall back to interleaved TX. */
int board_log_enqueue(const char *line)
{
    if (!s_logs || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return 0;
    char record[LOG_LENGTH] = {0};
    size_t n = 0;
    while (n < sizeof record - 1u && line[n]) { record[n] = line[n]; ++n; }
    if (xQueueSend(s_logs, record, 0) != pdPASS) {
        taskENTER_CRITICAL(); ++s_log_drops; taskEXIT_CRITICAL();
    }
    return 1;
}

uint32_t app_log_drops(void)
{
    uint32_t result;
    taskENTER_CRITICAL(); result = s_log_drops; taskEXIT_CRITICAL();
    return result;
}

static void print_tasks(void)
{
    app_task_info_t tasks[APP_TASK_COUNT];
    app_task_snapshot(tasks);
    board_log("NAME PRIO STACK_FREE/TOTAL(words) AGE(ms) HEARTBEATS");
    for (unsigned i = 0; i < APP_TASK_COUNT; ++i) {
        if(!tasks[i].active) continue;
        board_log("%s %u %u/%u %lu %lu %s", tasks[i].name, tasks[i].priority,
                  tasks[i].stack_free_words, tasks[i].stack_words,
                  (unsigned long)tasks[i].age_ms, (unsigned long)tasks[i].beats,
                  tasks[i].stale ? "LATE" : "OK");
    }
}

static void print_status(void)
{
    app_model_t m;
    app_model_snapshot(&m);
    board_log("[display] profile=%u bench=%u focus=%u nav=%u history=%u sysclk=%lu top=%u row=%u list=%u scroll=%u signal_scroll=%u",
        m.lcd_profile,m.lcd_bench,m.phone.focus_bar,m.phone.nav_selected,m.phone.history_count,(unsigned long)SystemCoreClock,
        m.phone.top_page,m.phone.selected_row,m.phone.list_scroll,m.phone.message_scroll,m.phone.signal.scroll);
    board_log("[status] fw=%s uptime=%lu d + %lu s page=%u LCD=%s error=%u heap=%lu min=%lu",
              APP_FW_VERSION, (unsigned long)(m.uptime_ms / 86400000u),
              (unsigned long)((m.uptime_ms / 1000u) % 86400u), m.page,
              m.lcd_ready ? "READY" : "START/FAULT", m.lcd_error,
              (unsigned long)m.heap_free, (unsigned long)m.heap_min);
    board_log("[io] ldr=%u valid=%u exposed=%u joy=%u valid=%u stage=%u idle=%u",
              m.light_raw, m.light_valid, m.light_exposed, m.input_raw, m.input_valid,
              m.joy_stage,m.joy_idle);
    static const char *const sd_names[]={"WAIT","READY","LIMIT","ERROR","DISABLED"};
    board_log("[services] modem=%s sd=%s free=%lu/%lu MiB writes=%lu phone_only=%u tasks=%u",
              APP_MODEM_ENABLE ? "ENABLED" : "DISABLED",sd_names[m.sd_state],
              (unsigned long)m.sd_free_mib,(unsigned long)m.sd_total_mib,
              (unsigned long)m.sd_writes,APP_PHONE_ONLY,app_task_active_count());
    board_log("[health] stale=%02lx stack_low=%02lx event_drop=%lu log_drop=%lu rx_drop=%lu",
              (unsigned long)m.stale_mask, (unsigned long)m.stack_low_mask,
              (unsigned long)m.event_drops, (unsigned long)m.log_drops,
              (unsigned long)m.rx_drops);
    board_log("[rotation] mode=%s requested=%u actual=%u sensor=%s error=%u",
              m.rotation_auto ? "AUTO" : "LOCK", m.rotation*90u, m.lcd_rotation*90u,
              m.motion_valid ? "READY" : "UNAVAILABLE", m.motion_error);
    phone_datetime_t t;
    if(phone_clock_read(&m.phone.clock,m.uptime_ms,&t))
        board_log("[clock] Beijing %04u-%02u-%02u %02u:%02u:%02u source=%u",t.year,t.month,t.day,t.hour,t.minute,t.second,m.phone.clock.source);
    else board_log("[clock] unset; use clock YYYY-MM-DD HH:MM[:SS]");
    board_log("[phone] demo=%u network=%u registered=%u LTE=%u signal_valid=%u RSRP=%d bars=%u SMS=%u call=%u",
              m.phone.demo,m.phone.network.enabled,m.phone.network.registered,m.phone.network.lte,
              m.phone.network.signal_valid,m.phone.network.rsrp_dbm,phone_signal_bars(&m.phone.network),
              m.phone.message_count,m.phone.call.state);
}

static void print_motion(void)
{
    app_model_t m;
    app_model_snapshot(&m);
    board_log("[motion] valid=%u error=%u x=%d y=%d z=%d mg flat=%u settling=%u samples=%lu",
              m.motion_valid, m.motion_error, m.accel_x_mg, m.accel_y_mg, m.accel_z_mg,
              m.motion_flat, m.motion_pending, (unsigned long)m.motion_samples);
    board_log("[motion] screen axes: X=%d sign=%d Y=%d sign=%d Z=%d offset=%u deg; verify mounting on board",
              CFG_MOTION_X_AXIS, CFG_MOTION_X_SIGN, CFG_MOTION_Y_AXIS, CFG_MOTION_Y_SIGN,
              CFG_MOTION_Z_AXIS, CFG_MOTION_ROTATION_OFFSET*90u);
}

static void print_signal(void)
{
    static app_model_t m;app_model_snapshot(&m);const signal_state_t *s=&m.phone.signal;
    const signal_sample_t *v=&s->sample;
    board_log("[signal] state=%u request=%lu completed=%lu valid=%u errors=%u present=%u utc=%lu scroll=%u",
        s->state,(unsigned long)s->request_id,(unsigned long)s->completed_id,v->valid,v->errors,v->present,(unsigned long)v->utc,s->scroll);
    board_log("[signal] RSRP=%d SINR_raw=%d SINR_x2=%d RSRQ=%d RSSI=%d Band=%u Cell=0x%07lX",
        v->rsrp,v->sinr_raw,v->sinr_raw-47,v->rsrq,v->rssi,v->band,(unsigned long)v->cell_id);
    board_log("[signal] operator=%s",v->operator_name);
}
static void execute_line(const char *line)
{
    app_command_t command = app_command_parse(line);
    app_event_t event = {0};
    switch (command.kind) {
    case APP_CMD_HELP:
        board_log("help | status | tasks | page home/contact/sms/contacts/inbox/message/call/incoming/next");
        board_log("rotate auto | rotate 0 | rotate 90 | rotate 180 | rotate 270");
        board_log("clock YYYY-MM-DD HH:MM[:SS] (Beijing); host/modem clock only, no normal demo data");
        board_log("key up/down/left/right/select/back/home/focus; explicit diagnostic call/end/delete");
        board_log("navigation: down=bar, up=content; center=confirm; 5ms scan/20ms stable, release before next action");
        board_log("lcd profile gpio/fast/poll0..7/dma0..7 | lcd bench off/carousel/list/scroll/color (display fixtures)");
        board_log("joystick has fixed levels at boot; joy calibrate starts optional manual setup");
        board_log("page signal/signalinfo | signal refresh | signal status; manual snapshot, deferred during calls");
        board_log("page settings/settingsmenu/rotation/calibration | settings status | settings retry | joy cancel");
        break;
    case APP_CMD_STATUS: print_status(); break;
    case APP_CMD_TASKS: print_tasks(); break;
    case APP_CMD_SIGNAL_STATUS:print_signal();break;
    case APP_CMD_SETTINGS_STATUS: {
        static app_model_t m;app_model_snapshot(&m);const settings_state_t *s=&m.phone.system;
        board_log("[settings] current=%u desired=%u saved=%u state=%u seq=%lu revision=%lu saved_revision=%lu cal_id=%lu result=%u",
            m.rotation*90u,s->desired.rotation*90u,s->saved.rotation*90u,s->save_state,(unsigned long)s->sequence,
            (unsigned long)s->revision,(unsigned long)s->saved_revision,(unsigned long)s->cal_id,s->cal_result);
        board_log("[settings] manual=%u calibration_rotation=%u idle=%u up=%u down=%u left=%u right=%u center=%u",
            s->desired.manual,s->desired.calibration_rotation*90u,s->desired.idle,s->desired.levels[0],s->desired.levels[1],s->desired.levels[2],s->desired.levels[3],s->desired.levels[4]);
        break;
    }
    case APP_CMD_MOTION: print_motion(); break;
    case APP_CMD_JOY_CALIBRATE:
        board_log(app_input_restart_calibration()?"[cmd] joystick calibration requested":"[cmd] busy; retry");
        break;
    case APP_CMD_PHONE:
        event.kind=APP_EVENT_PHONE; event.value.phone=command.phone;
        board_log(app_event_post(&event) ? "[cmd] phone event queued" : "[cmd] busy; retry");
        break;
    case APP_CMD_ROTATE:
#if APP_LCD_DIAGNOSTIC
        board_log("[cmd] diagnostic display active; rebuild without --lcd-diagnostic");
        break;
#endif
        event.kind = APP_EVENT_ROTATION_MODE;
        event.value.rotation_mode.automatic = command.page == 4;
        event.value.rotation_mode.rotation = command.page & 3u;
        board_log(app_event_post(&event) ? "[cmd] rotation queued" : "[cmd] busy; retry");
        break;
    case APP_CMD_PAGE:
#if APP_LCD_DIAGNOSTIC
        board_log("[cmd] diagnostic display active; rebuild without --lcd-diagnostic");
        break;
#endif
        event.kind = APP_EVENT_PAGE;
        event.value.page = command.page;
        board_log(app_event_post(&event) ? "[cmd] page queued" : "[cmd] busy; retry");
        break;
    case APP_CMD_LCD_CONFIG:
    case APP_CMD_LCD_BENCH:
        event.kind=command.kind==APP_CMD_LCD_CONFIG?APP_EVENT_LCD_CONFIG:APP_EVENT_LCD_BENCH;
        event.value.page=command.page;
        board_log(app_event_post(&event)?"[cmd] LCD config queued":"[cmd] busy; retry");
        break;
    case APP_CMD_LCD_RETRY:
#if APP_LCD_DIAGNOSTIC
        board_log("[cmd] diagnostic display active; rebuild without --lcd-diagnostic");
        break;
#endif
        event.kind = APP_EVENT_LCD_RETRY;
        board_log(app_event_post(&event) ? "[cmd] LCD retry queued" : "[cmd] busy; retry");
        break;
    default: board_log("[cmd] unknown command; use: help"); break;
    }
}

void app_console_task(void *arg)
{
    (void)arg;
    app_line_t line = {0};
    int command_pending = 0;
    board_log("[console] 115200 8N1; type help then ENTER");
    for (;;) {
        app_task_beat(APP_TASK_CONSOLE);
        /* Consume the buffered input promptly. A completed command waits for
         * earlier output to drain, so a burst of `tasks` commands cannot fill
         * the log queue with their own replies before TX gets a turn. */
        if (!command_pending) {
            for (unsigned i = 0; i < 128u; ++i) {
                int c = board_console_getc();
                if (c == -1) break;
                int result = app_line_feed(&line, c);
                if (result < 0) board_log("[cmd] input lost/too long; line discarded");
                else if (result > 0) { command_pending = 1; break; }
            }
        }
        if (command_pending && uxQueueMessagesWaiting(s_logs) == 0u) {
            execute_line(line.text);
            command_pending = 0;
        }
        /* One record per iteration bounds the interval before RX is serviced.
         * The normal CLI commands emit at most eight records (tasks/status). */
        char record[LOG_LENGTH];
        if (xQueueReceive(s_logs, record, 0) == pdPASS) {
            board_console_write(record, strlen(record));
            board_console_write("\n", 1);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
