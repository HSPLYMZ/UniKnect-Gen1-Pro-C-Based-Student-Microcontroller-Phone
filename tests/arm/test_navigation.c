#include "common.h"
#include <string.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/app_logic.c"
static phone_state_t p;
static uint8_t page;
static unsigned submitted;
static void press(uint8_t key)
{
    key=phone_resolve_key(&p,&page,key);if(!key) return;
    phone_intent_t intent;int result=phone_intent_from_key(&p,page,key,&intent);
    if(result>0) {++submitted;p.pending=1;p.pending_kind=intent.kind;}
    else if(!result) {
        phone_event_t e={.kind=PHONE_EVENT_ACTION,.value.key=key};CHECK(phone_apply(&p,&page,0,&e,0));
    }
}
static void bar(unsigned index)
{
    CHECK(!p.focus_bar);press(PHONE_KEY_DOWN);CHECK(p.focus_bar && p.nav_selected==0);
    for(unsigned i=0;i<index;++i) press(PHONE_KEY_RIGHT);CHECK(p.nav_selected==index);
}
void test_main(void)
{
    app_model_t boot;app_model_init(&boot);p=boot.phone;page=boot.page;
    CHECK(page==APP_PAGE_HOME && p.top_page==PHONE_TOP_CLOCK && !p.focus_bar && !p.history_count);
    CHECK(!boot.rotation_auto && boot.rotation==0 && !p.demo);
    phone_state_t history=boot.phone;uint8_t history_page=APP_PAGE_HOME;
    for(unsigned i=0;i<10;++i) {
        history.selected_row=(uint8_t)i;history.message_scroll=(uint8_t)i;
        phone_navigate(&history,&history_page,i&1u?APP_PAGE_MESSAGE:APP_PAGE_CALL);
    }
    CHECK(history.history_count==8 && history.history[0].row==2);
    phone_resolve_key(&history,&history_page,PHONE_KEY_BACK);
    CHECK(history.history_count==7 && history.selected_row==9 && history.message_scroll==9 && !history.focus_bar);
    CHECK(app_command_parse("demo on").kind==APP_CMD_INVALID);
    CHECK(app_command_parse("number 123").kind==APP_CMD_INVALID);
    press(PHONE_KEY_SELECT);CHECK(page==APP_PAGE_HOME && !submitted);
    const uint8_t right[]={APP_PAGE_CONTACT_ENTRY,APP_PAGE_SMS_ENTRY,APP_PAGE_SIGNAL_ENTRY,APP_PAGE_SETTINGS_ENTRY,APP_PAGE_HOME};
    for(unsigned i=0;i<PHONE_TOP_COUNT;++i) {press(PHONE_KEY_RIGHT);CHECK(page==right[i] && !p.focus_bar && !p.history_count);}
    const uint8_t left[]={APP_PAGE_SETTINGS_ENTRY,APP_PAGE_SIGNAL_ENTRY,APP_PAGE_SMS_ENTRY,APP_PAGE_CONTACT_ENTRY,APP_PAGE_HOME};
    for(unsigned i=0;i<PHONE_TOP_COUNT;++i) {press(PHONE_KEY_LEFT);CHECK(page==left[i] && !p.history_count);}
    const uint8_t tops[]={APP_PAGE_HOME,APP_PAGE_CONTACT_ENTRY,APP_PAGE_SMS_ENTRY,APP_PAGE_SIGNAL_ENTRY,APP_PAGE_SETTINGS_ENTRY};
    for(unsigned i=0;i<PHONE_TOP_COUNT;++i) {
        phone_navigate(&p,&page,tops[i]);press(PHONE_KEY_UP);CHECK(page==tops[i] && !p.focus_bar);
        bar(0);press(PHONE_KEY_LEFT);CHECK(p.nav_selected==0 && page==tops[i]);
        for(unsigned j=0;j<8;++j) press(PHONE_KEY_RIGHT);
        CHECK(p.nav_selected==2 && page==tops[i]);press(PHONE_KEY_DOWN);CHECK(p.focus_bar && page==tops[i]);
        press(PHONE_KEY_UP);CHECK(!p.focus_bar && page==tops[i]);
    }
    phone_navigate(&p,&page,APP_PAGE_CONTACT_ENTRY);press(PHONE_KEY_SELECT);
    CHECK(page==APP_PAGE_CONTACTS && p.history_count==1 && !submitted);
    press(PHONE_KEY_BACK);CHECK(page==APP_PAGE_CONTACT_ENTRY && !p.history_count);
    bar(0);press(PHONE_KEY_SELECT);CHECK(page==APP_PAGE_CONTACTS && p.history_count==1 && !submitted && !p.focus_bar);
    bar(0);press(PHONE_KEY_UP);CHECK(page==APP_PAGE_CONTACTS && p.selected_row==0);
    press(PHONE_KEY_LEFT);press(PHONE_KEY_RIGHT);CHECK(page==APP_PAGE_CONTACTS);
    press(PHONE_KEY_SELECT);press(PHONE_KEY_SELECT);CHECK(submitted==1 && p.pending_kind==PHONE_INTENT_DIAL);
    p.pending=0;p.call.state=PHONE_CALL_ACTIVE;
    press(PHONE_KEY_HOME);CHECK(page==APP_PAGE_HOME && p.call.state==PHONE_CALL_ACTIVE && !p.history_count);
    phone_navigate(&p,&page,APP_PAGE_CONTACT_ENTRY);press(PHONE_KEY_SELECT);press(PHONE_KEY_SELECT);
    CHECK(page==APP_PAGE_CALL && submitted==1);
    press(PHONE_KEY_BACK);CHECK(page==APP_PAGE_CONTACTS && p.call.state==PHONE_CALL_ACTIVE && submitted==1);
    press(PHONE_KEY_HOME);p.call.state=PHONE_CALL_IDLE;
    phone_navigate(&p,&page,APP_PAGE_SMS_ENTRY);press(PHONE_KEY_SELECT);
    CHECK(page==APP_PAGE_INBOX && !p.message_count && submitted==1);
    bar(0);press(PHONE_KEY_SELECT);CHECK(page==APP_PAGE_INBOX && p.focus_bar);
    press(PHONE_KEY_UP);
    press(PHONE_KEY_BACK);CHECK(page==APP_PAGE_SMS_ENTRY);
    bar(0);press(PHONE_KEY_SELECT);CHECK(page==APP_PAGE_INBOX && !p.focus_bar);
    p.message_count=4;
    for(unsigned i=0;i<4;++i) {
        p.inbox[i].id=i+1;strcpy(p.inbox[i].sender,"实际接口夹具");
        strcpy(p.inbox[i].text,"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    }
    press(PHONE_KEY_UP);CHECK(p.selected_message==0 && !p.focus_bar);
    for(unsigned i=0;i<3;++i) press(PHONE_KEY_DOWN);
    CHECK(p.selected_message==3 && p.list_scroll==1 && !p.focus_bar);
    press(PHONE_KEY_LEFT);press(PHONE_KEY_RIGHT);CHECK(page==APP_PAGE_INBOX && p.selected_message==3);
    bar(0);press(PHONE_KEY_UP);CHECK(p.selected_message==3 && p.list_scroll==1);
    press(PHONE_KEY_SELECT);CHECK(page==APP_PAGE_MESSAGE && !p.message_scroll && p.history_count==2);
    unsigned maximum=phone_wrapped_lines(p.inbox[3].text,phone_message_units(0))-phone_message_visible(0);
    for(unsigned i=0;i<maximum;++i) press(PHONE_KEY_DOWN);
    CHECK(p.message_scroll==maximum && !p.focus_bar);
    bar(0);press(PHONE_KEY_UP);CHECK(p.message_scroll==maximum && page==APP_PAGE_MESSAGE);
    press(PHONE_KEY_BACK);CHECK(page==APP_PAGE_INBOX && p.selected_message==3 && p.list_scroll==1);
    bar(0);press(PHONE_KEY_SELECT);CHECK(page==APP_PAGE_MESSAGE);
    press(PHONE_KEY_HOME);CHECK(page==APP_PAGE_HOME && !p.history_count && !p.message_scroll && !p.selected_message);
    for(unsigned i=0;i<3;++i) {
        phone_navigate(&p,&page,tops[i]);bar(1);press(PHONE_KEY_SELECT);
        CHECK(page==APP_PAGE_HOME && !p.focus_bar && !p.history_count);
        phone_navigate(&p,&page,tops[i]);bar(2);press(PHONE_KEY_SELECT);
        CHECK(page==APP_PAGE_HOME && !p.focus_bar && !p.history_count);
    }
    phone_navigate(&p,&page,APP_PAGE_SMS_ENTRY);press(PHONE_KEY_SELECT);
    for(unsigned i=0;i<3;++i) press(PHONE_KEY_DOWN);
    bar(1);press(PHONE_KEY_SELECT);
    CHECK(page==APP_PAGE_HOME && !p.history_count);
    phone_event_t incoming={.kind=PHONE_EVENT_CALL,.source=PHONE_SOURCE_MODEM,.value.call={.state=PHONE_CALL_INCOMING}};
    CHECK(phone_apply(&p,&page,0,&incoming,0) && page==APP_PAGE_INCOMING);
    press(PHONE_KEY_HOME);CHECK(p.call.state==PHONE_CALL_INCOMING && submitted==1);
    phone_navigate(&p,&page,APP_PAGE_INCOMING);press(PHONE_KEY_DOWN);press(PHONE_KEY_SELECT);
    CHECK(submitted==2 && p.pending_kind==PHONE_INTENT_REJECT);
    p.pending=0;p.navigation_locked=1;phone_state_t old=p;
    press(PHONE_KEY_SELECT);press(PHONE_KEY_RIGHT);CHECK(!memcmp(&old,&p,sizeof p));
    CHECK(app_command_parse("key home").phone.source==PHONE_SOURCE_HOST);
    CHECK(app_command_parse("page contact").page==APP_PAGE_CONTACT_ENTRY);
    CHECK(app_command_parse("page sms").page==APP_PAGE_SMS_ENTRY);
    CHECK(app_command_parse("page contacts").page==APP_PAGE_CONTACTS);
    /* Production calibration lifecycle is covered in test_settings. */
    /* Explicit HOST commands bypass top navigation, including bottom focus. */
    p=boot.phone;p.call.state=PHONE_CALL_ACTIVE;p.call.generation=4;
    for(unsigned i=0;i<3;++i) {
        phone_navigate(&p,&page,tops[i]);p.focus_bar=1;
        uint8_t action=phone_route_key(&p,&page,PHONE_KEY_END,PHONE_SOURCE_HOST);
        phone_intent_t intent;
        CHECK(action==PHONE_KEY_END && phone_intent_from_key(&p,page,action,&intent)==1 &&
              intent.kind==PHONE_INTENT_HANGUP && intent.call_generation==4);
        p.pending=1;CHECK(phone_intent_from_key(&p,page,action,&intent)==-1);p.pending=0;
        CHECK(phone_route_key(&p,&page,PHONE_KEY_DELETE,PHONE_SOURCE_HOST)==PHONE_KEY_DELETE);
        CHECK(!phone_route_key(&p,&page,PHONE_KEY_RIGHT,PHONE_SOURCE_UI) && page==tops[i]);
    }
    p.navigation_locked=1;CHECK(!phone_route_key(&p,&page,PHONE_KEY_END,PHONE_SOURCE_HOST));
    p=boot.phone;page=APP_PAGE_INCOMING;p.call.state=PHONE_CALL_ENDED;p.call.generation=1;p.action_selected=1;
    incoming.value.call.generation=2;
    CHECK(phone_apply(&p,&page,0,&incoming,10) && !p.action_selected && p.call.generation==2);
    p.action_selected=1;
    CHECK(phone_apply(&p,&page,0,&incoming,20) && p.action_selected==1);
    CHECK(phone_route_key(&p,&page,PHONE_KEY_CALL,PHONE_SOURCE_HOST)==PHONE_KEY_CALL);
    test_finish(0);
}
