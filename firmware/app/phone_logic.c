#include "app_logic.h"
#include "app_config.h"
#include <string.h>

_Static_assert(sizeof CFG_GUARDIAN_PHONE_NUMBER - 1u <= PHONE_NUMBER_MAX,
               "Guardian phone number exceeds the dial buffer");

uint32_t phone_utf8_next(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    if (!*s) return 0;
    uint32_t cp; unsigned count;
    if (*s < 128) { *text += 1; return *s; }
    if (*s >= 0xc2 && *s <= 0xdf) { cp=*s&31; count=2; }
    else if (*s >= 0xe0 && *s <= 0xef) { cp=*s&15; count=3; }
    else if (*s >= 0xf0 && *s <= 0xf4) { cp=*s&7; count=4; }
    else { *text += 1; return 0xfffd; }
    for (unsigned i=1;i<count;++i) {
        if (!s[i] || (s[i]&0xc0)!=0x80) { *text+=1; return 0xfffd; }
        cp=(cp<<6)|(s[i]&63);
    }
    if ((count==3 && cp<0x800) || (count==4 && cp<0x10000) ||
        (cp>=0xd800 && cp<=0xdfff) || cp>0x10ffff) { *text+=1; return 0xfffd; }
    *text += count;
    return cp;
}
int phone_utf8_valid(const char *text, size_t capacity)
{
    if (!text || !memchr(text,0,capacity)) return 0;
    const char *p=text;
    while (*p) {
        uint32_t cp=phone_utf8_next(&p);
        if (cp==0xfffd || (cp<32 && cp!='\n' && cp!='\r' && cp!='\t')) return 0;
    }
    return 1;
}
size_t phone_wrap_line(const char *text, unsigned units, char *out, size_t capacity)
{
    const char *p=text;
    size_t written=0; unsigned width=0;
    if (!capacity || !units) return 0;
    while (*p) {
        const char *begin=p;
        uint32_t cp=phone_utf8_next(&p);
        if (cp=='\r') continue;
        if (cp=='\n') break;
        unsigned char_width=cp<128 ? 1u : 2u;
        size_t n=(size_t)(p-begin);
        if (width+char_width>units || written+n>=capacity) { p=begin; break; }
        memcpy(out+written,begin,n); written+=n; width+=char_width;
    }
    out[written]=0;
    return (size_t)(p-text);
}
unsigned phone_wrapped_lines(const char *text, unsigned units)
{
    char line[PHONE_SMS_TEXT_MAX+1]; unsigned count=0;
    while (*text) {
        size_t n=phone_wrap_line(text,units,line,sizeof line);
        if (!n) break;
        text+=n; ++count;
    }
    return count;
}
static int valid_number(const char *s, int empty_ok)
{
    if (!memchr(s,0,PHONE_NUMBER_MAX+1)) return 0;
    unsigned digits=0;
    for (unsigned i=0;s[i];++i) {
        if (s[i]>='0' && s[i]<='9') ++digits;
        else if (s[i]!='*' && s[i]!='#' && !(s[i]=='+' && i==0)) return 0;
    }
    return digits || (empty_ok && !*s);
}
static int valid_voice_number(const char *s)
{
    if(!s || !memchr(s,0,PHONE_NUMBER_MAX+1)) return 0;
    unsigned digits=0;
    for(unsigned i=0;s[i];++i) {
        if(s[i]>='0' && s[i]<='9') ++digits;
        else if(s[i]!='+' || i!=0) return 0;
    }
    return digits>0;
}
static int call_running(const phone_state_t *p)
{ return p->call.state!=PHONE_CALL_IDLE && p->call.state!=PHONE_CALL_ENDED; }
int phone_top_index(uint8_t page)
{
    return page==APP_PAGE_HOME?PHONE_TOP_CLOCK:page==APP_PAGE_CONTACT_ENTRY?PHONE_TOP_CONTACT:
        page==APP_PAGE_SMS_ENTRY?PHONE_TOP_SMS:page==APP_PAGE_SIGNAL_ENTRY?PHONE_TOP_SIGNAL:page==APP_PAGE_SETTINGS_ENTRY?PHONE_TOP_SETTINGS:-1;
}
static const uint8_t top_pages[PHONE_TOP_COUNT]={APP_PAGE_HOME,APP_PAGE_CONTACT_ENTRY,APP_PAGE_SMS_ENTRY,APP_PAGE_SIGNAL_ENTRY,APP_PAGE_SETTINGS_ENTRY};
uint8_t phone_top_page(unsigned index) {return top_pages[index%PHONE_TOP_COUNT];}
unsigned phone_message_units(unsigned rotation)
{ return ((rotation&1u)?128u:160u)/6u-2u; }
unsigned phone_message_visible(unsigned rotation)
{ return (((rotation&1u)?160u:128u)-PHONE_NAV_H-62u)/14u; }
unsigned phone_contact_count(void) { return valid_voice_number(CFG_GUARDIAN_PHONE_NUMBER)?1u:0u; }
int phone_is_family_number(const char *number)
{
    if(!phone_contact_count() || !number || !*number)return 0;
    const char *a=number,*b=CFG_GUARDIAN_PHONE_NUMBER;
    if(*a=='+')++a;else if(!strncmp(a,"00",2))a+=2;
    if(*b=='+')++b;else if(!strncmp(b,"00",2))b+=2;
    /* The EC200U CN may report the configured mobile number with +86/0086. */
    if(strlen(a)==13 && !strncmp(a,"86",2) && strlen(b)==11)a+=2;
    if(strlen(b)==13 && !strncmp(b,"86",2) && strlen(a)==11)b+=2;
    return !strcmp(a,b);
}
static phone_nav_entry_t nav_snapshot(const phone_state_t *p,uint8_t page)
{
    return (phone_nav_entry_t){page,p->home_selected,p->menu_selected,p->keypad_selected,
        p->selected_message,p->message_scroll,p->action_selected,p->diagnostic_scroll,
        p->top_page,p->selected_row,p->list_scroll,p->signal.scroll};
}
static void nav_push(phone_state_t *p,phone_nav_entry_t e)
{
    if(p->history_count==PHONE_HISTORY_MAX) {
        memmove(p->history,p->history+1,(PHONE_HISTORY_MAX-1u)*sizeof p->history[0]); --p->history_count;
    }
    p->history[p->history_count++]=e;
}
static void nav_top(phone_state_t *p,uint8_t *page,unsigned top)
{
    *page=top_pages[top]; p->top_page=(uint8_t)top; p->top_position=(int16_t)top;
    p->focus_bar=p->nav_selected=0; p->history_count=0;
    p->selected_row=p->list_scroll=p->message_scroll=0;
    p->selected_message=p->action_selected=0;
    p->signal.scroll=0;
    p->note=PHONE_NOTE_NONE;
}
static void nav_back(phone_state_t *p,uint8_t *page)
{
    if(phone_top_index(*page)>=0 || !p->history_count) { nav_top(p,page,PHONE_TOP_CLOCK); return; }
    phone_nav_entry_t e=p->history[--p->history_count];
    *page=e.page; p->home_selected=e.home; p->menu_selected=e.menu; p->keypad_selected=e.keypad;
    p->selected_message=e.message<p->message_count?e.message:0;
    p->message_scroll=e.scroll; p->action_selected=e.action; p->diagnostic_scroll=e.diagnostic;
    p->top_page=e.top; p->selected_row=e.row; p->list_scroll=e.list;
    p->signal.scroll=e.signal_scroll;
    if(*page==APP_PAGE_SIGNAL_INFO) {
        unsigned lines=signal_lines(&p->signal.sample,phone_message_units(p->signal_rotation));
        unsigned visible=signal_visible(p->signal_rotation),maximum=lines>visible?lines-visible:0;
        if(p->signal.scroll>maximum)p->signal.scroll=(uint8_t)maximum;
    }
    if(*page==APP_PAGE_MESSAGE) {
        unsigned n=p->selected_message<p->message_count?phone_wrapped_lines(p->inbox[p->selected_message].text,phone_message_units(p->signal_rotation)):0;
        unsigned visible=phone_message_visible(p->signal_rotation),limit=n>visible?n-visible:0;
        if(p->message_scroll>limit)p->message_scroll=(uint8_t)limit;
    }
    if(phone_top_index(*page)>=0) p->top_position=p->top_page;
    p->focus_bar=p->nav_selected=0; p->note=PHONE_NOTE_NONE;
}
void phone_navigate(phone_state_t *p,uint8_t *page,uint8_t target)
{
    if(target>=APP_PAGE_COUNT) return;
    int top=phone_top_index(target);
    if(top>=0) { nav_top(p,page,(unsigned)top); return; }
    if(target==APP_PAGE_SETTINGS_MENU && *page!=APP_PAGE_SETTINGS_ENTRY)nav_top(p,page,PHONE_TOP_SETTINGS);
    if((target==APP_PAGE_ROTATION || target==APP_PAGE_CAL_CONFIRM) && *page!=APP_PAGE_SETTINGS_MENU)
        phone_navigate(p,page,APP_PAGE_SETTINGS_MENU);
    if(target==APP_PAGE_CONTACTS && *page!=APP_PAGE_CONTACT_ENTRY)
        nav_top(p,page,PHONE_TOP_CONTACT);
    if(target==APP_PAGE_INBOX && *page!=APP_PAGE_SMS_ENTRY)
        nav_top(p,page,PHONE_TOP_SMS);
    if(target==APP_PAGE_SIGNAL_INFO && *page!=APP_PAGE_SIGNAL_ENTRY)
        nav_top(p,page,PHONE_TOP_SIGNAL);
    if(*page!=target) {
        nav_push(p,nav_snapshot(p,*page)); *page=target;
        p->selected_row=p->list_scroll=p->action_selected=0;
        if(target==APP_PAGE_ROTATION)p->selected_row=p->signal_rotation;
        if(target==APP_PAGE_INBOX) p->selected_message=0;
        if(target==APP_PAGE_MESSAGE) p->message_scroll=0;
        if(target==APP_PAGE_SIGNAL_INFO) {p->signal.scroll=0;p->signal.refresh_requested=1;}
    }
    p->focus_bar=p->nav_selected=0;
}
int phone_can_select(const phone_state_t *p,uint8_t page)
{
    if(p->navigation_locked) return 0;
    if(page==APP_PAGE_HOME || page==APP_PAGE_MESSAGE) return 0;
    if(page==APP_PAGE_CONTACT_ENTRY || page==APP_PAGE_SMS_ENTRY) return 1;
    if(page==APP_PAGE_SIGNAL_ENTRY || page==APP_PAGE_SETTINGS_ENTRY || page==APP_PAGE_SETTINGS_MENU || page==APP_PAGE_ROTATION) return 1;
    if(page==APP_PAGE_CAL_CONFIRM)return (p->system.save_state>=SETTINGS_SAVE_FAILED && !p->selected_row) || (!p->pending && !call_running(p));
    if(page==APP_PAGE_SIGNAL_INFO) return !p->signal.request_id;
    if(page==APP_PAGE_CONTACTS) return phone_contact_count() && (!p->pending || call_running(p));
    if(page==APP_PAGE_INBOX) return p->message_count>0;
    if(page==APP_PAGE_CALL || page==APP_PAGE_INCOMING) return !p->pending;
    return 0;
}
/* One confirmation path for the content center key and the bottom Select. */
int phone_confirm(phone_state_t *p,uint8_t *page)
{
    if(!phone_can_select(p,*page)) return 0;
    if(*page==APP_PAGE_CONTACT_ENTRY) { phone_navigate(p,page,APP_PAGE_CONTACTS); return 0; }
    if(*page==APP_PAGE_SMS_ENTRY) { phone_navigate(p,page,APP_PAGE_INBOX); return 0; }
    if(*page==APP_PAGE_SETTINGS_ENTRY) {phone_navigate(p,page,APP_PAGE_SETTINGS_MENU);return 0;}
    if(*page==APP_PAGE_SETTINGS_MENU) {phone_navigate(p,page,p->selected_row?APP_PAGE_CAL_CONFIRM:APP_PAGE_ROTATION);return 0;}
    if(*page==APP_PAGE_ROTATION)return PHONE_KEY_SETTINGS_ROTATE;
    if(*page==APP_PAGE_CAL_CONFIRM)return (p->system.save_state>=SETTINGS_SAVE_FAILED && !p->selected_row)?PHONE_KEY_SETTINGS_RETRY:PHONE_KEY_CAL_START;
    if(*page==APP_PAGE_SIGNAL_ENTRY) {phone_navigate(p,page,APP_PAGE_SIGNAL_INFO);return 0;}
    if(*page==APP_PAGE_SIGNAL_INFO) return PHONE_KEY_SIGNAL_REFRESH;
    if(*page==APP_PAGE_CONTACTS) return call_running(p)?PHONE_KEY_SHOW_CALL:PHONE_KEY_CALL;
    if(*page==APP_PAGE_INBOX) {
        if(p->selected_message>=p->message_count) p->selected_message=0;
        p->inbox[p->selected_message].unread=0;
        phone_navigate(p,page,APP_PAGE_MESSAGE); return 0;
    }
    if(*page==APP_PAGE_CALL || *page==APP_PAGE_INCOMING) {
        if(p->call.state==PHONE_CALL_INCOMING) return p->action_selected?PHONE_KEY_END:PHONE_KEY_CALL;
        if(call_running(p)) return PHONE_KEY_END;
        nav_back(p,page);
    }
    return 0;
}
static void enter_bar(phone_state_t *p) { p->focus_bar=1; p->nav_selected=0; }
uint8_t phone_route_key(phone_state_t *p,uint8_t *page,uint8_t key,uint8_t source)
{
    if(p->navigation_locked) return 0;
    if(source==PHONE_SOURCE_HOST &&
       (key==PHONE_KEY_CALL || key==PHONE_KEY_END || key==PHONE_KEY_DELETE)) return key;
    return phone_resolve_key(p,page,key);
}
uint8_t phone_resolve_key(phone_state_t *p,uint8_t *page,uint8_t key)
{
    if(p->navigation_locked) return 0;
    /* FOCUS is a diagnostic command only; no physical long-press binding. */
    if(key==PHONE_KEY_FOCUS) { if(p->focus_bar) p->focus_bar=0; else enter_bar(p); return 0; }
    if(key==PHONE_KEY_HOME) { nav_top(p,page,PHONE_TOP_CLOCK); return 0; }
    if(key==PHONE_KEY_BACK) { nav_back(p,page); return 0; }
    if(p->focus_bar && key>=PHONE_KEY_UP && key<=PHONE_KEY_SELECT) {
        if(key==PHONE_KEY_UP) p->focus_bar=0;
        else if(key==PHONE_KEY_LEFT && p->nav_selected) --p->nav_selected;
        else if(key==PHONE_KEY_RIGHT && p->nav_selected<2u) ++p->nav_selected;
        else if(key==PHONE_KEY_SELECT) {
            unsigned selected=p->nav_selected;
            if(selected==1u) nav_top(p,page,PHONE_TOP_CLOCK);
            else if(selected==2u) nav_back(p,page);
            else {
                int action=phone_confirm(p,page);
                if(action) p->focus_bar=0;
                return (uint8_t)action;
            }
        }
        return 0;
    }
    if(key==PHONE_KEY_SELECT) return (uint8_t)phone_confirm(p,page);
    int top=phone_top_index(*page);
    if(top>=0) {
        if(key==PHONE_KEY_DOWN) enter_bar(p);
        else if(key==PHONE_KEY_LEFT || key==PHONE_KEY_RIGHT) {
            int delta=key==PHONE_KEY_RIGHT?1:-1;
            p->top_page=(uint8_t)((top+PHONE_TOP_COUNT+delta)%PHONE_TOP_COUNT);
            p->top_position=(int16_t)(p->top_position+delta);
            if(p->top_position>100 || p->top_position<-100) p->top_position=p->top_page;
            *page=top_pages[p->top_page];
        }
        return 0;
    }
    if(key>=PHONE_KEY_UP && key<=PHONE_KEY_RIGHT) {
        unsigned count=0;
        uint8_t *selection=&p->selected_row;
        if(*page==APP_PAGE_SETTINGS_MENU)count=2;
        else if(*page==APP_PAGE_ROTATION)count=4;
        else if(*page==APP_PAGE_CAL_CONFIRM)count=p->system.save_state>=SETTINGS_SAVE_FAILED?2u:1u;
        else if(*page==APP_PAGE_CONTACTS) count=phone_contact_count();
        else if(*page==APP_PAGE_INBOX) { count=p->message_count; selection=&p->selected_message; }
        else if(*page==APP_PAGE_CALL || *page==APP_PAGE_INCOMING) {
            count=p->call.state==PHONE_CALL_INCOMING?2u:1u; selection=&p->action_selected;
        } else if(*page==APP_PAGE_MESSAGE) {
            unsigned lines=p->message_count?phone_wrapped_lines(p->inbox[p->selected_message].text,phone_message_units(p->signal_rotation)):0;
            unsigned visible=phone_message_visible(p->signal_rotation), maximum=lines>visible?lines-visible:0;
            if(p->message_scroll>maximum) p->message_scroll=(uint8_t)maximum;
            if(key==PHONE_KEY_UP && p->message_scroll) --p->message_scroll;
            else if(key==PHONE_KEY_DOWN) { if(p->message_scroll<maximum) ++p->message_scroll; else enter_bar(p); }
            return 0;
        } else if(*page==APP_PAGE_SIGNAL_INFO) {
            unsigned rotation=p->signal_rotation;
            unsigned lines=signal_lines(&p->signal.sample,phone_message_units(rotation));
            unsigned visible=signal_visible(rotation),maximum=lines>visible?lines-visible:0;
            if(p->signal.scroll>maximum)p->signal.scroll=(uint8_t)maximum;
            if(key==PHONE_KEY_UP && p->signal.scroll)--p->signal.scroll;
            else if(key==PHONE_KEY_DOWN) {if(p->signal.scroll<maximum)++p->signal.scroll;else enter_bar(p);}
            return 0;
        }
        if(key==PHONE_KEY_UP && *selection) --*selection;
        else if(key==PHONE_KEY_DOWN) {
            if(count && *selection+1u<count) ++*selection; else enter_bar(p);
        }
        if(*page==APP_PAGE_INBOX) {
            if(*selection<p->list_scroll) p->list_scroll=*selection;
            if(*selection>=p->list_scroll+3u) p->list_scroll=*selection-2u;
        }
        return 0;
    }
    return key; /* explicit host CALL/END etc.; physical keys never create them */
}
int phone_intent_from_key(const phone_state_t *p,uint8_t page,uint8_t key,phone_intent_t *intent)
{
    if(!p || !intent) return 0;
    memset(intent,0,sizeof *intent);
    intent->call_generation=p->call.generation;
    if(p->demo || p->navigation_locked) return 0;
    if(key==PHONE_KEY_SELECT) {
        if(p->focus_bar) return 0;
        if(page==APP_PAGE_CONTACTS && phone_contact_count()) key=call_running(p)?PHONE_KEY_SHOW_CALL:PHONE_KEY_CALL;
        else if(page==APP_PAGE_INCOMING || page==APP_PAGE_CALL)
            key=p->call.state==PHONE_CALL_INCOMING && !p->action_selected?PHONE_KEY_CALL:PHONE_KEY_END;
    }
    if(key!=PHONE_KEY_CALL && key!=PHONE_KEY_END) return 0;
    if(p->pending) return -1;
    if(p->call.state==PHONE_CALL_INCOMING) intent->kind=key==PHONE_KEY_CALL?PHONE_INTENT_ANSWER:PHONE_INTENT_REJECT;
    else if(call_running(p)) { if(key==PHONE_KEY_END) intent->kind=PHONE_INTENT_HANGUP; }
    else if(key==PHONE_KEY_CALL && phone_contact_count()) {
        intent->kind=PHONE_INTENT_DIAL; strcpy(intent->number,CFG_GUARDIAN_PHONE_NUMBER);
    }
    return intent->kind!=PHONE_INTENT_NONE;
}
void phone_state_init(phone_state_t *p) { memset(p,0,sizeof *p);settings_defaults(&p->system.desired);p->system.saved=p->system.desired; }

