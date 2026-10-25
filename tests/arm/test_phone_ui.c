#include "common.h"
#include <string.h>
#include <stdio.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/phone_modem.c"
#include "../../firmware/app/app_logic.c"
void test_main(void)
{
    CHECK(phone_is_family_number(CFG_GUARDIAN_PHONE_NUMBER));
    char alias[40];snprintf(alias,sizeof alias,"+86%s",CFG_GUARDIAN_PHONE_NUMBER);CHECK(phone_is_family_number(alias));
    snprintf(alias,sizeof alias,"0086%s",CFG_GUARDIAN_PHONE_NUMBER);CHECK(phone_is_family_number(alias));
    CHECK(!phone_is_family_number("12345678902") && !phone_is_family_number(NULL) && !phone_is_family_number(""));
    uint32_t utc;phone_datetime_t date;
    CHECK(phone_time_parse("2026-10-24 16:30:45",&utc));
    CHECK(phone_time_to_beijing(utc,&date) && date.hour==16 && date.minute==30 && date.second==45);
    CHECK(!phone_time_parse("2026-02-29 12:00",&utc));CHECK(!phone_time_parse("2026-10-24 24:00",&utc));
    CHECK(!phone_time_parse("2026-10-24 12:00:60",&utc));
    phone_clock_t clock={0};CHECK(!phone_clock_read(&clock,0,&date));
    CHECK(phone_time_parse("2026-10-24 23:59:59",&utc));CHECK(phone_clock_set(&clock,utc,0xfffffff0ull,PHONE_SOURCE_HOST));
    CHECK(phone_clock_read(&clock,0x1000003d8ull,&date) && date.day==25 && !date.hour && !date.minute && !date.second);
    CHECK(phone_utf8_valid("中文 A\n短信",40));CHECK(!phone_utf8_valid("\xc0\xaf",3));
    char text[32];CHECK(phone_wrap_line("中文AB",4,text,sizeof text)==6 && !strcmp(text,"中文"));
    phone_event_t e;
    CHECK(phone_modem_clock("+CCLK: \"26/10/24,16:30:45+32\"",&e));
    CHECK(e.source==PHONE_SOURCE_MODEM && e.kind==PHONE_EVENT_CLOCK);
    CHECK(phone_time_to_beijing(e.value.utc,&date) && date.hour==16 && date.second==45);
    CHECK(phone_modem_clock("+CCLK: \"26/10/24,03:00:00-20\"",&e));
    CHECK(phone_time_to_beijing(e.value.utc,&date) && date.hour==16);
    CHECK(!phone_modem_clock("+CCLK: \"00/01/01,00:00:00+00\"",&e));
    CHECK(!phone_modem_clock("+CCLK: \"26/02/30,16:30:00+32\"",&e));
    CHECK(!phone_modem_clock("+CCLK: \"26/10/24,16:30:00+99\"",&e));
    CHECK(!phone_modem_clock("+CCLK: \"26/10/24,16:30:00+32\"junk",&e));
    CHECK(phone_modem_clock("+QLTS: \"2026/10/24,16:30:45+32,0\"",&e));
    CHECK(phone_time_to_beijing(e.value.utc,&date) && date.hour==16 && date.second==45);
    CHECK(!phone_modem_clock("+QLTS: \"\"",&e));
    CHECK(!phone_modem_clock("+QLTS: \"2026/02/30,16:30:45+32,0\"",&e));
    CHECK(!phone_modem_clock("+QLTS: \"2026/10/24,16:30:45-49,0\"",&e));
    CHECK(phone_modem_qeng("+QENG: \"servingcell\",\"NOCONN\",\"LTE\",\"FDD\",460,01,5A23565,255,3745,8,3,3,DE10,-90,-16,-60,42,25",&e));
    phone_state_t p;phone_state_init(&p);uint8_t page=APP_PAGE_HOME;
    CHECK(phone_apply(&p,&page,0,&e,100) && phone_signal_bars(&p.network)==4);
    phone_event_t correction={.kind=PHONE_EVENT_CLOCK,.source=PHONE_SOURCE_HOST};
    CHECK(phone_time_parse("2026-10-24 16:30:45",&correction.value.utc));
    CHECK(phone_apply(&p,&page,0,&correction,100));
    correction.source=PHONE_SOURCE_MODEM;correction.value.utc-=28800u;
    CHECK(!phone_apply(&p,&page,0,&correction,200));
    CHECK(phone_clock_read(&p.clock,200,&date) && date.hour==16);
    phone_tick(&p,60101);CHECK(!p.network.signal_valid);
    phone_intent_t intent;CHECK(phone_intent_from_key(&p,page,PHONE_KEY_SELECT,&intent)==0);
    page=APP_PAGE_CONTACT_ENTRY;CHECK(phone_intent_from_key(&p,page,PHONE_KEY_SELECT,&intent)==0);
    page=APP_PAGE_CONTACTS;CHECK(phone_intent_from_key(&p,page,PHONE_KEY_SELECT,&intent)==1);
    CHECK(intent.kind==PHONE_INTENT_DIAL && !strcmp(intent.number,CFG_GUARDIAN_PHONE_NUMBER));
    p.pending=1;CHECK(phone_intent_from_key(&p,page,PHONE_KEY_CALL,&intent)==-1);p.pending=0;
    phone_resolve_key(&p,&page,PHONE_KEY_HOME);
    phone_event_t dialing={.kind=PHONE_EVENT_CALL,.source=PHONE_SOURCE_MODEM,.value.call={.state=PHONE_CALL_DIALING}};
    CHECK(phone_apply(&p,&page,0,&dialing,0) && page==APP_PAGE_HOME);
    e=(phone_event_t){.kind=PHONE_EVENT_CALL,.source=PHONE_SOURCE_MODEM,.value.call={.state=PHONE_CALL_INCOMING}};
    CHECK(phone_apply(&p,&page,0,&e,0) && page==APP_PAGE_INCOMING);
    CHECK(phone_intent_from_key(&p,page,PHONE_KEY_SELECT,&intent)==1 && intent.kind==PHONE_INTENT_ANSWER);
    p.action_selected=1;CHECK(phone_intent_from_key(&p,page,PHONE_KEY_SELECT,&intent)==1 && intent.kind==PHONE_INTENT_REJECT);
    CHECK(phone_intent_from_key(&p,page,PHONE_KEY_BACK,&intent)==0);
    p.pending=1;p.pending_kind=PHONE_INTENT_ANSWER;p.pending_id=7;e.value.call.state=PHONE_CALL_ACTIVE;
    CHECK(phone_apply(&p,&page,0,&e,100) && p.pending && p.call.connected_ms==100);
    phone_event_t completed={.kind=PHONE_EVENT_VOICE_RESULT,.source=PHONE_SOURCE_MODEM,
        .value.result={.request_id=7,.note=PHONE_NOTE_NONE}};
    CHECK(phone_apply(&p,&page,0,&completed,100) && !p.pending);
    CHECK(phone_intent_from_key(&p,page,PHONE_KEY_END,&intent)==1 && intent.kind==PHONE_INTENT_HANGUP);
    phone_resolve_key(&p,&page,PHONE_KEY_HOME);CHECK(page==APP_PAGE_HOME && p.call.state==PHONE_CALL_ACTIVE);
    e.value.call.state=PHONE_CALL_ENDED;CHECK(phone_apply(&p,&page,0,&e,2100) && page==APP_PAGE_HOME && phone_call_seconds(&p,9000)==2);
    phone_event_t sms={.kind=PHONE_EVENT_SMS,.source=PHONE_SOURCE_MODEM,.value.sms={.id=1,.sender="13800138000",.text="短信接口的离线夹具"}};
    CHECK(phone_apply(&p,&page,0,&sms,0) && p.message_count==1 && p.inbox[0].unread);
    p.selected_message=0;p.message_scroll=3;p.list_scroll=0;
    CHECK(phone_apply(&p,&page,0,&sms,0) && p.message_scroll==3);
    strcpy(sms.value.sms.text,"内容发生变化");CHECK(phone_apply(&p,&page,0,&sms,0) && !p.message_scroll);
    for(unsigned i=2;i<=PHONE_SMS_SLOTS;++i) {sms.value.sms.id=i;CHECK(phone_apply(&p,&page,0,&sms,0));}
    sms.value.sms.id=99;CHECK(phone_apply(&p,&page,0,&sms,0) && p.note==PHONE_NOTE_FULL && p.message_count==PHONE_SMS_SLOTS);
    CHECK(app_command_parse("clock 2026-10-24 16:30:45").kind==APP_CMD_PHONE);
    CHECK(app_command_parse("clock 2026-10-24 25:00").kind==APP_CMD_INVALID);test_finish(0);
}
