/* Production voice task loop, deterministic AT/RTOS boundaries. No board IO. */
#include "common.h"
#include <string.h>
#include <stdarg.h>
#define xTaskGetTickCount review_now
#define vTaskDelay review_delay
#define xQueueGenericCreateStatic review_create_queue
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/app_logic.c"
#include "../../firmware/app/voice_logic.c"
#include "../../firmware/app/voice_service.c"

typedef struct { unsigned size,capacity,count,head;uint8_t *bytes; } fake_queue_t;
static fake_queue_t queues[2];
static unsigned queue_count,critical,commands,queries,event_posts,scenarios;
static unsigned receive_fail,alloc_fail,stop_task,beats,registration_fail;
static TickType_t ticks,query_time;
static TickType_t completed_at;
static uint8_t completion_seen;
static int cmd_rc,clcc_rc,change_after_command;
static const char *lines[3];
static unsigned line_count;
static app_model_t model;
static uint32_t revision;
static struct at_client client={.status=AT_STATUS_INITIALIZED};
static struct at_response response;
static const char incoming[]="+CLCC: 1,1,4,0,0,\"10086\",129";
static const char active_in[]="+CLCC: 1,1,0,0,0,\"10086\",129";
static const char active_out[]="+CLCC: 1,0,0,0,0,\"10086\",129";
static const char outgoing[]="+CLCC: 1,0,2,0,0,\"10086\",129";

