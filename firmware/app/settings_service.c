#include "settings_service.h"
#include "settings_store.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
static settings_store_t s_store;
static settings_config_t s_boot;
static uint8_t s_boot_ready;
static uint32_t s_writing_revision;
static struct {
    uint32_t next,id,revision;
    uint8_t cancel,result,stage,release;
    settings_config_t config;
} s_cal;
/* Only the input task accesses these fields. */
static struct {
    joy_input_t backup;
    uint32_t id,progress;
    uint8_t rotation,backup_rotation,stage,release;
} s_input;
static uint32_t next_id(uint32_t id) {return ++id?id:1u;}
void settings_boot_init(void)
{
    settings_store_scan(&s_store,&settings_flash_io);
    if(!settings_store_prepare(&s_store))s_store.error=1;
    s_boot=s_store.config;s_boot_ready=1;
}
void settings_model_load(app_model_t *m)
{
    if(s_boot_ready) {
        m->phone.system.desired=m->phone.system.saved=s_boot;
        m->phone.system.sequence=s_store.sequence;
        m->phone.system.save_state=s_store.error?SETTINGS_SAVE_FAILED:SETTINGS_SAVED;
        m->rotation=s_boot.rotation;m->rotation_auto=0;
        m->phone.signal_rotation=m->rotation;
        m->joy_stage=6;m->joy_idle=s_boot.idle;memcpy(m->joy_levels,s_boot.levels,sizeof m->joy_levels);
    }
}
static void load_joy(joy_input_t *j,const settings_config_t *c)
{
    joy_input_reset(j);j->idle=c->idle;memcpy(j->levels,c->levels,sizeof j->levels);
    j->stage=6;j->wait_release=1;j->fixed_levels=!c->manual;
}
void settings_input_init(joy_input_t *joy)
{
    settings_config_t c;if(s_boot_ready)c=s_boot;else settings_defaults(&c);
    load_joy(joy,&c);memset(&s_input,0,sizeof s_input);s_input.rotation=c.calibration_rotation;
}
static void publish_joy(const joy_input_t *j,uint8_t result,unsigned rotation)
{
    taskENTER_CRITICAL();s_cal.stage=j->stage;s_cal.release=j->wait_release;s_cal.result=result;
    s_cal.config.idle=j->idle;memcpy(s_cal.config.levels,j->levels,sizeof j->levels);
    s_cal.config.calibration_rotation=(uint8_t)rotation;s_cal.config.manual=!j->fixed_levels;
    s_cal.revision=next_id(s_cal.revision);taskEXIT_CRITICAL();
}
joy_dir_t settings_input_step(joy_input_t *joy,uint16_t raw,int valid,uint32_t now,unsigned rotation)
{
    uint32_t id;uint8_t cancel,result;
    taskENTER_CRITICAL();id=s_cal.id;cancel=s_cal.cancel;result=s_cal.result;taskEXIT_CRITICAL();
    if(id && id!=s_input.id && result==CAL_RUNNING) {
        s_input.backup=*joy;s_input.backup_rotation=s_input.rotation;s_input.id=id;
        s_input.rotation=(uint8_t)rotation;s_input.progress=now;joy_input_reset(joy);
        s_input.stage=s_input.release=255;publish_joy(joy,CAL_RUNNING,rotation);
    }
    if(id && id==s_input.id && result==CAL_RUNNING) {
        uint8_t outcome=cancel;
        if(!outcome && (uint32_t)(now-s_input.progress)>=60000u)outcome=CAL_TIMEOUT;
        if(!outcome) {
            (void)joy_input_feed(joy,raw,valid);
            if(joy->stage!=s_input.stage || joy->wait_release!=s_input.release) {
                s_input.stage=joy->stage;s_input.release=joy->wait_release;s_input.progress=now;
                publish_joy(joy,CAL_RUNNING,s_input.rotation);
            }
            if(joy->stage==6)outcome=CAL_SUCCESS;
        }
        if(outcome) {
            if(outcome!=CAL_SUCCESS) {*joy=s_input.backup;s_input.rotation=s_input.backup_rotation;}
            joy->wait_release=1;joy->held=joy->pending=JOY_NONE;joy->released=joy->debounce=0;
            publish_joy(joy,outcome,s_input.rotation);
        }
        return JOY_NONE;
    }
    if(id && id==s_input.id && result==CAL_SUCCESS && cancel) {
        *joy=s_input.backup;s_input.rotation=s_input.backup_rotation;joy->wait_release=1;
        joy->held=joy->pending=JOY_NONE;joy->released=joy->debounce=0;
        publish_joy(joy,cancel,s_input.rotation);return JOY_NONE;
    }
    /* Hold the release gate until the controller has acknowledged completion. */
    if(id)return JOY_NONE;
    s_input.id=0;
    return joy_screen_direction(joy_input_feed(joy,raw,valid),s_input.rotation,rotation);
}
static void request_cancel(uint8_t reason)
{
    taskENTER_CRITICAL();if(s_cal.id && (s_cal.result==CAL_RUNNING || s_cal.result==CAL_SUCCESS))s_cal.cancel=reason;taskEXIT_CRITICAL();
}
int settings_action(app_model_t *m,uint8_t action)
{
    settings_state_t *s=&m->phone.system;
    int host_start=action==PHONE_KEY_CAL_HOST_START;
    if(host_start)action=PHONE_KEY_CAL_START;
    if(action==PHONE_KEY_CAL_CANCEL) {request_cancel(CAL_CANCELLED);return 1;}
    if(m->phone.navigation_locked)return action==PHONE_KEY_CAL_START || action==PHONE_KEY_SETTINGS_ROTATE || action==PHONE_KEY_SETTINGS_RETRY;
    if(action==PHONE_KEY_SETTINGS_RETRY) {
        s->save_state=SETTINGS_PENDING;return 1;
    }
    if(action==PHONE_KEY_SETTINGS_ROTATE) {
        if(m->page!=APP_PAGE_ROTATION || m->phone.selected_row>3)return 1;
        m->rotation=m->phone.selected_row;m->rotation_auto=0;m->phone.signal_rotation=m->rotation;
        s->desired.rotation=m->rotation;s->revision=next_id(s->revision);s->save_state=SETTINGS_PENDING;return 1;
    }
    if(action!=PHONE_KEY_CAL_START)return 0;
    if(m->phone.pending || (m->phone.call.state!=PHONE_CALL_IDLE && m->phone.call.state!=PHONE_CALL_ENDED)) {
        s->cal_result=CAL_BUSY;return 1;
    }
    taskENTER_CRITICAL();
    if(s_cal.id) {taskEXIT_CRITICAL();return 1;}
    s_cal.next=next_id(s_cal.next);s_cal.id=s_cal.next;s_cal.cancel=0;s_cal.result=CAL_RUNNING;
    s_cal.stage=s_cal.release=0;s_cal.revision=next_id(s_cal.revision);
    s->cal_id=s_cal.id;taskEXIT_CRITICAL();
    s->cal_origin=!host_start && m->page==APP_PAGE_CAL_CONFIRM;s->cal_result=CAL_RUNNING;
    /* Replace the confirmation with the wizard; its parent stays in history. */
    if(s->cal_origin)m->page=APP_PAGE_JOYSTICK;
    else phone_navigate(&m->phone,&m->page,APP_PAGE_JOYSTICK);
    m->phone.navigation_locked=1;m->phone.focus_bar=m->phone.nav_selected=0;
    m->joy_stage=m->joy_wait_release=0;return 1;
}
void settings_service_apply(app_model_t *m,int voice_busy)
{
    settings_state_t *s=&m->phone.system;
    if(s->cal_id && voice_busy)request_cancel(CAL_INCOMING);
    uint32_t id,revision;uint8_t result,stage,release;settings_config_t c;
    taskENTER_CRITICAL();id=s_cal.id;revision=s_cal.revision;result=s_cal.result;stage=s_cal.stage;release=s_cal.release;c=s_cal.config;taskEXIT_CRITICAL();
    if(id && id==s->cal_id && revision!=s->cal_revision) {
        if(voice_busy && result==CAL_SUCCESS)return; /* Input owner must restore first. */
        if(result==CAL_SUCCESS) {
            c.rotation=s->desired.rotation;
            if(!settings_valid(&c)) {request_cancel(CAL_INVALID);return;}
        }
        s->cal_revision=revision;m->joy_stage=stage;m->joy_wait_release=release;
        m->joy_idle=c.idle;memcpy(m->joy_levels,c.levels,sizeof m->joy_levels);
        if(result!=CAL_RUNNING) {
            if(result==CAL_SUCCESS && !voice_busy) {
                c.rotation=s->desired.rotation;
                if(settings_valid(&c)) {s->desired=c;s->revision=next_id(s->revision);s->save_state=SETTINGS_PENDING;}
                else result=CAL_INVALID;
            }
            if(voice_busy)result=CAL_INCOMING;
            s->cal_result=result;s->cal_id=0;m->phone.navigation_locked=0;
            if(m->page==APP_PAGE_JOYSTICK) {
                if(s->cal_origin)phone_resolve_key(&m->phone,&m->page,PHONE_KEY_BACK);
                else phone_navigate(&m->phone,&m->page,APP_PAGE_HOME);
            }
            taskENTER_CRITICAL();if(s_cal.id==id)s_cal.id=0;taskEXIT_CRITICAL();
        }
    }
    m->phone.signal_rotation=m->rotation;
    if(s->layout_rotation!=m->rotation) {
        s->layout_rotation=m->rotation;
        unsigned selected=m->phone.selected_message;
        unsigned lines=selected<m->phone.message_count?phone_wrapped_lines(m->phone.inbox[selected].text,phone_message_units(m->rotation)):0;
        unsigned visible=phone_message_visible(m->rotation),limit=lines>visible?lines-visible:0;
        if(m->phone.message_scroll>limit)m->phone.message_scroll=(uint8_t)limit;
    }
    if(voice_busy || !s_boot_ready)return;
    if(s_store.busy) {
        int rc=settings_store_step(&s_store);
        if(rc<0) {s->save_state=SETTINGS_SAVE_FAILED;return;}
        if(rc>0) {
            s->saved=s_store.config;s->sequence=s_store.sequence;s->saved_revision=s_writing_revision;
            s->save_state=settings_equal(&s->desired,&s->saved)?SETTINGS_SAVED:SETTINGS_PENDING;
        }
    } else if(s->save_state==SETTINGS_PENDING) {
        int rc=settings_store_begin(&s_store,&s->desired);s_writing_revision=s->revision;
        if(rc<0)s->save_state=rc==-2?SETTINGS_FULL:SETTINGS_SAVE_FAILED;
        else if(rc>0) {s->saved=s_store.config;s->saved_revision=s->revision;s->save_state=SETTINGS_SAVED;}
        else s->save_state=SETTINGS_WRITING;
    }
}
