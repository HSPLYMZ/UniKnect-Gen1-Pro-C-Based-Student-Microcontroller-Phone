#include "common.h"
#include <string.h>
#include <stdio.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/app_logic.c"
static const char serving[]="+QENG: \"servingcell\",\"NOCONN\",\"LTE\",\"TDD\",460,15,6097C02,65,38950,40,5,5,58B2,-79,-4,-56,116,40";
void test_main(void)
{
    signal_sample_t s={0};CHECK(sizeof s<256);
    CHECK(signal_parse_serving(serving,&s) && s.valid==63 && s.registered && s.lte);
    CHECK(s.cell_id==0x6097c02 && s.band==40 && s.rsrp==-79 && s.sinr_raw==116);
    char out[128];signal_half_db(out,sizeof out,116);CHECK(!strcmp(out,"34.5"));
    signal_half_db(out,sizeof out,46);CHECK(!strcmp(out,"-0.5"));
    signal_half_db(out,sizeof out,47);CHECK(!strcmp(out,"0.0"));
    signal_half_db(out,sizeof out,48);CHECK(!strcmp(out,"0.5"));
    signal_half_db(out,sizeof out,1);CHECK(!strcmp(out,"-23.0"));
    signal_half_db(out,sizeof out,127);CHECK(!strcmp(out,"40.0"));
    CHECK(signal_parse_serving("+QENG: \"servingcell\",\"NOCONN\",\"LTE\",\"TDD\",460,15,FFFFFFFF,65,38950,0,5,5,58B2,-,-999,56,255,40",&s));
    CHECK(!s.valid && s.sinr_raw==255); /* Preserve invalid raw code for diagnostics. */
    CHECK(!signal_parse_serving("+QENG: \"servingcell\",\"NOCONN\",\"LTE\"",&s));
    CHECK(signal_parse_serving("+QENG: \"servingcell\",\"SEARCH\"",&s) && !s.registered && !s.valid);
    CHECK(signal_parse_serving(serving,&s));
    CHECK(signal_parse_operator("+QSPN: \"004300480049004E0041002000420052004F00410044004E00450054\",\"00430042004E\",\"4E2D56FD5E7F7535\",1,\"46015\"",&s));
    CHECK(!strcmp(s.operator_name,"中国广电") && s.valid==127);
    CHECK(signal_parse_operator("+QSPN: \"FULL, NAME\",\"SHORT\",\"\",0,\"46015\"",&s) && !strcmp(s.operator_name,"SHORT"));
    CHECK(signal_parse_operator("+QSPN: \"FULL\",\"\",\"BRAND\",0,\"46015\"",&s) && !strcmp(s.operator_name,"BRAND"));
    CHECK(!signal_parse_operator("+QSPN: \"D800\",\"DC00\",\"ZZZZ\",1,\"46015\"",&s));
    CHECK(!signal_parse_operator("+QSPN: \"X\",\"Y\",\"Z\",2,\"46015\"",&s));
    char long_name[257],line[1024];
    for(unsigned i=0;i<64;++i)memcpy(long_name+i*4,"0041",4);
    long_name[256]=0;
    (void)snprintf(line,sizeof line,"+QSPN: \"\",\"\",\"%s\",1,\"46015\"",long_name);
    CHECK(signal_parse_operator(line,&s) && strlen(s.operator_name)==63 && !strcmp(s.operator_name+60,"..."));
    CHECK(phone_utf8_valid(s.operator_name,sizeof s.operator_name));
    s.present=1;
    CHECK(signal_lines(&s,19)>=9 && signal_visible(0)==4 && signal_visible(1)==6);
    CHECK(signal_line(&s,24,1,out,sizeof out) && !strcmp(out,"SINR:34.5 dB"));
    app_model_t m;app_model_init(&m);
    phone_navigate(&m.phone,&m.page,APP_PAGE_SIGNAL_ENTRY);
    CHECK(m.phone.top_page==PHONE_TOP_SIGNAL && !m.phone.history_count);
    CHECK(!phone_resolve_key(&m.phone,&m.page,PHONE_KEY_SELECT));
    CHECK(m.page==APP_PAGE_SIGNAL_INFO && m.phone.signal.refresh_requested && m.phone.history_count==1);
    m.phone.signal.refresh_requested=0;m.phone.signal.sample=s;
    for(unsigned rotation=0;rotation<4;++rotation) {
        m.phone.signal_rotation=(uint8_t)rotation;m.phone.signal.scroll=0;
        unsigned maximum=signal_lines(&s,phone_message_units(rotation))-signal_visible(rotation);
        for(unsigned i=0;i<maximum;++i)phone_resolve_key(&m.phone,&m.page,PHONE_KEY_DOWN);
        CHECK(m.phone.signal.scroll==maximum && !m.phone.focus_bar);
        phone_resolve_key(&m.phone,&m.page,PHONE_KEY_DOWN);CHECK(m.phone.focus_bar && !m.phone.nav_selected);
        phone_resolve_key(&m.phone,&m.page,PHONE_KEY_UP);CHECK(!m.phone.focus_bar && m.phone.signal.scroll==maximum);
    }
    uint8_t action=phone_resolve_key(&m.phone,&m.page,PHONE_KEY_SELECT);
    CHECK(action==PHONE_KEY_SIGNAL_REFRESH);phone_intent_t intent;
    CHECK(!phone_intent_from_key(&m.phone,m.page,action,&intent));
    phone_event_t event={.kind=PHONE_EVENT_ACTION,.value.key=action};CHECK(phone_apply(&m.phone,&m.page,0,&event,0));
    CHECK(m.phone.signal.refresh_requested);
    m.phone.signal.refresh_requested=0;phone_navigate(&m.phone,&m.page,APP_PAGE_CALL);
    phone_resolve_key(&m.phone,&m.page,PHONE_KEY_BACK);
    CHECK(m.page==APP_PAGE_SIGNAL_INFO && !m.phone.signal.refresh_requested && m.phone.signal.scroll>0);
    phone_resolve_key(&m.phone,&m.page,PHONE_KEY_HOME);CHECK(m.page==APP_PAGE_HOME && !m.phone.history_count);
    CHECK(app_command_parse("page signal").page==APP_PAGE_SIGNAL_ENTRY);
    CHECK(app_command_parse("page signalinfo").page==APP_PAGE_SIGNAL_INFO);
    CHECK(app_command_parse("signal refresh").phone.value.key==PHONE_KEY_SIGNAL_REFRESH);
    CHECK(app_command_parse("signal status").kind==APP_CMD_SIGNAL_STATUS);
    test_finish(0);
}
