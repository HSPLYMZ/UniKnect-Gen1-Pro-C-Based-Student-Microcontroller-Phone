#include "app_runtime.h"
#include "app_config.h"
#include "adc_ldr.h"
#include "adc_bus.h"
#include "board_pins.h"
#include "joystick.h"
#include "joy_input.h"
#include "settings_service.h"
#include "board.h"
#include "lcd_st7735.h"
#include "ui_display.h"
uint32_t ui_clock_ms(void) {return (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;}
#include <string.h>
#if APP_LCD_DIAGNOSTIC
#include "lcd_probe.h"
#endif

void app_light_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        app_event_t event = {.kind = APP_EVENT_LIGHT};
        if (adc_ldr_read_checked(&event.value.raw) != 0) event.kind = APP_EVENT_LIGHT_FAULT;
        (void)app_event_post(&event);
        app_task_beat(APP_TASK_LIGHT);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CFG_LIGHT_POLL_MS));
    }
}

int app_input_restart_calibration(void)
{
    app_event_t e={.kind=APP_EVENT_PHONE};e.value.phone.kind=PHONE_EVENT_ACTION;
    e.value.phone.source=PHONE_SOURCE_HOST;e.value.phone.value.key=PHONE_KEY_CAL_HOST_START;
    return app_event_post(&e);
}

void app_input_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    unsigned polls = 0;
    joy_input_t joy;
    settings_input_init(&joy);
    uint8_t last_stage=255, last_release=255;
    for (;;) {
        uint16_t raw=0;
        int valid=adc_bus_read_checked(BUTTON_ADC_CHANNEL,&raw)==0;
        uint8_t rotation=app_rotation_snapshot();
        joy_dir_t direction=settings_input_step(&joy,raw,valid,(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,rotation);
        if(direction!=JOY_NONE) {
            app_event_t key={.kind=APP_EVENT_PHONE};
            key.value.phone.kind=PHONE_EVENT_KEY;
            key.value.phone.source=PHONE_SOURCE_UI;
            static const uint8_t keys[]={0,PHONE_KEY_UP,PHONE_KEY_DOWN,PHONE_KEY_LEFT,
                                          PHONE_KEY_RIGHT,PHONE_KEY_SELECT};
            key.value.phone.value.key=keys[direction];
            (void)app_event_post(&key);
        }
        if(joy.stage!=last_stage || joy.wait_release!=last_release || polls%(200u/CFG_JOY_SCAN_MS)==0u) {
            app_event_t event={.kind=APP_EVENT_JOY};
            event.value.joy.idle=joy.idle;
            memcpy(event.value.joy.levels,joy.levels,sizeof joy.levels);
            event.value.joy.stage=joy.stage;
            event.value.joy.wait_release=joy.wait_release;
            (void)app_event_post(&event);
            last_stage=joy.stage;last_release=joy.wait_release;
        }
        /* Nonblocking 5 ms ladder scan; publish diagnostic ADC at 5 Hz. */
        if ((polls++ % (200u/CFG_JOY_SCAN_MS)) == 0u) {
            app_event_t event = {.kind = APP_EVENT_INPUT};
            event.value.raw = raw;
            if (!valid) event.kind = APP_EVENT_INPUT_FAULT;
            (void)app_event_post(&event);
        }
        app_task_beat(APP_TASK_INPUT);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CFG_JOY_SCAN_MS));
    }
}

#if APP_LCD_DIAGNOSTIC
void lcd_probe_progress(int ready)
{
    static unsigned calls;
    static int previous_ready = -1;
    app_task_beat(APP_TASK_DISPLAY);
    if ((calls++ % 25u) == 0u || ready != previous_ready) {
        app_event_t event = {.kind = APP_EVENT_LCD};
        event.value.lcd.ready = ready && lcd_get_error() == LCD_ERROR_NONE;
        event.value.lcd.error = (uint8_t)lcd_get_error();
        (void)app_event_post(&event);
    }
    previous_ready = ready;
}

