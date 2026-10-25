#include "common.h"
#include <string.h>
#include <stdarg.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/signal_service.c"
static phone_state_t phone;
static unsigned busy,queries,critical,wakes,fail_mask,allocate_fail,interrupt_group;
static unsigned order[32];
static struct at_client client={.status=AT_STATUS_INITIALIZED};
static struct at_response response;
static const char *responses[4];
void vPortEnterCritical(void) {++critical;}
void vPortExitCritical(void) {CHECK(critical);--critical;}
int voice_service_busy(void) {return (int)busy;}
static void wake(void) {CHECK(!critical);++wakes;}
at_response_t at_create_resp(size_t bytes,size_t lines,int32_t timeout)
{
    (void)lines;CHECK(bytes<=1024 && timeout<=3000 && !critical);
    if(allocate_fail)return NULL;
    memset(&response,0,sizeof response);return &response;
}
void at_delete_resp(at_response_t r) {(void)r;}
const char *at_resp_get_line(at_response_t r,size_t n) {(void)r;CHECK(n && n<=4);return responses[n-1];}
int at_obj_exec_cmd(at_client_t cli,at_response_t r,const char *fmt,...)
{
    (void)cli;CHECK(!critical && !busy);va_list args;va_start(args,fmt);
    const char *cmd=va_arg(args,const char *);va_end(args);
    unsigned group=strstr(cmd,"servingcell")?0:!strcmp(cmd,"AT+QSPN")?1:2;
    CHECK(group<2 && queries<32);order[queries++]=group;
    if(group==0) {responses[0]="+QENG: \"servingcell\",\"NOCONN\",\"LTE\",\"TDD\",460,15,6097C02,65,38950,40,5,5,58B2,-79,-4,-56,116,40";r->line_counts=1;}
    else if(group==1) {responses[0]="+QSPN: \"FULL\",\"CBN\",\"中国广电\",0,\"46015\"";r->line_counts=1;}
    if(interrupt_group==group+1) {busy=1;interrupt_group=0;}
    return fail_mask&(1u<<group)?QOSA_ERROR_GENERAL:QOSA_OK;
}
static void reset(void)
{
    CHECK(!critical);busy=queries=wakes=fail_mask=allocate_fail=interrupt_group=0;
    signal_service_init();signal_service_set_waker(wake);phone_state_init(&phone);
    client.status=AT_STATUS_INITIALIZED;
}
static uint32_t request(void)
{
    phone.signal.refresh_requested=1;signal_service_apply(&phone,100,0);
    CHECK(phone.signal.request_id && phone.signal.state==SIGNAL_QUEUED);
    uint32_t id=phone.signal.request_id;CHECK(signal_service_request()==id);return id;
}
void test_main(void)
{
    reset();uint32_t id=request();CHECK(wakes==2);
    signal_service_worker(&client);CHECK(queries==2 && s_signal.id==id && s_signal.state==SIGNAL_DONE);
    for(unsigned i=0;i<5;++i)signal_service_worker(&client);
    CHECK(queries==2 && s_signal.id==id);
    CHECK(phone.signal.sample.valid==0);signal_service_apply(&phone,1000,0);
    CHECK(!phone.signal.request_id && phone.signal.sample.valid==127 && !s_signal.id);
    CHECK(!strcmp(phone.signal.sample.operator_name,"中国广电") && phone.signal.sample.sinr_raw==116);
    signal_sample_t frozen=phone.signal.sample;
    phone.network.rsrp_dbm=-120;signal_service_apply(&phone,60000,0);
    CHECK(!memcmp(&frozen,&phone.signal.sample,sizeof frozen) && queries==2);
    reset();request();busy=1;signal_service_worker(&client);signal_service_apply(&phone,500,0);
    CHECK(!queries && phone.signal.state==SIGNAL_DEFERRED && phone.signal.request_id);
    busy=0;signal_service_worker(&client);signal_service_apply(&phone,1000,0);CHECK(queries==2 && !phone.signal.request_id);
    for(unsigned group=1;group<=2;++group) {
        reset();request();interrupt_group=group;signal_service_worker(&client);
        CHECK(queries==group && s_signal.state==SIGNAL_DEFERRED);
        busy=0;signal_service_worker(&client);CHECK(queries==group+2 && order[group]==0);
        signal_service_apply(&phone,1000,0);CHECK(phone.signal.sample.valid==127);
    }
    for(unsigned mask=1;mask<=3;++mask) {
        reset();request();fail_mask=mask;signal_service_worker(&client);signal_service_apply(&phone,1000,0);
        CHECK(!phone.signal.request_id && phone.signal.sample.errors==mask && phone.signal.sample.present);
        if(mask&1)CHECK(!(phone.signal.sample.valid&63));
        if(mask&2)CHECK(!(phone.signal.sample.valid&SIGNAL_OPERATOR));
        signal_service_worker(&client);CHECK(queries==2); /* no retry */
    }
    reset();request();allocate_fail=1;signal_service_worker(&client);signal_service_apply(&phone,1000,0);
    CHECK(!phone.signal.request_id && phone.signal.state==SIGNAL_FAILED && !queries);
    reset();request();client.status=0;signal_service_worker(&client);signal_service_apply(&phone,1000,0);
    CHECK(!phone.signal.request_id && phone.signal.state==SIGNAL_FAILED && !queries);
    reset();s_signal.next=0xffffffffu;CHECK(request()==1);
    signal_service_worker(&client);phone.signal.request_id=99;signal_service_apply(&phone,1000,0);
    CHECK(phone.signal.request_id==99 && !phone.signal.sample.present && !s_signal.id); /* stale result ignored */
    reset();request();phone.signal.scroll=20;
    signal_service_worker(&client);signal_service_apply(&phone,1000,0);
    CHECK(phone.signal.scroll<20 && !phone.signal.request_id);
    CHECK(!critical);test_finish(0);
}
