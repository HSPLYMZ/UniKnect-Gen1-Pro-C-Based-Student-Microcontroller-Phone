#include "app_runtime.h"
#include "app_config.h"
#include "board.h"
#include "storage_logic.h"
#include "voice_service.h"
#include <stdio.h>
#include <string.h>

#if APP_MODEM_ENABLE
#include "at.h"

#define SD_FILE "SD:uniknect.jsonl"
#define SD_RECORD_CAP 256u

typedef struct {
    at_client_t client;
    const char *data;
    uint32_t length,written;
    uint8_t complete,error;
} sd_write_t;

static void publish(unsigned state,uint64_t free_bytes,uint64_t total_bytes,uint32_t writes)
{
    app_event_t event={.kind=APP_EVENT_STORAGE};
    event.value.storage.state=(uint8_t)state;
    event.value.storage.free_mib=(uint32_t)(free_bytes/(1024u*1024u));
    event.value.storage.total_mib=(uint32_t)(total_bytes/(1024u*1024u));
    event.value.storage.writes=writes;
    (void)app_event_post(&event);
}

static int space(at_client_t client,uint64_t *free_bytes,uint64_t *total_bytes)
{
    at_response_t resp=at_create_resp(160,0,3000);
    if(!resp) return 0;
    int ok=0;
    if(at_obj_exec_cmd(client,resp,"AT+QFLDS=\"SD\"")==QOSA_OK)
        for(size_t i=1;i<=resp->line_counts;++i)
            if(storage_parse_space(at_resp_get_line(resp,i),free_bytes,total_bytes)) {ok=1;break;}
    at_delete_resp(resp);
    return ok;
}

static int open_file(at_client_t client,int *handle)
{
    at_response_t resp=at_create_resp(128,0,3000);
    if(!resp) return 0;
    int ok=0;
    /* Mode 0 preserves an existing file; mode 1 would silently truncate it. */
    if(at_obj_exec_cmd(client,resp,"AT+QFOPEN=\"" SD_FILE "\",0")==QOSA_OK)
        for(size_t i=1;i<=resp->line_counts;++i)
            if(storage_parse_handle(at_resp_get_line(resp,i),handle)) {ok=1;break;}
    at_delete_resp(resp);
    return ok;
}

static int seek_end(at_client_t client,int handle,uint32_t *position)
{
    at_response_t resp=at_create_resp(128,0,3000);
    if(!resp) return 0;
    int ok=at_obj_exec_cmd(client,resp,"AT+QFSEEK=%d,0,2",handle)==QOSA_OK;
    if(ok) {
        ok=0;
        if(at_obj_exec_cmd(client,resp,"AT+QFPOSITION=%d",handle)==QOSA_OK)
            for(size_t i=1;i<=resp->line_counts;++i)
                if(storage_parse_position(at_resp_get_line(resp,i),position)) {ok=1;break;}
    }
    at_delete_resp(resp);
    return ok;
}

static int close_file(at_client_t client,int handle)
{
    at_response_t resp=at_create_resp(96,0,3000);
    if(!resp) return 0;
    int ok=at_obj_exec_cmd(client,resp,"AT+QFCLOSE=%d",handle)==QOSA_OK;
    at_delete_resp(resp);
    return ok;
}

static int line_copy(char out[64],const char *input,size_t length)
{
    if(length>=64u) return 0;
    if(out!=input) memcpy(out,input,length);
    out[length]=0;
    while(length && (out[length-1]=='\r' || out[length-1]=='\n' || out[length-1]==' '))
        out[--length]=0;
    return 1;
}

/* The AT parser owns the receive stream while this callback is active. The
 * command lock remains held by at_obj_exec_cmd, so periodic network probes
 * cannot interleave with CONNECT/data/+QFWRITE/OK. */
static void write_callback(const char *first,size_t size,void *arg)
{
    sd_write_t *ctx=(sd_write_t *)arg;
    char line[64];
    if(!line_copy(line,first,size)) {ctx->error=1;return;}
    unsigned attempts=0;
    while(strcmp(line,"CONNECT")!=0) {
        if(strstr(line,"ERROR") || ++attempts>=4u) {ctx->error=1;return;}
        size_t n=at_client_self_recv(ctx->client,line,sizeof line-1u,750,1,false);
        if(!n || !line_copy(line,line,n)) {ctx->error=1;return;}
    }
    if(at_client_obj_send_nolock(ctx->client,ctx->data,ctx->length,false)!=ctx->length) {
        ctx->error=1;return;
    }
    for(attempts=0;attempts<4u;++attempts) {
        size_t n=at_client_self_recv(ctx->client,line,sizeof line-1u,750,1,false);
        if(!n || !line_copy(line,line,n)) {ctx->error=1;return;}
        uint32_t written;
        if(storage_parse_write(line,&written)) ctx->written=written;
        else if(!strcmp(line,"OK")) {
            ctx->complete=1;
            return;
        } else if(strstr(line,"ERROR")) {ctx->error=1;return;}
    }
    ctx->error=1;
}