unsigned phone_signal_bars(const phone_network_t *n)
{
    if (!n->enabled || !n->registered || !n->lte || !n->signal_valid) return 0;
    if (n->rsrp_dbm>=-80) return 5;
    if (n->rsrp_dbm>=-90) return 4;
    if (n->rsrp_dbm>=-100) return 3;
    if (n->rsrp_dbm>=-110) return 2;
    return n->rsrp_dbm>=-120 ? 1 : 0;
}
unsigned phone_call_seconds(const phone_state_t *p, uint64_t now)
{
    if (!p->call.has_connection || (p->call.state!=PHONE_CALL_ACTIVE && p->call.state!=PHONE_CALL_ENDED)) return 0;
    uint64_t end=p->call.state==PHONE_CALL_ENDED ? p->call.changed_ms : now;
    return end>=p->call.connected_ms ? (unsigned)((end-p->call.connected_ms)/1000u) : 0;
}
static void demo_off(phone_state_t *p)
{
    if (!p->demo) return;
    unsigned count=0;
    for (unsigned i=0;i<p->message_count;++i)
        if (p->inbox[i].source!=PHONE_SOURCE_DEMO) p->inbox[count++]=p->inbox[i];
    p->message_count=(uint8_t)count;
    p->selected_message=p->message_scroll=0;
    memset(&p->network,0,sizeof p->network);
    memset(&p->call,0,sizeof p->call);
    if (p->clock.source==PHONE_SOURCE_DEMO) p->clock.valid=0;
    p->demo=0;
}
static void demo_on(phone_state_t *p, uint64_t now)
{
    if (p->demo) return;
    if (p->network.enabled || p->message_count || (p->call.state!=PHONE_CALL_IDLE && p->call.state!=PHONE_CALL_ENDED)) {
        p->note=PHONE_NOTE_DEMO_DENIED; return;
    }
    p->demo=1;
    p->network=(phone_network_t){.updated_ms=now,.rsrp_dbm=-86,.enabled=1,.registered=1,.lte=1,.signal_valid=1};
    if (!p->clock.valid) {
        phone_datetime_t t={2026,10,23,16,30,0}; uint32_t utc;
        phone_time_from_beijing(&t,&utc);
        phone_clock_set(&p->clock,utc,now,PHONE_SOURCE_DEMO);
    }
    static const char *const senders[]={"妈妈","系统通知","Alex"};
    static const char *const messages[]={
        "到家了吗？记得吃饭。到了给我回个电话，周末一起去公园散步。另外，桌上有给你留的水果，出门时别忘了带钥匙。看完短信后可以按下翻继续阅读。",
        "这是本地演示短信。当前没有连接4G模组，不会发送短信或拨打电话。",
        "Meet at 18:30. Bring your STM32 board!"};
    for (unsigned i=0;i<3;++i) {
        phone_sms_t *sms=&p->inbox[i];
        memset(sms,0,sizeof *sms); sms->id=i+1;
        sms->utc=p->clock.utc-300u*(i+1u); sms->source=PHONE_SOURCE_DEMO; sms->unread=i<2;
        strcpy(sms->sender,senders[i]); strcpy(sms->text,messages[i]);
    }
    p->message_count=3;
}
static void connected(phone_state_t *p, uint64_t now)
{
    p->call.state=PHONE_CALL_ACTIVE; p->call.changed_ms=now;
    p->call.connected_ms=now; p->call.has_connection=1;
}
void phone_tick(phone_state_t *p, uint64_t now)
{
    if (p->demo && p->call.state==PHONE_CALL_DIALING && now-p->call.changed_ms>=1500)
        connected(p,now);
    if (!p->demo && p->network.signal_valid && now-p->network.updated_ms>60000u)
        p->network.signal_valid=0;
}
static void end_call(phone_state_t *p, uint64_t now)
{
    if (p->demo) { p->call.state=PHONE_CALL_ENDED; p->call.changed_ms=now; }
    else p->note=PHONE_NOTE_OFFLINE; /* backend intent will be wired after modem bring-up */
}
static void append_digit(phone_state_t *p, uint8_t key)
{
    size_t n=strlen(p->number);
    if (n<PHONE_NUMBER_MAX) { p->number[n]=(char)key; p->number[n+1]=0; }
    else p->note=PHONE_NOTE_FULL;
}
static int apply_key(phone_state_t *p, uint8_t *page, unsigned rotation, uint8_t key, uint64_t now)
{
    if (!((key>='0' && key<='9') || key=='*' || key=='#' ||
          (key>=PHONE_KEY_UP && key<=PHONE_KEY_SIGNAL_REFRESH))) return 0;
    if (((key>='0' && key<='9') || key=='*' || key=='#') && p->call.state==PHONE_CALL_ACTIVE) return 0;
    p->note=PHONE_NOTE_NONE;
    if(key==PHONE_KEY_SIGNAL_REFRESH) {
        if(!p->navigation_locked && !p->signal.request_id)p->signal.refresh_requested=1;
        return 1;
    }
    if(key==PHONE_KEY_SHOW_CALL) {
        phone_navigate(p,page,p->call.state==PHONE_CALL_INCOMING?APP_PAGE_INCOMING:APP_PAGE_CALL); return 1;
    }
    if ((key>='0' && key<='9') || key=='*' || key=='#') {
        *page=APP_PAGE_DIALER; append_digit(p,key); return 1;
    }
    if (key==PHONE_KEY_END) {
        if (p->call.state!=PHONE_CALL_IDLE && p->call.state!=PHONE_CALL_ENDED) { end_call(p,now); *page=APP_PAGE_CALL; }
        else *page=APP_PAGE_HOME;
        return 1;
    }
    if (key==PHONE_KEY_CALL) {
        if (p->call.state==PHONE_CALL_ACTIVE || p->call.state==PHONE_CALL_DIALING) { phone_navigate(p,page,APP_PAGE_CALL); return 1; }
        if (p->call.state==PHONE_CALL_INCOMING) {
            if (p->demo) { connected(p,now); phone_navigate(p,page,APP_PAGE_CALL); }
            else p->note=PHONE_NOTE_OFFLINE;
        } else {
            const char *target=(*page==APP_PAGE_DIALER && *p->number) ?
                p->number : CFG_GUARDIAN_PHONE_NUMBER;
            if((*page==APP_PAGE_DIALER && !*p->number) || !valid_number(target,0)) { p->note=PHONE_NOTE_EMPTY;return 1; }
            if (p->demo) {
                memset(&p->call,0,sizeof p->call); strcpy(p->call.number,target);
                p->call.state=PHONE_CALL_DIALING; p->call.changed_ms=now; phone_navigate(p,page,APP_PAGE_CALL);
            } else p->note=PHONE_NOTE_OFFLINE;
        }
        return 1;
    }
    if (key==PHONE_KEY_MUTE || key==PHONE_KEY_SPEAKER) {
        if (p->demo && p->call.state==PHONE_CALL_ACTIVE) {
            if (key==PHONE_KEY_MUTE) p->call.muted=!p->call.muted;
            else p->call.speaker=!p->call.speaker;
        } else p->note=PHONE_NOTE_OFFLINE;
        return 1;
    }
    if (key==PHONE_KEY_DELETE) {
        if (*page==APP_PAGE_DIALER) {
            size_t n=strlen(p->number);
            if(n) p->number[n-1]=0;
        }
        return 1;
    }
    return 1;
}
static int phone_apply_impl(phone_state_t *p, uint8_t *page, unsigned rotation, const phone_event_t *e, uint64_t now)
{
    if (!p || !page || !e) return 0;
    switch (e->kind) {
    case PHONE_EVENT_KEY:
    case PHONE_EVENT_ACTION: {
        uint8_t key=e->value.key;
        if(e->kind==PHONE_EVENT_KEY) key=phone_route_key(p,page,key,e->source);
        if(!key) return 1;
        return apply_key(p,page,rotation,key,now);
    }
    case PHONE_EVENT_NUMBER:
        if (!valid_number(e->value.number,1)) return 0;
        phone_navigate(p,page,APP_PAGE_DIALER);
        strcpy(p->number,e->value.number); p->note=0; return 1;
    case PHONE_EVENT_CLOCK: {
        /* An explicit host correction stays authoritative until reboot. */
        if(e->source==PHONE_SOURCE_MODEM && p->clock.valid && p->clock.source==PHONE_SOURCE_HOST) return 0;
        phone_clock_t clock;
        if (!phone_clock_set(&clock,e->value.utc,now,e->source)) return 0;
        if (e->source==PHONE_SOURCE_MODEM) demo_off(p);
        p->clock=clock; return 1;
    }
    case PHONE_EVENT_NETWORK:
        if (e->source!=PHONE_SOURCE_MODEM) return 0;
        if (e->value.network.signal_valid && (e->value.network.rsrp_dbm < -140 || e->value.network.rsrp_dbm > -44)) return 0;
        demo_off(p);
        p->network=(phone_network_t){.updated_ms=now,.rsrp_dbm=e->value.network.rsrp_dbm,
            .enabled=!!e->value.network.enabled,.registered=!!e->value.network.registered,
            .lte=!!e->value.network.lte,.signal_valid=!!e->value.network.signal_valid}; return 1;
    case PHONE_EVENT_SMS: {
        if (e->source!=PHONE_SOURCE_MODEM || !e->value.sms.id ||
            !phone_utf8_valid(e->value.sms.sender,sizeof e->value.sms.sender) ||
            !phone_utf8_valid(e->value.sms.text,sizeof e->value.sms.text)) return 0;
        demo_off(p);
        unsigned slot=0;
        while (slot<p->message_count && p->inbox[slot].id!=e->value.sms.id) ++slot;
        if (slot==PHONE_SMS_SLOTS) { p->note=PHONE_NOTE_FULL; return 1; }
        if (slot<p->message_count && p->inbox[slot].utc==e->value.sms.utc &&
            !strcmp(p->inbox[slot].sender,e->value.sms.sender) &&
            !strcmp(p->inbox[slot].text,e->value.sms.text)) return 1;
        uint8_t unread=slot==p->message_count ? 1 : p->inbox[slot].unread;
        p->inbox[slot]=e->value.sms; p->inbox[slot].unread=unread; p->inbox[slot].source=e->source;
        if(slot==p->selected_message) p->message_scroll=0;
        if (slot==p->message_count) ++p->message_count;
        return 1;
    }
    case PHONE_EVENT_CALL: {
        if ((e->source!=PHONE_SOURCE_MODEM && !(p->demo && e->source==PHONE_SOURCE_DEMO)) ||
            e->value.call.state>PHONE_CALL_ENDED || !valid_number(e->value.call.number,1)) return 0;
        if (e->source==PHONE_SOURCE_MODEM) demo_off(p);
        /* Only a matching request result clears pending. A periodic snapshot
         * is a call fact, not an acknowledgement of an arbitrary operation. */
        int new_call=e->value.call.generation && e->value.call.generation!=p->call.generation;
        int new_incoming=e->value.call.state==PHONE_CALL_INCOMING &&
            (new_call || p->call.state==PHONE_CALL_IDLE || p->call.state==PHONE_CALL_ENDED);
        /* Periodic modem snapshots must not steal the user's current page or
         * restart a call timer when state and number have not changed. */
        int changed=*e->value.call.number && strcmp(p->call.number,e->value.call.number);
        if (!new_call && p->call.state==e->value.call.state && (!changed ||
             p->call.state==PHONE_CALL_ENDED)) {
            p->call.id=e->value.call.id; /* Bind provisional RING without moving focus/timers. */
            return 1;
        }
        if (e->value.call.state==PHONE_CALL_IDLE || e->value.call.state==PHONE_CALL_DIALING ||
            e->value.call.state==PHONE_CALL_INCOMING ||
            (changed && !e->value.call.generation) || new_call)
            memset(&p->call,0,sizeof p->call);
        if (e->value.call.state==PHONE_CALL_ACTIVE && p->call.state!=PHONE_CALL_ACTIVE) connected(p,now);
        if (p->call.state!=PHONE_CALL_ENDED || e->value.call.state!=PHONE_CALL_ENDED) p->call.changed_ms=now;
        p->call.state=e->value.call.state;
        p->call.id=e->value.call.id;p->call.generation=e->value.call.generation;
        if(*e->value.call.number) strcpy(p->call.number,e->value.call.number);
        if(new_incoming) p->action_selected=0;
        if(p->call.state==PHONE_CALL_INCOMING)
            phone_navigate(p,page,APP_PAGE_INCOMING);
        /* Dialing/connected/ended snapshots never steal navigation. The
         * explicit dial submission opens the call page in the control task. */
        return 1;
    }
    case PHONE_EVENT_VOICE_RESULT:
        if(e->source!=PHONE_SOURCE_MODEM || e->value.result.note>PHONE_NOTE_EMPTY) return 0;
        if(!p->pending || !e->value.result.request_id || e->value.result.request_id!=p->pending_id) return 1;
        p->pending=0;p->pending_kind=PHONE_INTENT_NONE;p->pending_id=0;
        p->note=e->value.result.note;return 1;
    case PHONE_EVENT_DEMO:
        if (e->value.demo) demo_on(p,now); else demo_off(p);
        *page=APP_PAGE_HOME; return 1;
    default: return 0;
    }
}

int phone_apply(phone_state_t *p,uint8_t *page,unsigned rotation,const phone_event_t *e,uint64_t now)
{ return phone_apply_impl(p,page,rotation,e,now); }