void app_display_task(void *arg) { lcd_probe_run(arg); }
#else
void app_display_task(void *arg)
{
    (void)arg;
    static ui_frame_t desired;
    static app_model_t model;
    static app_task_info_t tasks[APP_TASK_COUNT];
    static phone_state_t last_phone;
    uint32_t generation = 0, cells = 0;
    int reinitialize = 1;
    int last_rotation = -1, last_page = -1;
    uint64_t last_clock_second = UINT64_MAX;
    uint32_t last_call_seconds = UINT32_MAX, last_diagnostic_bucket = UINT32_MAX;
    unsigned publish_count = 0;
    static ui_animation_t animation;
    static phone_state_t bench_phone;
    uint8_t last_bench=255;
    uint8_t last_color=255;
    uint32_t bench_start=0, window_start=0, completed=0, max_gap=0;
    uint32_t max_compose=0,max_send=0,previous_frame=0,max_graph=0,max_convert=0;
    lcd_stats_t window_stats={0};
    static uint32_t frame_times[256]; /* 5 seconds at up to 50 completed FPS */
    unsigned frame_head=0,frame_count=0;
    uint32_t rolling_start=0,rolling_min=UINT32_MAX;
    TickType_t wake=xTaskGetTickCount();
    int animating=0;
    for (;;) {
        app_task_beat(APP_TASK_DISPLAY);
        app_model_snapshot(&model);
        if (reinitialize || model.lcd_retry_generation != generation) {
            generation = model.lcd_retry_generation;
            reinitialize = 0;
            unsigned profile=model.lcd_profile;
            lcd_set_transport(profile<2u);
            lcd_set_gpio_delay(profile==1u?0u:12u);
            lcd_set_dma_enabled(profile>=10u);
            if(profile>=2u) lcd_set_spi_div(7u-(profile-2u)%8u);
            board_log("[display] profile=%u: %s",profile,profile==0u?"verified GPIO":profile==1u?"fast GPIO (visual check required)":profile<10u?"SPI polling (visual check required)":"SPI DMA (visual check required)");
            lcd_init();
            ui_cache_reset(); /* lcd_init completed a black framebuffer flush. */
            last_rotation = -1;
            animation.valid=0; animating=0;
            wake=xTaskGetTickCount(); window_start=(uint32_t)wake;
            completed=previous_frame=max_gap=max_compose=max_send=0;
            frame_head=frame_count=0; rolling_start=0; rolling_min=UINT32_MAX;
            window_stats=lcd_stats();
            app_task_beat(APP_TASK_DISPLAY);
            publish_count = 0;
            app_model_snapshot(&model); /* initialization can take multiple seconds */
        }
        uint32_t now_ms=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
        if(last_bench!=model.lcd_bench) {
            if(last_bench==4u) last_rotation=-1;
            last_bench=model.lcd_bench; bench_start=now_ms;
            last_color=255;
            phone_state_init(&bench_phone);
            uint8_t bench_page=APP_PAGE_HOME;
            phone_event_t demo={.kind=PHONE_EVENT_DEMO,.value.demo=1};
            (void)phone_apply(&bench_phone,&bench_page,model.rotation,&demo,model.uptime_ms);
            bench_phone.inbox[3]=bench_phone.inbox[2];
            bench_phone.inbox[3].id=4;bench_phone.message_count=4;
            bench_phone.signal.sample=(signal_sample_t){.valid=127,.present=1,.registered=1,.lte=1,
                .rsrp=-79,.rsrq=-4,.rssi=-56,.sinr_raw=116,.band=40,.cell_id=0x6097c02,
                .utc=bench_phone.clock.utc};
            strcpy(bench_phone.signal.sample.operator_name,"中国广电");
            bench_phone.signal.state=SIGNAL_DONE;
            animation.valid=0; window_start=now_ms;
            completed=previous_frame=max_gap=max_compose=max_send=0;
            frame_head=frame_count=0; rolling_start=0; rolling_min=UINT32_MAX;
            window_stats=lcd_stats();
        }
        if(model.lcd_bench && (model.lcd_bench<4u || model.lcd_bench==5u || model.lcd_bench==6u)) {
            model.phone=bench_phone;
            uint32_t step=(now_ms-bench_start)/128u;
            if(model.lcd_bench==1u) {
                unsigned pos=((now_ms-bench_start)/256u)%(PHONE_TOP_COUNT*2u);
                unsigned position=pos<=PHONE_TOP_COUNT?pos:PHONE_TOP_COUNT*2u-pos;
                model.phone.top_position=(int16_t)position;
                model.phone.top_page=(uint8_t)(position%PHONE_TOP_COUNT);
                model.page=phone_top_page(position);
            } else if(model.lcd_bench==6u) {
                model.page=APP_PAGE_ROTATION;
                unsigned pos=step%6u;model.phone.selected_row=(uint8_t)(pos<=3u?pos:6u-pos);
            } else if(model.lcd_bench==2u) {
                model.page=APP_PAGE_INBOX;
                unsigned pos=step%6u;model.phone.selected_message=pos<=3u?pos:6u-pos;
                model.phone.list_scroll=model.phone.selected_message>=3u?1u:0u;
            } else {
                if(model.lcd_bench==5u) {
                    model.page=APP_PAGE_SIGNAL_INFO;
                    unsigned lines=signal_lines(&model.phone.signal.sample,phone_message_units(model.rotation));
                    unsigned visible=signal_visible(model.rotation),limit=lines>visible?lines-visible:0;
                    unsigned pos=limit?step%(limit*2u):0;
                    model.phone.signal.scroll=(uint8_t)(pos<=limit?pos:limit*2u-pos);
                } else {
                model.page=APP_PAGE_MESSAGE;
                unsigned total=phone_wrapped_lines(model.phone.inbox[0].text,phone_message_units(model.rotation));
                unsigned visible=phone_message_visible(model.rotation), limit=total>visible?total-visible:0;
                unsigned pos=limit?step%(limit*2u):0;
                model.phone.message_scroll=(uint8_t)(pos<=limit?pos:limit*2u-pos);
                }
            }
        }
        if (lcd_get_error() == LCD_ERROR_NONE) {
            int rotation_changed = last_rotation != model.rotation;
            int page_changed = last_page != model.page;
            int changed = rotation_changed || page_changed;
            uint64_t second = UINT64_MAX;
            if (model.phone.clock.valid && model.uptime_ms >= model.phone.clock.anchor_ms)
                second = ((uint64_t)model.phone.clock.utc +
                          (model.uptime_ms - model.phone.clock.anchor_ms) / 1000u + 28800u);
            uint32_t call_seconds = model.phone.call.state == PHONE_CALL_ACTIVE ?
                phone_call_seconds(&model.phone, model.uptime_ms) : UINT32_MAX;
            uint32_t diagnostic_bucket = model.page == APP_PAGE_IO || model.page == APP_PAGE_JOYSTICK ?
                (uint32_t)(model.uptime_ms / 200u) :
                model.page <= APP_PAGE_TASKS ? (uint32_t)(model.uptime_ms / 1000u) : UINT32_MAX;
            int compose = animating || model.lcd_bench || changed || second != last_clock_second ||
                call_seconds != last_call_seconds || diagnostic_bucket != last_diagnostic_bucket ||
                memcmp(&last_phone, &model.phone, sizeof last_phone) != 0;
            if (rotation_changed) (void)lcd_set_rotation(model.rotation);
            if (compose) {
                app_task_snapshot(tasks);
                uint32_t begin=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
                animating=ui_compose_animated(&desired,&model,tasks,&animation,begin);
                uint32_t duration=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS-begin;
                uint32_t graph_ms,convert_ms;ui_compose_cost(&graph_ms,&convert_ms);
                if(graph_ms>max_graph) max_graph=graph_ms;
                if(convert_ms>max_convert) max_convert=convert_ms;
                if(duration>max_compose) max_compose=duration;
                last_phone = model.phone;
                last_clock_second = second;
                last_call_seconds = call_seconds;
                last_diagnostic_bucket = diagnostic_bucket;
            }
            /* Rotation invalidates the physical framebuffer. A page change
             * at the same angle can reuse the last displayed cells and send
             * only changed rectangles, draining them in this iteration. */
            TickType_t refresh_start = xTaskGetTickCount();
            int result;
            if(model.lcd_bench==4u) {
                static const uint16_t colors[]={LCD_RED,LCD_GREEN,LCD_BLUE,LCD_WHITE,LCD_BLACK};
                uint8_t color=(uint8_t)(((now_ms-bench_start)/1000u)%5u);
                result=0;
                if(color!=last_color || changed) {
                    lcd_fill(colors[color]); lcd_flush(); last_color=color;
                    result=lcd_get_error()==LCD_ERROR_NONE?1:-1;
                }
            } else result = rotation_changed ? ui_render_full(&desired) :
                ui_flush_step(&desired, page_changed ?
                    (unsigned)desired.cols * desired.rows : CFG_DISPLAY_CELL_BUDGET);
            uint32_t finish=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
            uint32_t send_ms=finish-(uint32_t)refresh_start*portTICK_PERIOD_MS;
            if(send_ms>max_send) max_send=send_ms;
            if (result > 0) {
                cells += (uint32_t)result;
                /* Adjacent carousel pages are animation frames too; exclude
                 * rotation and ordinary whole-page loads from interaction FPS. */
                int carousel_frame=phone_top_index((uint8_t)last_page)>=0 && phone_top_index(model.page)>=0;
                if(!rotation_changed && (!page_changed || carousel_frame)) {
                    ++completed;
                    if(!rolling_start) rolling_start=finish;
                    while(frame_count && finish-frame_times[frame_head]>=5000u) {
                        frame_head=(frame_head+1u)%256u; --frame_count;
                    }
                    if(frame_count<256u) {
                        frame_times[(frame_head+frame_count)%256u]=finish; ++frame_count;
                    }
                    /* The least populated sliding window can end just before
                     * this completion. Exclude the newly arrived frame so
                     * boundary accounting never inflates the lower bound. */
                    uint32_t rolling_rate=(frame_count-1u)*2u;
                    if(finish-rolling_start>=5000u && rolling_rate<rolling_min) rolling_min=rolling_rate;
                    if(previous_frame && finish-previous_frame>max_gap) max_gap=finish-previous_frame;
                    previous_frame=finish;
                }
            }
            if (changed && result >= 0)
                board_log("[display] page=%u rotation=%u cells=%d refresh=%lu ms",
                          model.page,model.rotation,result,
                          (unsigned long)((TickType_t)(xTaskGetTickCount()-refresh_start)*portTICK_PERIOD_MS));
            if (result >= 0) {
                last_rotation = model.rotation;
                last_page = model.page;
                if (changed) publish_count = 0;
            }
        }
        int error = lcd_get_error();
        if (publish_count++ % 10u == 0u || error != model.lcd_error) {
            app_event_t event = {.kind = APP_EVENT_LCD};
            event.value.lcd.ready = error == LCD_ERROR_NONE;
            event.value.lcd.error = (uint8_t)error;
            event.value.lcd.rotation = (uint8_t)lcd_get_rotation();
            event.value.lcd.flushed_cells = cells;
            (void)app_event_post(&event);
            if (error && error != model.lcd_error)
                board_log("[display] transport error=%d dma_flags=%08lx; use: lcd retry", error,(unsigned long)lcd_stats().dma_flags);
        }
        app_task_beat(APP_TASK_DISPLAY);
        uint32_t stamp=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
        if(stamp-window_start>=5000u) {
            if(model.lcd_bench) board_log("[uicost] graph=%lu convert=%lu",(unsigned long)max_graph,(unsigned long)max_convert);
            max_graph=max_convert=0;
            lcd_stats_t stats=lcd_stats();
            uint32_t elapsed=stamp-window_start;
            if(completed || model.lcd_bench)
                board_log("[fps] bench=%u rotation=%u profile=%u frames=%lu window=%lu fps10=%lu rolling10=%lu gap=%lu compose=%lu send=%lu pixels=%lu tx=%lu timeout=%lu dmaerr=%lu",
                    model.lcd_bench,model.rotation,model.lcd_profile,(unsigned long)completed,(unsigned long)elapsed,
                    (unsigned long)(completed*10000u/elapsed),(unsigned long)(rolling_min==UINT32_MAX?0:rolling_min),(unsigned long)max_gap,
                    (unsigned long)max_compose,(unsigned long)max_send,
                    (unsigned long)(stats.pixels-window_stats.pixels),(unsigned long)(stats.transfers-window_stats.transfers),
                    (unsigned long)stats.timeouts,(unsigned long)stats.dma_errors);
            completed=max_gap=max_compose=max_send=0; rolling_min=UINT32_MAX;
            window_start=stamp; window_stats=stats;
        }
        /* Include render/transfer cost in the period; drop missed deadlines. */
        TickType_t current=xTaskGetTickCount(),period=pdMS_TO_TICKS(CFG_DISPLAY_MS);
        if((TickType_t)(current-wake)>=period) {
            /* The frame has already used its period. Continue with current
             * state instead of adding a second full period of idle time. */
            wake=current; taskYIELD();
        } else vTaskDelayUntil(&wake,period);
    }
}
#endif

