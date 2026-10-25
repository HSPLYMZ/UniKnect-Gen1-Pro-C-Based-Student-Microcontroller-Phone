/* Project-owned component and task registries. No UniRTOS code is linked. */
#include "app_runtime.h"
#include "app_config.h"
#include "adc_bus.h"
#include "adc_ldr.h"
#include "joystick.h"
#include "sensor_i2c.h"
#include "board.h"
#include "voice_service.h"
#include "signal_service.h"
#include "settings_service.h"
#include "queue.h"
#include <string.h>

static StaticQueue_t s_event_queue_cb;
static uint8_t s_event_storage[CFG_QUEUE_LIMIT * sizeof(app_event_t)];
static QueueHandle_t s_events;
static uint32_t s_event_drops;
static app_model_t s_published;

#define TASK_MEMORY(key, words) static StaticTask_t key##_tcb; static StackType_t key##_stack[words]
TASK_MEMORY(control, 1024);
TASK_MEMORY(light, 384);
TASK_MEMORY(input, 768);
TASK_MEMORY(display, 1024);
TASK_MEMORY(console, 1536);
TASK_MEMORY(health, 768);
TASK_MEMORY(motion, 768);
TASK_MEMORY(storage, 1024);
TASK_MEMORY(voice, 1536);
typedef struct {
    const char *name;
    TaskFunction_t entry;
    StaticTask_t *tcb;
    StackType_t *stack;
    uint16_t words;
    uint8_t priority;
    uint32_t deadline_ms;
    uint8_t active;
} task_spec_t;
#define TASK(key, function, prio, deadline) \
    {#key, function, &key##_tcb, key##_stack, sizeof key##_stack / sizeof(StackType_t), prio, deadline, 1}
#define OPTIONAL_TASK(key, function, prio, deadline) \
    {#key, function, &key##_tcb, key##_stack, sizeof key##_stack / sizeof(StackType_t), prio, deadline, !APP_PHONE_ONLY}
static const task_spec_t s_tasks[] = {
    TASK(control, app_control_task, 4, 1000),
    OPTIONAL_TASK(light, app_light_task, 3, 500),
    TASK(input, app_input_task, 3, 500),
    TASK(display, app_display_task, 3, 4000),
    /* Console must preempt a full GPIO LCD flush to drain the UART RX ring. */
    TASK(console, app_console_task, 3, 1500),
    TASK(health, app_health_task, 5, 2500),
    OPTIONAL_TASK(motion, app_motion_task, 3, 2500),
    OPTIONAL_TASK(storage, app_storage_task, 2, 40000),
    TASK(voice, app_voice_task, 4, 30000),
};
_Static_assert(sizeof s_tasks / sizeof s_tasks[0] == APP_TASK_COUNT, "Task registry mismatch");
unsigned app_task_active_count(void)
{
    unsigned count=0;
    for(unsigned i=0;i<APP_TASK_COUNT;++i) count+=s_tasks[i].active!=0;
    return count;
}
static TaskHandle_t s_handles[APP_TASK_COUNT];
static TickType_t s_last_beat[APP_TASK_COUNT];
static uint32_t s_beats[APP_TASK_COUNT];
static uint8_t s_grace_expired;
static uint16_t s_stack_free_words[APP_TASK_COUNT];
static uint8_t s_stack_sampled;

static void event_bus_init(void)
{
    s_events = xQueueCreateStatic(CFG_QUEUE_LIMIT, sizeof(app_event_t),
                                 s_event_storage, &s_event_queue_cb);
    configASSERT(s_events);
    vQueueAddToRegistry(s_events, "app-events");
    app_model_init(&s_published);
    settings_model_load(&s_published);
}

static void sensors_init(void)
{
    adc_bus_init();
    joy_init();
    /* Both GPIOC analogue channels are configured before tasks run. */
    if(!APP_PHONE_ONLY) { adc_ldr_init(); sensor_i2c_init(); }
}

void app_framework_start(void)
{
    /* Explicit registration keeps dependency order reviewable without linker magic. */
    static const struct { const char *name; void (*init)(void); } components[] = {
        {"event-bus", event_bus_init},
        {"console", app_console_init},
        {"sensors", sensors_init},
        {"voice-queue", voice_service_init},
        {"signal-mailbox", signal_service_init},
    };
    for (unsigned i = 0; i < sizeof components / sizeof components[0]; ++i) {
        components[i].init();
        board_log("[init] %s ready", components[i].name);
    }
    for (unsigned i = 0; i < APP_TASK_COUNT; ++i) {
        const task_spec_t *s = &s_tasks[i];
        if(!s->active) continue;
        s_handles[i] = xTaskCreateStatic(s->entry, s->name, s->words, NULL,
                                       s->priority, s->stack, s->tcb);
        configASSERT(s_handles[i]);
    }
}

int app_event_post(const app_event_t *e)
{
    if (e && s_events && xQueueSend(s_events, e, 0) == pdPASS) return 1;
    taskENTER_CRITICAL();
    ++s_event_drops;
    taskEXIT_CRITICAL();
    return 0;
}

uint32_t app_event_drops(void)
{
    uint32_t value;
    taskENTER_CRITICAL(); value = s_event_drops; taskEXIT_CRITICAL();
    return value;
}

void app_model_snapshot(app_model_t *m)
{
    taskENTER_CRITICAL(); *m = s_published; taskEXIT_CRITICAL();
}

uint8_t app_rotation_snapshot(void)
{
    uint8_t rotation;
    taskENTER_CRITICAL(); rotation=s_published.rotation; taskEXIT_CRITICAL();
    return rotation;
}

void app_task_beat(unsigned id)
{
    configASSERT(id < APP_TASK_COUNT);
    taskENTER_CRITICAL();
    s_last_beat[id] = xTaskGetTickCount();
    ++s_beats[id];
    taskEXIT_CRITICAL();
}

void app_task_measure_stacks(void)
{
    uint16_t measured[APP_TASK_COUNT];
    for (unsigned i = 0; i < APP_TASK_COUNT; ++i)
        measured[i] = s_handles[i] ? (uint16_t)uxTaskGetStackHighWaterMark(s_handles[i]) : 0;
    taskENTER_CRITICAL();
    memcpy(s_stack_free_words, measured, sizeof measured);
    s_stack_sampled = 1;
    taskEXIT_CRITICAL();
}

void app_task_snapshot(app_task_info_t out[APP_TASK_COUNT])
{
    /* The first caller gets valid data even before the health task's first
     * sample; subsequent display/CLI snapshots use the 1 Hz cached values. */
    if (!s_stack_sampled) app_task_measure_stacks();
    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL();
    if (now >= pdMS_TO_TICKS(8000)) s_grace_expired = 1;
    uint8_t grace_expired = s_grace_expired;
    taskEXIT_CRITICAL();
    for (unsigned i = 0; i < APP_TASK_COUNT; ++i) {
        const task_spec_t *s = &s_tasks[i];
        TickType_t last;
        taskENTER_CRITICAL();
        now = xTaskGetTickCount();
        last = s_last_beat[i];
        out[i].beats = s_beats[i];
        out[i].stack_free_words = s_stack_free_words[i];
        taskEXIT_CRITICAL();
        out[i].name = s->name;
        out[i].active=s->active;
        out[i].priority = s->priority;
        out[i].stack_words = s->words;
        out[i].age_ms = (uint32_t)((now - last) * portTICK_PERIOD_MS);
        /* Grace includes the panel's power-up delay and initial black flush. */
        out[i].stale = s->active && grace_expired &&
                      app_heartbeat_late((uint32_t)now, (uint32_t)last,
                                         pdMS_TO_TICKS(s->deadline_ms));
    }
}

void app_control_task(void *arg)
{
    (void)arg;
    static app_model_t model;
    app_model_init(&model);settings_model_load(&model);
    uint32_t voice_revision=0;
    TickType_t previous = xTaskGetTickCount();
    for (;;) {
        app_event_t e;
        int received=xQueueReceive(s_events, &e, pdMS_TO_TICKS(100)) == pdPASS;
        TickType_t now = xTaskGetTickCount();
        model.uptime_ms += (uint64_t)(TickType_t)(now - previous) * portTICK_PERIOD_MS;
        previous = now;
        /* Reliable voice state/results precede a new ordinary input event. */
        voice_service_apply(&model.phone,&model.page,model.rotation,model.uptime_ms,&voice_revision);
        int handled=0;
        if(received && model.lcd_bench && e.kind==APP_EVENT_PHONE &&
           e.value.phone.kind==PHONE_EVENT_KEY && e.value.phone.source==PHONE_SOURCE_UI) handled=1;
        if(received && !handled && e.kind==APP_EVENT_PHONE && e.value.phone.kind==PHONE_EVENT_KEY) {
            uint8_t key=phone_route_key(&model.phone,&model.page,e.value.phone.value.key,e.value.phone.source);
            if(!key) handled=1;
            else { e.value.phone.kind=PHONE_EVENT_ACTION; e.value.phone.value.key=key; }
        }
        if(received && !handled && e.kind==APP_EVENT_PHONE && e.value.phone.kind==PHONE_EVENT_ACTION &&
           (e.value.phone.source==PHONE_SOURCE_UI || e.value.phone.source==PHONE_SOURCE_HOST))
            handled=settings_action(&model,e.value.phone.value.key);
#if APP_MODEM_ENABLE && !APP_LCD_DIAGNOSTIC
        if(received && !handled && e.kind==APP_EVENT_PHONE && e.value.phone.kind==PHONE_EVENT_ACTION &&
           (e.value.phone.source==PHONE_SOURCE_UI || e.value.phone.source==PHONE_SOURCE_HOST)) {
            phone_intent_t intent;
            int decision=phone_intent_from_key(&model.phone,model.page,e.value.phone.value.key,&intent);
            if(decision) {
                handled=1;
                if(decision>0) {
                    uint32_t request_id=voice_service_submit(&intent);
                    if(request_id) {
                        model.phone.pending=1;
                        model.phone.pending_id=request_id;
                        model.phone.pending_kind=intent.kind;
                        model.phone.note=PHONE_NOTE_NONE;
                        if(intent.kind==PHONE_INTENT_DIAL)
                            phone_navigate(&model.phone,&model.page,APP_PAGE_CALL);
                    } else model.phone.note=PHONE_NOTE_BUSY;
                }
            }
        }
#endif
        if (received && !handled) (void)app_model_apply(&model,&e);
        settings_service_apply(&model,voice_service_busy());
        signal_service_apply(&model.phone,model.uptime_ms,model.rotation);
        phone_tick(&model.phone,model.uptime_ms);
        taskENTER_CRITICAL(); s_published = model; taskEXIT_CRITICAL();
        app_task_beat(APP_TASK_CONTROL);
    }
}
