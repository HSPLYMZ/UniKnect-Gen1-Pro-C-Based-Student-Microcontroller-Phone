#include "voice_service.h"
#include "app_runtime.h"
#include "app_config.h"
#include "board.h"
#include "queue.h"
#include <string.h>

typedef struct { uint32_t id; phone_intent_t intent; } voice_request_t;
static StaticQueue_t s_queue_cb;
static uint8_t s_queue_storage[sizeof(voice_request_t)];
static QueueHandle_t s_queue;
static uint32_t s_next_request,s_reserved_request;
static uint8_t s_call_busy;
static voice_delivery_t s_delivery;
static uint32_t next_nonzero(uint32_t n) { return ++n?n:1u; }

void voice_service_init(void)
{
    s_next_request=s_reserved_request=0;s_call_busy=0;
    memset(&s_delivery,0,sizeof s_delivery);
    s_queue=xQueueCreateStatic(1,sizeof(voice_request_t),s_queue_storage,&s_queue_cb);
    configASSERT(s_queue);
}
uint32_t voice_service_submit(const phone_intent_t *intent)
{
    if(!intent || !s_queue || intent->kind<PHONE_INTENT_DIAL || intent->kind>PHONE_INTENT_HANGUP)
        return 0;
    taskENTER_CRITICAL();
    if(s_reserved_request || s_delivery.result_id) { taskEXIT_CRITICAL(); return 0; }
    s_next_request=next_nonzero(s_next_request);
    voice_request_t request={.id=s_next_request,.intent=*intent};
    s_reserved_request=request.id;
    taskEXIT_CRITICAL();
    if(xQueueSend(s_queue,&request,0)!=pdPASS) {
        taskENTER_CRITICAL();s_reserved_request=0;taskEXIT_CRITICAL();return 0;
    }
    return request.id;
}
int voice_service_busy(void)
{
    taskENTER_CRITICAL();int busy=s_call_busy || s_reserved_request;taskEXIT_CRITICAL();
    return busy;
}
int voice_service_receive(uint32_t revision,voice_delivery_t *out)
{
    if(!out) return 0;
    taskENTER_CRITICAL();
    int changed=revision!=s_delivery.revision;
    if(changed) *out=s_delivery;
    taskEXIT_CRITICAL();return changed;
}
void voice_service_ack(uint32_t request_id)
{
    taskENTER_CRITICAL();
    if(request_id && s_delivery.result_id==request_id) {
        s_delivery.result_id=0;
        if(s_reserved_request==request_id) s_reserved_request=0;
        s_delivery.revision=next_nonzero(s_delivery.revision);
    }
    taskEXIT_CRITICAL();
}
void voice_service_apply(phone_state_t *phone,uint8_t *page,unsigned rotation,
                         uint64_t now_ms,uint32_t *revision)
{
    voice_delivery_t delivery;
    if(!voice_service_receive(*revision,&delivery)) return;
    *revision=delivery.revision;
    phone_event_t event={.kind=PHONE_EVENT_CALL,.source=PHONE_SOURCE_MODEM};
    if(delivery.call_valid) {
        event.value.call.state=delivery.call.state;
        event.value.call.id=delivery.call.id;
        event.value.call.generation=delivery.generation;
        memcpy(event.value.call.number,delivery.call.number,sizeof event.value.call.number);
        (void)phone_apply(phone,page,rotation,&event,now_ms);
    }
    if(delivery.result_id) {
        event.kind=PHONE_EVENT_VOICE_RESULT;
        event.value.result.request_id=delivery.result_id;
        event.value.result.note=delivery.note;
        (void)phone_apply(phone,page,rotation,&event,now_ms);
        voice_service_ack(delivery.result_id);
    }
}

#if APP_MODEM_ENABLE && !APP_LCD_DIAGNOSTIC
#include "at.h"