void vPortEnterCritical(void) { ++critical; }
void vPortExitCritical(void)
{
    CHECK(critical);--critical;
    if(!critical && s_delivery.result_id && !completion_seen) { completed_at=ticks;completion_seen=1; }
}
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t value) { CHECK(!critical);ticks+=value; }
QueueHandle_t xQueueGenericCreateStatic(UBaseType_t capacity,UBaseType_t size,uint8_t *storage,
                                      StaticQueue_t *cb,uint8_t type)
{
    (void)cb;(void)type;CHECK(queue_count<2);
    fake_queue_t *q=&queues[queue_count++];
    *q=(fake_queue_t){.size=size,.capacity=capacity,.bytes=storage};return (QueueHandle_t)q;
}
BaseType_t xQueueGenericSend(QueueHandle_t handle,const void *data,TickType_t wait,BaseType_t pos)
{
    (void)pos;CHECK(!wait);fake_queue_t *q=(fake_queue_t *)handle;
    if(receive_fail || q->count==q->capacity) return pdFALSE;
    memcpy(q->bytes+((q->head+q->count)%q->capacity)*q->size,data,q->size);++q->count;return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t handle,void *data,TickType_t wait)
{
    fake_queue_t *q=(fake_queue_t *)handle;
    if(!q->count) { ticks+=wait;return pdFALSE; }
    memcpy(data,q->bytes+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;--q->count;return pdPASS;
}
void board_log(const char *fmt,...) { (void)fmt;CHECK(!critical); }
void app_model_snapshot(app_model_t *out) { *out=model; }
int app_event_post(const app_event_t *event) { (void)event;++event_posts;return 0; }
void app_task_beat(unsigned id)
{
    CHECK(id==APP_TASK_VOICE);++beats;
    if(stop_task && beats==3) {
        voice_service_apply(&model.phone,&model.page,0,ticks,&revision);
        CHECK(!model.phone.pending && model.phone.note==PHONE_NOTE_NO_SERVICE);
        CHECK(!commands && !critical);test_finish(0);
    }
}
at_client_t at_client_get_first(void) { return &client; }
at_response_t at_create_resp(size_t bytes,size_t count,int32_t timeout)
{
    (void)bytes;(void)count;if(alloc_fail)return NULL;
    memset(&response,0,sizeof response);response.timeout=timeout;return &response;
}
void at_delete_resp(at_response_t r) { (void)r; }
const char *at_resp_get_line(at_response_t r,size_t n) { (void)r;CHECK(n && n<=line_count);return lines[n-1]; }
int at_obj_set_urc_table(at_client_t c,const struct at_urc *urcs,size_t n)
{
    (void)c;CHECK(n==3 && urcs==s_urcs);
    if(registration_fail) { --registration_fail;return QOSA_ERROR_GENERAL; }
    return QOSA_OK;
}
int at_obj_exec_cmd(at_client_t c,at_response_t r,const char *fmt,...)
{
    (void)c;CHECK(!critical);const char *cmd=fmt;va_list args;va_start(args,fmt);
    if(!strcmp(fmt,"%s")) cmd=va_arg(args,const char *);
    va_end(args);
    if(!strcmp(cmd,"AT+CLCC")) {
        ++queries;ticks+=query_time>(TickType_t)r->timeout?(TickType_t)r->timeout:query_time;
        r->line_counts=line_count;return clcc_rc;
    }
    if(!strcmp(cmd,"AT^DSCI=1") || !strcmp(cmd,"AT+CLIP=1")) return QOSA_OK;
    ++commands;
    if(cmd_rc==QOSA_ERROR_TIMEOUT) ticks+=!strncmp(cmd,"ATD",3)?15000:5000;
    if(change_after_command==1) { lines[0]=active_in;line_count=1; }
    else if(change_after_command==2) line_count=0;
    else if(change_after_command==3) { lines[0]=outgoing;line_count=1; }
    return cmd_rc;
}
static void reset(void)
{
    CHECK(!critical);++scenarios;
    queue_count=commands=queries=event_posts=receive_fail=alloc_fail=registration_fail=0;
    ticks=query_time=revision=0;cmd_rc=clcc_rc=QOSA_OK;change_after_command=0;line_count=0;
    completion_seen=0;completed_at=0;
    memset(&s_call,0,sizeof s_call);memset(&s_request,0,sizeof s_request);
    s_have_call=0;s_generation=s_target_generation=0;s_target_ended=0;
    voice_service_init();
    s_notices=xQueueCreateStatic(8,sizeof(voice_notice_t),s_notice_storage,&s_notice_cb);
    app_model_init(&model);model.phone.network.registered=1;
}
static void consume(void)
{
    voice_service_apply(&model.phone,&model.page,model.rotation,ticks,&revision);
    CHECK(!event_posts && !critical);
}
static void establish(unsigned state)
{
    voice_snapshot_t call={.id=1,.state=(uint8_t)state,.incoming=state==PHONE_CALL_INCOMING};
    strcpy(call.number,"10086");accept_call(call);consume();
    if(state==PHONE_CALL_ACTIVE) phone_navigate(&model.phone,&model.page,APP_PAGE_CALL);
    lines[0]=state==PHONE_CALL_INCOMING?incoming:active_out;line_count=1;
}
static uint32_t submit(unsigned kind)
{
    phone_intent_t intent={.kind=(uint8_t)kind,.call_generation=model.phone.call.generation};
    strcpy(intent.number,"10086");uint32_t id=voice_service_submit(&intent);CHECK(id);
    model.phone.pending=1;model.phone.pending_id=id;model.phone.pending_kind=(uint8_t)kind;
    CHECK(!voice_service_submit(&intent));return id;
}
static void notice(const char *text)
{ dsci_urc(&client,text,strlen(text),NULL); }
void test_main(void)
{
    /* Each action: explicit ERROR, uncertain timeout and OK-but-no-transition. */
    for(unsigned kind=PHONE_INTENT_ANSWER;kind<=PHONE_INTENT_HANGUP;++kind)
    for(unsigned failure=0;failure<3;++failure) {
        reset();establish(kind==PHONE_INTENT_HANGUP?PHONE_CALL_ACTIVE:PHONE_CALL_INCOMING);
        uint32_t id=submit(kind);cmd_rc=failure==0?QOSA_ERROR_GENERAL:failure==1?QOSA_ERROR_TIMEOUT:QOSA_OK;
        service_step(&client);
        if(failure) {
            CHECK(s_request.id==id && !s_delivery.result_id);
            TickType_t due=s_request_due;
            for(unsigned i=0;i<9 && s_request.id;++i) { service_step(&client);CHECK(s_request_due==due); }
        }
        CHECK(!s_request.id && s_delivery.result_id==id && s_delivery.note==PHONE_NOTE_CALL_FAILED);
        CHECK(completion_seen && (int32_t)(completed_at-s_request_due)<=0);
        CHECK(commands==1 && model.phone.pending); /* result persists until consumer */
        for(unsigned i=0;i<4;++i)service_step(&client);
        CHECK(commands==1 && s_delivery.result_id==id);consume();
        CHECK(!model.phone.pending && model.phone.note==PHONE_NOTE_CALL_FAILED);
        CHECK(phone_can_select(&model.phone,model.page) && voice_service_busy());
        uint32_t after=revision;consume();CHECK(revision>=after);
        CHECK(commands==1 && model.phone.call.state==(kind==PHONE_INTENT_HANGUP?PHONE_CALL_ACTIVE:PHONE_CALL_INCOMING));
    }
    for(unsigned kind=PHONE_INTENT_ANSWER;kind<=PHONE_INTENT_HANGUP;++kind) {
        reset();establish(kind==PHONE_INTENT_HANGUP?PHONE_CALL_ACTIVE:PHONE_CALL_INCOMING);submit(kind);
        change_after_command=kind==PHONE_INTENT_ANSWER?1:2;service_step(&client);consume();
        CHECK(commands==1 && !model.phone.pending && !model.phone.note);
        CHECK(model.phone.call.state==(kind==PHONE_INTENT_ANSWER?PHONE_CALL_ACTIVE:PHONE_CALL_ENDED));
    }
    /* A negative command response followed by confirmed success wins. */
    reset();establish(PHONE_CALL_INCOMING);submit(PHONE_INTENT_ANSWER);
    cmd_rc=QOSA_ERROR_GENERAL;change_after_command=1;service_step(&client);consume();
    CHECK(!model.phone.pending && model.phone.note==PHONE_NOTE_NONE);

    reset();establish(PHONE_CALL_ACTIVE);uint32_t gen=s_generation;
    notice("^DSCI: 2,1,4,0,10010,129");notice("^DSCI: 2,1,5,0,10010,129");
    notice("^DSCI: 2,1,6,0,10010,129");drain_notices();consume();
    CHECK(s_have_call && s_call.id==1 && s_generation==gen && model.phone.call.state==PHONE_CALL_ACTIVE);
    lines[0]="+CLCC: 2,1,0,0,0,\"10010\",129";lines[1]=active_out;line_count=2;
    CHECK(reconcile(&client)==1 && s_call.id==1);
    notice("^DSCI: 1,0,6,0,10086,129");drain_notices();consume();CHECK(!s_have_call);
    notice("^DSCI: 2,1,4,0,10010,129");drain_notices();consume();
    CHECK(s_call.id==2 && s_generation!=gen && model.phone.call.state==PHONE_CALL_INCOMING);
    model.phone.action_selected=1;notice("^DSCI: 2,1,4,0,10010,129");drain_notices();consume();
    CHECK(model.phone.action_selected==1);

    reset();ring_urc(&client,"RING",4,NULL);drain_notices();consume();uint32_t provisional=s_generation;
    CHECK(reconcile(&client)==0 && s_have_call); /* RING may precede CLCC visibility. */
    notice("^DSCI: 2,1,6,0,10010,129");drain_notices();CHECK(s_have_call && !s_call.id);
    clip_urc(&client,"+CLIP: \"10086\",129",0,NULL);drain_notices();consume();
    lines[0]=incoming;line_count=1;CHECK(reconcile(&client)==1);
    CHECK(s_call.id==1 && s_generation==provisional && !strcmp(s_call.number,"10086"));
    consume();CHECK(model.phone.call.id==1);model.phone.action_selected=1;end_primary();consume();
    notice("^DSCI: 1,1,4,0,10086,129");drain_notices();consume();
    CHECK(!model.phone.action_selected && s_generation!=provisional);
    /* A full notice queue is nonblocking and authoritative CLCC repairs it. */
    for(unsigned i=0;i<12;++i)ring_urc(&client,"RING",4,NULL);
    line_count=0;service_step(&client);consume();CHECK(!s_have_call);
    reset();ring_urc(&client,"RING",4,NULL);drain_notices();
    for(unsigned i=0;i<6;++i) service_step(&client);
    CHECK(s_have_call);service_step(&client);CHECK(!s_have_call);

    /* Full shared application queue cannot lose the terminal result. */
    reset();model.phone.network.registered=0;uint32_t id=submit(PHONE_INTENT_DIAL);
    service_step(&client);CHECK(!commands && s_delivery.result_id==id);
    for(unsigned i=0;i<10;++i)service_step(&client);
    CHECK(s_delivery.result_id==id && !event_posts);consume();
    CHECK(!model.phone.pending && model.phone.note==PHONE_NOTE_NO_SERVICE && !voice_service_busy());

    /* Old result, wrong ack and a newer mailbox revision cannot clear a new op. */
    reset();establish(PHONE_CALL_INCOMING);id=submit(PHONE_INTENT_ANSWER);
    cmd_rc=QOSA_ERROR_GENERAL;service_step(&client);voice_delivery_t captured;
    CHECK(voice_service_receive(0,&captured));voice_service_ack(id+1);CHECK(s_delivery.result_id==id);
    consume();uint32_t next=submit(PHONE_INTENT_REJECT);CHECK(next!=id);
    phone_event_t old={.kind=PHONE_EVENT_VOICE_RESULT,.source=PHONE_SOURCE_MODEM,
        .value.result={.request_id=id,.note=PHONE_NOTE_CALL_FAILED}};
    CHECK(phone_apply(&model.phone,&model.page,0,&old,ticks) && model.phone.pending_id==next);
    service_step(&client);voice_service_ack(id);CHECK(s_delivery.result_id==next);consume();
    CHECK(!model.phone.pending);
    /* An update between receive and ack remains visible on the next read. */
    id=submit(PHONE_INTENT_ANSWER);service_step(&client);CHECK(voice_service_receive(revision,&captured));
    uint32_t before=captured.revision;notice("^DSCI: 1,1,6,0,10086,129");drain_notices();
    voice_service_ack(id);CHECK(voice_service_receive(before,&captured) && captured.call.state==PHONE_CALL_ENDED);

    reset();establish(PHONE_CALL_INCOMING);ticks=0xfffffff0u;submit(PHONE_INTENT_ANSWER);
    service_step(&client);for(unsigned i=0;i<8;++i)service_step(&client);consume();
    CHECK(!model.phone.pending && model.phone.note==PHONE_NOTE_CALL_FAILED);
    reset();s_next_request=0xffffffffu;CHECK(submit(PHONE_INTENT_DIAL)==1);service_offline();consume();
    CHECK(!model.phone.pending && model.phone.note==PHONE_NOTE_NO_SERVICE);

    reset();establish(PHONE_CALL_ACTIVE);submit(PHONE_INTENT_HANGUP);
    clcc_rc=QOSA_ERROR_TIMEOUT;query_time=3000;
    for(unsigned i=0;i<5;++i) service_step(&client);
    consume();
    CHECK(!model.phone.pending && model.phone.call.state==PHONE_CALL_ACTIVE);
    CHECK(completion_seen && (int32_t)(completed_at-s_request_due)<=0);
    reset();establish(PHONE_CALL_ACTIVE);submit(PHONE_INTENT_HANGUP);service_offline();consume();
    CHECK(!commands && !model.phone.pending && model.phone.call.state==PHONE_CALL_ACTIVE);

    reset();submit(PHONE_INTENT_DIAL);change_after_command=3;service_step(&client);consume();
    CHECK(commands==1 && queries==2 && !model.phone.pending && model.phone.call.state==PHONE_CALL_DIALING);
    reset();submit(PHONE_INTENT_DIAL);cmd_rc=QOSA_ERROR_TIMEOUT;
    service_step(&client);for(unsigned i=0;i<8;++i)service_step(&client);consume();
    CHECK(commands==1 && !model.phone.pending && !s_have_call);
    /* A previous ATD may have taken effect late: do not send a second ATD. */
    submit(PHONE_INTENT_DIAL);lines[0]=outgoing;line_count=1;service_step(&client);consume();
    CHECK(commands==1 && model.phone.note==PHONE_NOTE_BUSY && model.phone.call.state==PHONE_CALL_DIALING);
    reset();submit(PHONE_INTENT_DIAL);lines[0]="+CLCC: 2,1,5,0,0,\"10010\",129";line_count=1;
    service_step(&client);consume();CHECK(!commands && model.phone.note==PHONE_NOTE_BUSY && !s_have_call);
    reset();submit(PHONE_INTENT_DIAL);cmd_rc=QOSA_ERROR_GENERAL;
    service_step(&client);consume();CHECK(commands==1 && !model.phone.pending && model.phone.note==PHONE_NOTE_CALL_FAILED);
    reset();submit(PHONE_INTENT_DIAL);service_step(&client);
    for(unsigned i=0;i<8;++i) service_step(&client);
    consume();CHECK(commands==1 && !model.phone.pending && model.phone.note==PHONE_NOTE_CALL_FAILED);
    /* Failed old-call identity cannot make a new caller satisfy ANSWER. */
    reset();establish(PHONE_CALL_INCOMING);submit(PHONE_INTENT_ANSWER);service_step(&client);
    notice("^DSCI: 1,1,6,0,10086,129");notice("^DSCI: 2,1,4,0,10010,129");
    lines[0]="+CLCC: 2,1,4,0,0,\"10010\",129";line_count=1;
    service_step(&client);consume();
    CHECK(!model.phone.pending && model.phone.note==PHONE_NOTE_CALL_FAILED && model.phone.call.id==2);
    /* Different CLCC record alone is still an existing call for dial preflight. */
    reset();establish(PHONE_CALL_ACTIVE);model.phone.call.state=PHONE_CALL_IDLE;submit(PHONE_INTENT_DIAL);
    lines[0]="+CLCC: 2,1,0,0,0,\"10010\",129";line_count=1;
    service_step(&client);consume();CHECK(!commands && !model.phone.pending && model.phone.note==PHONE_NOTE_BUSY);
    reset();submit(PHONE_INTENT_DIAL);clcc_rc=QOSA_ERROR_TIMEOUT;service_step(&client);consume();
    CHECK(!commands && !model.phone.pending && model.phone.note==PHONE_NOTE_NO_SERVICE);
    reset();submit(PHONE_INTENT_DIAL);alloc_fail=1;service_step(&client);consume();
    CHECK(!commands && !model.phone.pending);
    reset();receive_fail=1;phone_intent_t intent={.kind=PHONE_INTENT_DIAL};
    CHECK(!voice_service_submit(&intent) && !voice_service_busy());

    /* A stale page/model generation must never control a new caller. */
    reset();establish(PHONE_CALL_INCOMING);model.phone.call.generation=99;submit(PHONE_INTENT_REJECT);
    service_step(&client);consume();CHECK(!commands && !model.phone.pending);
    /* Exercise production startup and ready loop too; heartbeat stops the task. */
    reset();queue_count=1;s_notices=NULL;stop_task=1;beats=0;registration_fail=1;
    submit(PHONE_INTENT_DIAL);
    CHECK(scenarios>=28);app_voice_task(NULL);test_finish(999);
}
