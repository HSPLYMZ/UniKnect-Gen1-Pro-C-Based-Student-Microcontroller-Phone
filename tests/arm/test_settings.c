#include "common.h"
#include <string.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/app_logic.c"
#include "../../firmware/app/joy_input.c"
#include "../../firmware/app/settings_store.c"
#include "../../firmware/app/settings_service.c"
#define flash ((uint8_t *)(uintptr_t)SETTINGS_FLASH_BASE)
static unsigned critical;
void vPortEnterCritical(void) {++critical;}
void vPortExitCritical(void) {CHECK(critical);--critical;}
static void read_mem(uint32_t a,void *out,unsigned n) {memcpy(out,flash+a-SETTINGS_FLASH_BASE,n);}
static int write_mem(uint32_t a,uint32_t w) {CHECK(!critical);memcpy(flash+a-SETTINGS_FLASH_BASE,&w,4);return 1;}
static int erase_mem(unsigned n) {memset(flash+n*SETTINGS_SECTOR_BYTES,255,SETTINGS_SECTOR_BYTES);return 1;}
const settings_io_t settings_flash_io={read_mem,write_mem,erase_mem};
static app_model_t m;
static joy_input_t joy;
static uint32_t now;
static void samples(uint16_t raw,unsigned n)
{for(unsigned i=0;i<n;++i) {now+=5;CHECK(settings_input_step(&joy,raw,1,now,m.rotation)==JOY_NONE);settings_service_apply(&m,0);}}
static uint8_t key(uint8_t k) {return phone_resolve_key(&m.phone,&m.page,k);}
static void navigate(uint8_t k) {uint8_t a=key(k);if(a)CHECK(settings_action(&m,a));}
static void reset(void)
{
    memset(flash,255,(2*SETTINGS_SECTOR_BYTES));memset(&s_cal,0,sizeof s_cal);now=0;
    settings_boot_init();app_model_init(&m);settings_model_load(&m);settings_input_init(&joy);
}
void test_main(void)
{
    reset();
    for(unsigned i=0;i<PHONE_TOP_COUNT;++i) {navigate(PHONE_KEY_RIGHT);CHECK(m.page==phone_top_page((i+1u)%PHONE_TOP_COUNT));}
    phone_navigate(&m.phone,&m.page,APP_PAGE_SETTINGS_ENTRY);navigate(PHONE_KEY_SELECT);CHECK(m.page==APP_PAGE_SETTINGS_MENU);
    navigate(PHONE_KEY_SELECT);CHECK(m.page==APP_PAGE_ROTATION);
    navigate(PHONE_KEY_UP);CHECK(!m.phone.selected_row);navigate(PHONE_KEY_DOWN);navigate(PHONE_KEY_SELECT);
    CHECK(m.rotation==1 && m.phone.system.desired.rotation==1 && m.page==APP_PAGE_ROTATION);
    for(unsigned i=0;i<20;++i)settings_service_apply(&m,1);
    CHECK(!s_store.sequence && m.phone.system.save_state==SETTINGS_PENDING);
    for(unsigned i=0;i<20;++i)settings_service_apply(&m,0);
    CHECK(s_store.sequence==1 && m.phone.system.save_state==SETTINGS_SAVED);
    app_event_t temporary={.kind=APP_EVENT_ROTATION_MODE,.value.rotation_mode={0,3}};
    CHECK(app_model_apply(&m,&temporary) && m.rotation==3 && m.phone.system.desired.rotation==1);
    settings_store_t boot;settings_store_scan(&boot,&settings_flash_io);CHECK(boot.config.rotation==1);
    navigate(PHONE_KEY_BACK);CHECK(m.page==APP_PAGE_SETTINGS_MENU && m.phone.selected_row==0);
    navigate(PHONE_KEY_DOWN);navigate(PHONE_KEY_SELECT);CHECK(m.page==APP_PAGE_CAL_CONFIRM);
    navigate(PHONE_KEY_SELECT);CHECK(m.phone.system.cal_id && m.phone.navigation_locked);
    uint32_t id=m.phone.system.cal_id;CHECK(settings_action(&m,PHONE_KEY_CAL_START) && m.phone.system.cal_id==id);
    CHECK(!app_model_apply(&m,&temporary));
    samples(4065,16);CHECK(joy.stage==1);
    const uint16_t v[]={3500,3100,2600,2200,1700};
    for(unsigned i=0;i<5;++i) {samples(v[i],8);samples(4065,5);}
    CHECK(m.page==APP_PAGE_SETTINGS_MENU && m.phone.selected_row==1 && !m.phone.navigation_locked && !m.phone.system.cal_id);
    CHECK(m.phone.system.desired.manual && m.phone.system.desired.calibration_rotation==3 && joy.levels[0]==3500);
    for(unsigned i=0;i<20;++i)settings_service_apply(&m,0);
    settings_store_scan(&boot,&settings_flash_io);CHECK(boot.config.rotation==1 && boot.config.levels[4]==1700);
    /* New request cancelled, then timeout across Tick wrap: restore prior map. */
    CHECK(settings_action(&m,PHONE_KEY_CAL_START));samples(4065,1);
    CHECK(settings_action(&m,PHONE_KEY_CAL_CANCEL));samples(4065,1);
    CHECK(!m.phone.system.cal_id && joy.levels[0]==3500 && !m.phone.navigation_locked);
    now=0xfffffff0u;CHECK(settings_action(&m,PHONE_KEY_CAL_START));samples(4065,1);now+=60000u;samples(4065,1);
    CHECK(m.phone.system.cal_result==CAL_TIMEOUT && joy.levels[0]==3500);
    /* Incoming cancels before progress, preserving incoming-page ownership. */
    CHECK(settings_action(&m,PHONE_KEY_CAL_START));samples(4065,1);
    m.page=APP_PAGE_INCOMING;m.phone.call.state=PHONE_CALL_INCOMING;settings_service_apply(&m,1);samples(4065,1);
    CHECK(m.page==APP_PAGE_INCOMING && !m.phone.navigation_locked && joy.levels[0]==3500);
    CHECK(settings_action(&m,PHONE_KEY_CAL_START) && m.phone.system.cal_result==CAL_BUSY);
    CHECK(app_command_parse("settings status").kind==APP_CMD_SETTINGS_STATUS);
    CHECK(app_command_parse("joy cancel").phone.value.key==PHONE_KEY_CAL_CANCEL);
    /* Incoming arriving after input finished, but before controller acceptance,
     * still restores the old map and cannot navigate away from the call. */
    reset();CHECK(settings_action(&m,PHONE_KEY_CAL_HOST_START));
    for(unsigned i=0;i<16;++i) {now+=5;(void)settings_input_step(&joy,4065,1,now,0);}
    for(unsigned i=0;i<5;++i) {
        for(unsigned j=0;j<8;++j) {now+=5;(void)settings_input_step(&joy,v[i],1,now,0);}
        for(unsigned j=0;j<5;++j) {now+=5;(void)settings_input_step(&joy,4065,1,now,0);}
    }
    CHECK(s_cal.result==CAL_SUCCESS && joy.levels[0]==3500);
    m.page=APP_PAGE_INCOMING;m.phone.call.state=PHONE_CALL_INCOMING;
    settings_service_apply(&m,1);CHECK(m.phone.system.cal_id && m.phone.navigation_locked);
    (void)settings_input_step(&joy,4065,1,now+5,0);settings_service_apply(&m,1);
    CHECK(!m.phone.system.cal_id && !m.phone.navigation_locked && joy.levels[0]==2046 && m.page==APP_PAGE_INCOMING);
    reset();CHECK(settings_action(&m,PHONE_KEY_CAL_HOST_START));samples(4065,1);
    CHECK(settings_action(&m,PHONE_KEY_CAL_CANCEL));
    (void)settings_input_step(&joy,4065,1,now+5,0);
    id=m.phone.system.cal_id;m.phone.system.cal_id=99;settings_service_apply(&m,0);
    CHECK(m.phone.system.cal_id==99 && m.phone.navigation_locked);
    m.phone.system.cal_id=id;settings_service_apply(&m,0);CHECK(!m.phone.system.cal_id);
    CHECK(!critical);test_finish(0);
}