enum { NOTICE_DSCI, NOTICE_RING, NOTICE_CLIP };
typedef struct { uint8_t kind; voice_snapshot_t call; } voice_notice_t;
static StaticQueue_t s_notice_cb;
static uint8_t s_notice_storage[8*sizeof(voice_notice_t)];
static QueueHandle_t s_notices;
static voice_snapshot_t s_call;
static uint32_t s_generation;
static uint8_t s_have_call;
static uint8_t s_any_voice;
static TickType_t s_provisional_due;
static voice_request_t s_request;
static uint32_t s_target_generation;
static uint8_t s_target_ended;
static int s_command_result;
static TickType_t s_request_due;

/* Callback work is bounded and nonblocking. Only the voice task owns call
 * identity and completion; periodic CLCC reconciles a full notice queue. */
static void notice_post(const voice_notice_t *notice)
{ if(s_notices) (void)xQueueSend(s_notices,notice,0); }
static void dsci_urc(struct at_client *client,const char *data,size_t length,void *arg)
{
    (void)client;(void)length;(void)arg;
    voice_notice_t n={.kind=NOTICE_DSCI};
    if(voice_parse_dsci(data,&n.call)) notice_post(&n);
}
static void ring_urc(struct at_client *client,const char *data,size_t length,void *arg)
{
    (void)client;(void)data;(void)length;(void)arg;
    voice_notice_t n={.kind=NOTICE_RING};notice_post(&n);
}
static void clip_urc(struct at_client *client,const char *data,size_t length,void *arg)
{
    (void)client;(void)length;(void)arg;
    voice_notice_t n={.kind=NOTICE_CLIP};
    if(voice_parse_clip(data,n.call.number)) notice_post(&n);
}
static const struct at_urc s_urcs[]={
    {"^DSCI:","\r\n",dsci_urc}, {"RING","\r\n",ring_urc}, {"+CLIP:","\r\n",clip_urc},
};
static int live(const voice_snapshot_t *call)
{ return call->state!=PHONE_CALL_IDLE && call->state!=PHONE_CALL_ENDED; }
static void publish_call(void)
{
    taskENTER_CRITICAL();
    s_call_busy=s_have_call;
    if(!s_delivery.call_valid || s_delivery.generation!=s_generation ||
       memcmp(&s_delivery.call,&s_call,sizeof s_call)) {
        s_delivery.call=s_call;s_delivery.generation=s_generation;s_delivery.call_valid=1;
        s_delivery.revision=next_nonzero(s_delivery.revision);
    }
    taskEXIT_CRITICAL();
}
static void accept_call(voice_snapshot_t call)
{
    if(s_have_call && s_call.id && call.id && s_call.id!=call.id) return;
    if(!s_have_call) {
        if(!live(&call)) return;
        s_generation=next_nonzero(s_generation);
        if(!call.id) s_provisional_due=xTaskGetTickCount()+pdMS_TO_TICKS(7000);
    } else {
        if(!call.id) call.id=s_call.id;
        if(!*call.number) memcpy(call.number,s_call.number,sizeof call.number);
    }
    s_call=call;s_have_call=(uint8_t)live(&call);
    if(!s_have_call && s_request.id && s_target_generation==s_generation) s_target_ended=1;
    publish_call();
}
static void end_primary(void)
{
    if(s_have_call) {
        voice_snapshot_t ended=s_call;ended.state=PHONE_CALL_ENDED;accept_call(ended);
    }
}
static void drain_notices(void)
{
    voice_notice_t n;
    for(unsigned i=0;i<8 && xQueueReceive(s_notices,&n,0)==pdPASS;++i) {
        if(n.kind==NOTICE_DSCI) {
            if(n.call.waiting) continue;
            if(n.call.state==PHONE_CALL_ENDED &&
               (!s_have_call || !s_call.id || n.call.id!=s_call.id)) continue;
            if(s_have_call && !s_call.id && n.call.incoming!=s_call.incoming) continue;
            accept_call(n.call);
        }
        else if(n.kind==NOTICE_RING && !s_have_call) {
            voice_snapshot_t incoming={.state=PHONE_CALL_INCOMING,.incoming=1};accept_call(incoming);
        } else if(n.kind==NOTICE_CLIP && s_have_call && s_call.state==PHONE_CALL_INCOMING) {
            voice_snapshot_t call=s_call;memcpy(call.number,n.call.number,sizeof call.number);accept_call(call);
        }
    }
}
static void complete(uint8_t note)
{
    if(!s_request.id) return;
    taskENTER_CRITICAL();
    /* Admission stays reserved until the control task acknowledges the result. */
    s_delivery.result_id=s_request.id;s_delivery.note=note;
    s_delivery.revision=next_nonzero(s_delivery.revision);
    taskEXIT_CRITICAL();s_request.id=0;
}
static int command(at_client_t cli,const phone_intent_t *intent)
{
    at_response_t r=at_create_resp(160,0,intent->kind==PHONE_INTENT_DIAL?15000:5000);
    if(!r) return -1;
    int rc=QOSA_ERROR_GENERAL;
    switch(intent->kind) {
    case PHONE_INTENT_DIAL: rc=at_obj_exec_cmd(cli,r,"ATD%s;",intent->number);break;
    case PHONE_INTENT_ANSWER: rc=at_obj_exec_cmd(cli,r,"ATA");break;
    case PHONE_INTENT_REJECT:
    case PHONE_INTENT_HANGUP: rc=at_obj_exec_cmd(cli,r,"AT+CHUP");break;
    default: break;
    }
    at_delete_resp(r);
    return rc==QOSA_OK?1:rc==QOSA_ERROR_TIMEOUT?0:-1;
}
static int simple(at_client_t cli,const char *cmd)
{
    at_response_t r=at_create_resp(128,0,3000);
    if(!r) return 0;
    int ok=at_obj_exec_cmd(cli,r,"%s",cmd)==QOSA_OK;
    at_delete_resp(r);return ok;
}
/* With an established ID, never select a different CLCC record just because it
 * precedes the primary. A successful absence confirms that the primary ended. */