static int write_file(at_client_t client,int handle,const char *data,uint32_t length)
{
    sd_write_t ctx={.client=client,.data=data,.length=length};
    at_response_t resp=at_create_resp_by_selffunc_new(128,0,6000,write_callback,&ctx);
    if(!resp) return 0;
    int rc=at_obj_exec_cmd(client,resp,"AT+QFWRITE=%d,%u,10",handle,(unsigned)length);
    at_delete_resp(resp);
    return rc==QOSA_OK && !ctx.error && ctx.complete && ctx.written==length;
}

static int make_record(char out[SD_RECORD_CAP],const app_model_t *m)
{
    /* Leading newline isolates a record from a possibly torn previous write. */
    storage_record_t r={.uptime_ms=m->uptime_ms,.light_raw=m->light_raw,
        .input_raw=m->input_raw,.x_mg=m->accel_x_mg,.y_mg=m->accel_y_mg,
        .z_mg=m->accel_z_mg,.rsrp_dbm=m->phone.network.rsrp_dbm};
    return storage_format_record(out,SD_RECORD_CAP,&r);
}

void app_storage_task(void *arg)
{
    (void)arg;
    static app_model_t model;
    char record[SD_RECORD_CAP];
    uint64_t free_bytes=0,total_bytes=0;
    uint32_t writes=0;
    uint8_t damaged=0;
    at_client_t client=at_client_get_first();
    for(;;) {
        app_task_beat(APP_TASK_STORAGE);
        if(client->status!=AT_STATUS_INITIALIZED) {
            publish(APP_SD_WAITING,free_bytes,total_bytes,writes);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if(damaged) {
            publish(APP_SD_ERROR,free_bytes,total_bytes,writes);
            vTaskDelay(pdMS_TO_TICKS(CFG_SD_RETRY_MS));
            continue;
        }
        if(voice_service_busy()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if(!space(client,&free_bytes,&total_bytes)) {
            publish(APP_SD_ERROR,0,0,writes);
            board_log("[sd] capacity query failed; retry in 30s");
            vTaskDelay(pdMS_TO_TICKS(CFG_SD_RETRY_MS));
            continue;
        }
        if(free_bytes<CFG_SD_MIN_FREE_BYTES+SD_RECORD_CAP) {
            publish(APP_SD_LIMIT,free_bytes,total_bytes,writes);
            board_log("[sd] reserve reached; logging stopped");
            vTaskDelay(pdMS_TO_TICKS(CFG_SD_RETRY_MS));
            continue;
        }
        app_model_snapshot(&model);
        if(voice_service_busy()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int length=make_record(record,&model);
        int handle=-1;
        uint32_t position=0;
        if(!length || !open_file(client,&handle)) {
            publish(APP_SD_ERROR,free_bytes,total_bytes,writes);
            board_log("[sd] open failed; retry in 30s");
            vTaskDelay(pdMS_TO_TICKS(CFG_SD_RETRY_MS));
            continue;
        }
        if(voice_service_busy()) {
            damaged=(uint8_t)!close_file(client,handle);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int located=seek_end(client,handle,&position);
        if(voice_service_busy()) {
            damaged=(uint8_t)!close_file(client,handle);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if(!located || position>CFG_SD_MAX_FILE_BYTES-(uint32_t)length) {
            int closed=close_file(client,handle);
            damaged=(uint8_t)!closed;
            publish(located?APP_SD_LIMIT:APP_SD_ERROR,free_bytes,total_bytes,writes);
            board_log(located?"[sd] 8 MiB file limit reached":"[sd] seek failed");
            vTaskDelay(pdMS_TO_TICKS(CFG_SD_RETRY_MS));
            continue;
        }
        int written=write_file(client,handle,record,(uint32_t)length);
        int closed=close_file(client,handle);
        if(!written || !closed) {
            damaged=1; /* A failed transfer may have changed the last line. */
            publish(APP_SD_ERROR,free_bytes,total_bytes,writes);
            board_log("[sd] write/close failed; stop until reboot");
            vTaskDelay(pdMS_TO_TICKS(CFG_SD_RETRY_MS));
            continue;
        }
        ++writes;
        publish(APP_SD_READY,free_bytes,total_bytes,writes);
        if(writes==1u || writes%6u==0u)
            board_log("[sd] %s appended record %lu, file=%lu bytes, free=%lu MiB",
                      SD_FILE,(unsigned long)writes,(unsigned long)(position+(uint32_t)length),
                      (unsigned long)(free_bytes/(1024u*1024u)));
        app_task_beat(APP_TASK_STORAGE);
        vTaskDelay(pdMS_TO_TICKS(CFG_SD_LOG_MS));
    }
}
#else
void app_storage_task(void *arg)
{
    (void)arg;
    app_event_t event={.kind=APP_EVENT_STORAGE};
    event.value.storage.state=APP_SD_DISABLED;
    (void)app_event_post(&event);
    for(;;) {
        app_task_beat(APP_TASK_STORAGE);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif
