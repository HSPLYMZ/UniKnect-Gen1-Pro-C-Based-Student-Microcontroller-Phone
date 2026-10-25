#include "signal_service.h"
#include "app_config.h"
#include "voice_service.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* One reserved request, coalesced wakeups, and a retained terminal result. */
static struct {
    signal_sample_t sample;
    uint32_t next,id,revision;
    uint8_t state;
} s_signal;
static void (*s_signal_wake)(void);
static uint32_t signal_next(uint32_t n) {return ++n?n:1u;}
void signal_service_init(void) {memset(&s_signal,0,sizeof s_signal);s_signal_wake=NULL;}
void signal_service_set_waker(void (*wake)(void))
{
    taskENTER_CRITICAL();s_signal_wake=wake;int pending=s_signal.id!=0;taskEXIT_CRITICAL();
    if(pending && wake)wake();
}
uint32_t signal_service_request(void)
{
    taskENTER_CRITICAL();
    if(!s_signal.id) {
        s_signal.next=signal_next(s_signal.next);s_signal.id=s_signal.next;
        s_signal.state=SIGNAL_QUEUED;s_signal.revision=signal_next(s_signal.revision);
    }
    uint32_t id=s_signal.id;void (*wake)(void)=s_signal_wake;
    taskEXIT_CRITICAL();if(wake)wake();return id;
}
int signal_service_pending(void)
{
    taskENTER_CRITICAL();int pending=s_signal.id && s_signal.state<SIGNAL_DONE;taskEXIT_CRITICAL();return pending;
}
static void signal_publish(uint8_t state,const signal_sample_t *sample)
{
    taskENTER_CRITICAL();
    if(s_signal.state!=state || sample) {
        if(sample)s_signal.sample=*sample;
        s_signal.state=state;s_signal.revision=signal_next(s_signal.revision);
    }
    taskEXIT_CRITICAL();
}
void signal_service_apply(phone_state_t *p,uint64_t now,unsigned rotation)
{
    signal_sample_t sample;uint32_t id;uint8_t state;
    taskENTER_CRITICAL();
    id=s_signal.id;state=s_signal.state;
    int changed=p->signal.delivery_revision!=s_signal.revision;
    if(changed) {p->signal.delivery_revision=s_signal.revision;sample=s_signal.sample;}
    taskEXIT_CRITICAL();
    if(changed && id && id==p->signal.request_id) {
        p->signal.state=state;
        if(state>=SIGNAL_DONE) {
            sample.updated_ms=now;sample.utc=0;
            if(p->clock.valid && now>=p->clock.anchor_ms) {
                uint64_t utc=(uint64_t)p->clock.utc+(now-p->clock.anchor_ms)/1000u;
                if(utc<=UINT32_MAX)sample.utc=(uint32_t)utc;
            }
            p->signal.sample=sample;p->signal.completed_id=id;p->signal.request_id=0;
            unsigned lines=signal_lines(&sample,phone_message_units(rotation)),visible=signal_visible(rotation);
            if(p->signal.scroll>(lines>visible?lines-visible:0))p->signal.scroll=(uint8_t)(lines>visible?lines-visible:0);
        }
    }
    if(changed && id && state>=SIGNAL_DONE) {
        taskENTER_CRITICAL();
        if(s_signal.id==id && s_signal.state>=SIGNAL_DONE) {
            s_signal.id=0;s_signal.revision=signal_next(s_signal.revision);
        }
        taskEXIT_CRITICAL();
    }
    if(p->signal.refresh_requested) {
        p->signal.refresh_requested=0;
        if(!p->signal.request_id) {p->signal.request_id=signal_service_request();p->signal.state=SIGNAL_QUEUED;}
    }
#if !APP_MODEM_ENABLE || APP_LCD_DIAGNOSTIC
    signal_service_worker(NULL);
#endif
    if(p->signal.layout_rotation!=rotation) {
        p->signal.layout_rotation=(uint8_t)rotation;
        unsigned lines=signal_lines(&p->signal.sample,phone_message_units(rotation));
        unsigned visible=signal_visible(rotation),maximum=lines>visible?lines-visible:0;
        if(p->signal.scroll>maximum)p->signal.scroll=(uint8_t)maximum;
    }
}

#if APP_MODEM_ENABLE && !APP_LCD_DIAGNOSTIC
#include "at.h"
static int signal_query(at_client_t cli,unsigned group,signal_sample_t *sample)
{
    static const char *const commands[]={"AT+QENG=\"servingcell\"","AT+QSPN"};
    at_response_t response=at_create_resp(1024,0,3000);
    if(!response)return 0;
    int ok=at_obj_exec_cmd(cli,response,"%s",commands[group])==QOSA_OK,parsed=0;
    if(ok)for(size_t n=1;n<=response->line_counts;++n) {
        const char *line=at_resp_get_line(response,n);if(!line)continue;
        if(group==0)parsed|=signal_parse_serving(line,sample);
        else parsed|=signal_parse_operator(line,sample);
    }
    at_delete_resp(response);
    ok=ok && parsed;
    return ok;
}
void signal_service_worker(void *client)
{
    if(!signal_service_pending())return;
    if(voice_service_busy()) {signal_publish(SIGNAL_DEFERRED,NULL);return;}
    signal_sample_t sample={0};at_client_t cli=client;
    if(!cli || cli->status!=AT_STATUS_INITIALIZED) {
        sample.present=1;sample.errors=3;signal_publish(SIGNAL_FAILED,&sample);return;
    }
    signal_publish(SIGNAL_FETCHING,NULL);
    for(unsigned group=0;group<2;++group) {
        if(voice_service_busy()) {signal_publish(SIGNAL_DEFERRED,NULL);return;}
        if(!signal_query(cli,group,&sample))sample.errors|=(uint8_t)(1u<<group);
        if(voice_service_busy()) {signal_publish(SIGNAL_DEFERRED,NULL);return;}
    }
    sample.present=1;signal_publish(sample.errors==3?SIGNAL_FAILED:SIGNAL_DONE,&sample);
}
#else
void signal_service_worker(void *client)
{
    (void)client;
    if(signal_service_pending()) {signal_sample_t sample={.present=1,.errors=3};signal_publish(SIGNAL_FAILED,&sample);}
}
#endif