static int reconcile_for(at_client_t cli,int32_t timeout_ms)
{
    at_response_t r=at_create_resp(512,0,timeout_ms);
    if(!r) return -1;
    int rc=at_obj_exec_cmd(cli,r,"AT+CLCC"),found=0;
    s_any_voice=0;
    voice_snapshot_t chosen={0};
    if(rc==QOSA_OK) for(size_t i=1;i<=r->line_counts;++i) {
        voice_snapshot_t candidate;
        if(!voice_parse_clcc(at_resp_get_line(r,i),&candidate)) continue;
        s_any_voice=1;
        if(candidate.waiting) continue; /* Detect as busy, never adopt a waiting call. */
        if(s_have_call && s_call.id && candidate.id!=s_call.id) continue;
        if(s_have_call && !s_call.id && candidate.incoming!=s_call.incoming) continue;
        if(!found || (candidate.state==PHONE_CALL_ACTIVE && chosen.state!=PHONE_CALL_ACTIVE))
            chosen=candidate;
        found=1;
    }
    at_delete_resp(r);
    if(rc!=QOSA_OK) return -1;
    if(found) accept_call(chosen);
    else if(!s_have_call || s_call.id || (int32_t)(xTaskGetTickCount()-s_provisional_due)>=0)
        end_primary();
    return found;
}
static int reconcile(at_client_t cli) { return reconcile_for(cli,3000); }
static int goal_reached(void)
{
    unsigned kind=s_request.intent.kind;
    if(kind==PHONE_INTENT_REJECT || kind==PHONE_INTENT_HANGUP) return s_target_ended;
    if(kind==PHONE_INTENT_ANSWER)
        return s_generation==s_target_generation && s_call.state==PHONE_CALL_ACTIVE;
    return s_have_call && !s_call.incoming && s_generation!=s_target_generation &&
        (s_call.state==PHONE_CALL_DIALING || s_call.state==PHONE_CALL_ACTIVE) &&
        (!*s_call.number || !strcmp(s_call.number,s_request.intent.number));
}
static void request_update(void)
{
    if(!s_request.id) return;
    if(goal_reached()) complete(PHONE_NOTE_NONE);
    else if(s_command_result<0 || s_target_ended ||
            (int32_t)(xTaskGetTickCount()-s_request_due)>=0)
        complete(PHONE_NOTE_CALL_FAILED);
}
static void service_step(at_client_t cli)
{
    drain_notices();
    TickType_t wait=pdMS_TO_TICKS(1000);
    if(s_request.id) {
        TickType_t now=xTaskGetTickCount();
        if((int32_t)(now-s_request_due)>=0) request_update();
        else if(s_request_due-now<wait) wait=s_request_due-now;
    }
    voice_request_t queued;
    if(xQueueReceive(s_queue,&queued,wait)==pdPASS) {
        s_request=queued;s_target_generation=s_generation;s_target_ended=0;
        if(queued.intent.kind==PHONE_INTENT_DIAL) {
            static app_model_t model;app_model_snapshot(&model);
            if(!model.phone.network.registered) complete(PHONE_NOTE_NO_SERVICE);
            else {
                /* A fresh CLCC is required before every explicit ATD, including
                 * a manual retry after an uncertain previous dial. */
                int present=reconcile(cli);
                if(present<0) complete(PHONE_NOTE_NO_SERVICE);
                else if(present || s_have_call || s_any_voice) complete(PHONE_NOTE_BUSY);
                else { s_target_generation=s_generation;s_target_ended=0; }
            }
        } else if(!s_have_call || queued.intent.call_generation!=s_generation ||
                  (queued.intent.kind==PHONE_INTENT_ANSWER && s_call.state!=PHONE_CALL_INCOMING))
            complete(PHONE_NOTE_CALL_FAILED);
        if(s_request.id) {
            s_command_result=command(cli,&queued.intent);
            s_request_due=xTaskGetTickCount()+pdMS_TO_TICKS(7000);
            if(s_command_result<=0) board_log("[voice] request %lu command failed/uncertain; reconcile",
                                              (unsigned long)queued.id);
        }
    }
    drain_notices();
    if(s_request.id) {
        TickType_t now=xTaskGetTickCount();
        if((int32_t)(now-s_request_due)>=0) request_update();
        else {
            uint32_t remaining=(s_request_due-now)*portTICK_PERIOD_MS;
            (void)reconcile_for(cli,(int32_t)(remaining<3000u?remaining:3000u));
        }
    } else (void)reconcile(cli);
    request_update();
}
static void service_offline(void)
{
    voice_request_t queued;
    if(xQueueReceive(s_queue,&queued,0)==pdPASS) s_request=queued;
    complete(PHONE_NOTE_NO_SERVICE);
    /* Losing the link does not prove that a real call ended. */
}
void app_voice_task(void *arg)
{
    (void)arg;
    at_client_t cli=at_client_get_first();uint8_t ready=0;
    s_notices=xQueueCreateStatic(8,sizeof(voice_notice_t),s_notice_storage,&s_notice_cb);
    configASSERT(s_notices);
    for(;;) {
        app_task_beat(APP_TASK_VOICE);
        if(cli->status!=AT_STATUS_INITIALIZED) {
            service_offline();ready=0;
            vTaskDelay(pdMS_TO_TICKS(500));continue;
        }
        if(!ready) {
            if(at_obj_set_urc_table(cli,s_urcs,sizeof s_urcs/sizeof s_urcs[0])!=QOSA_OK) {
                service_offline();
                vTaskDelay(pdMS_TO_TICKS(1000));continue;
            }
            int dsci=simple(cli,"AT^DSCI=1"),clip=simple(cli,"AT+CLIP=1");
            board_log("[voice] ready DSCI=%u CLIP=%u; no automatic dial",dsci,clip);ready=1;
        }
        service_step(cli);
    }
}
#else
void app_voice_task(void *arg)
{
    (void)arg;
    for(;;) {app_task_beat(APP_TASK_VOICE);vTaskDelay(pdMS_TO_TICKS(1000));}
}
#endif