void app_health_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    uint32_t previous_idle = app_idle_counter();
    unsigned seconds = 0;
    uint32_t previous_fault = 0;
    for (;;) {
        app_task_beat(APP_TASK_HEALTH);
        app_task_measure_stacks();
        app_task_info_t tasks[APP_TASK_COUNT];
        app_task_snapshot(tasks);
        app_event_t event = {.kind = APP_EVENT_HEALTH};
        for (unsigned i = 0; i < APP_TASK_COUNT; ++i) {
            if(!tasks[i].active) continue;
            if (tasks[i].stale) event.value.health.stale_mask |= 1u << i;
            if (tasks[i].stack_free_words < 64u) event.value.health.stack_low_mask |= 1u << i;
        }
        event.value.health.heap_free = (uint32_t)xPortGetFreeHeapSize();
        event.value.health.heap_min = (uint32_t)xPortGetMinimumEverFreeHeapSize();
        event.value.health.event_drops = app_event_drops();
        event.value.health.log_drops = app_log_drops();
        event.value.health.rx_drops = board_console_rx_drops();
        uint32_t idle = app_idle_counter();
        event.value.health.idle_loops = idle - previous_idle;
        previous_idle = idle;
        (void)app_event_post(&event);
        uint32_t faults = event.value.health.stale_mask | event.value.health.stack_low_mask;
        board_led_set(BOARD_LED_RED, faults != 0u);
        board_led_set(BOARD_LED_GREEN, seconds & 1u);
        board_led_set(BOARD_LED_BLUE, !faults);
        if (seconds++ % 10u == 0u || faults != previous_fault) {
            board_log("[health] heap=%lu min=%lu stale=%02lx stack_low=%02lx event_drop=%lu log_drop=%lu rx_drop=%lu",
                      (unsigned long)event.value.health.heap_free,
                      (unsigned long)event.value.health.heap_min,
                      (unsigned long)event.value.health.stale_mask,
                      (unsigned long)event.value.health.stack_low_mask,
                      (unsigned long)event.value.health.event_drops,
                      (unsigned long)event.value.health.log_drops,
                      (unsigned long)event.value.health.rx_drops);
        }
        previous_fault = faults;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
    }
}
