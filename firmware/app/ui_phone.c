/* UniKnect custom pages using WouoUI's actual graph/font/animation modules.
 * WouoUI pixels use 1=ink here; conversion to RGB565 stays in ui_display.c.
 * No drawing function changes focus, page state, or submits phone requests. */
#include "ui_display.h"
#include "app_config.h"
#include "lcd_st7735.h"
#include "phone_font.h"
#include "WouoUI_graph.h"
#include <stdio.h>
#include <string.h>

static ScreenBuff mono;
static ScreenBuff *drawing=&mono;
static Pen pen;
static Canvas clip;
static int page_x,scroll_y,cursor_y;
static uint32_t s_graph_ms,s_convert_ms;
__attribute__((weak)) uint32_t ui_clock_ms(void) {return 0;}
void ui_compose_cost(uint32_t *graph_ms,uint32_t *convert_ms)
{*graph_ms=s_graph_ms;*convert_ms=s_convert_ms;}
static int screen_w(void) { return lcd_width(); }
static int screen_h(void) { return lcd_height(); }
static int bottom_y(void) { return screen_h()-PHONE_NAV_H; }
static int is_top(uint8_t page)
{ return phone_top_index(page)>=0; }
static void set_clip(int x,int y,int w,int h) { clip=(Canvas){x,y,w,h}; }
static void ink(int value) { WouoUI_GraphSetPenColor(value?1:0); }
static void point(int x,int y,int value)
{
    if(x<clip.start_x || y<clip.start_y || x>=clip.start_x+clip.w || y>=clip.start_y+clip.h) return;
    ink(value); WouoUI_CanvasDrawPoint(&clip,(int16_t)(x-clip.start_x),(int16_t)(y-clip.start_y));
}
static void rect(int x,int y,int w,int h,int value)
{
    if(w<=0 || h<=0) return;
    int right=x+w,bottom=y+h;
    if(x<clip.start_x) x=clip.start_x;
    if(y<clip.start_y) y=clip.start_y;
    if(right>clip.start_x+clip.w) right=clip.start_x+clip.w;
    if(bottom>clip.start_y+clip.h) bottom=clip.start_y+clip.h;
    w=right-x;h=bottom-y;if(w<=0 || h<=0) return;
    ink(value); WouoUI_CanvasDrawRBox(&clip,(int16_t)(x-clip.start_x),(int16_t)(y-clip.start_y),(int16_t)w,(int16_t)h,0);
}
static void box(int x,int y,int w,int h,int radius)
{
    if(x<clip.start_x || y<clip.start_y || x+w>clip.start_x+clip.w || y+h>clip.start_y+clip.h) {
        rect(x,y,w,1,1);rect(x,y+h-1,w,1,1);rect(x,y,1,h,1);rect(x+w-1,y,1,h,1);return;
    }
    ink(1); WouoUI_CanvasDrawRBoxEmpty(&clip,(int16_t)(x-clip.start_x),(int16_t)(y-clip.start_y),(int16_t)w,(int16_t)h,(uint8_t)radius);
}
static void small(int x,int y,const char *text,int value)
{
    ink(value); WouoUI_CanvasDrawStr(&clip,(int16_t)(x-clip.start_x),(int16_t)(y-clip.start_y),Font_6_8,(uint8_t *)text);
}
static int text_width(const char *text)
{
    int w=0; while(*text) w+=phone_utf8_next(&text)<128?6:12; return w;
}
static void mono_byte(int px,int py,unsigned bits,int value)
{
    int end=clip.start_y+clip.h,first=py<clip.start_y?clip.start_y:py;
    int last=py+8>end?end:py+8;
    if(first>=last || !bits) return;
    bits=(bits>>(first-py))&((1u<<(last-first))-1u);
    if(!bits) return;
    unsigned page=(unsigned)first/8u,mask=bits<<((unsigned)first&7u);
    if(value) (*drawing)[page][px]|=(uint8_t)mask;
    else (*drawing)[page][px]&=(uint8_t)~mask;
    if(mask>>8) {
        if(value) (*drawing)[page+1u][px]|=(uint8_t)(mask>>8);
        else (*drawing)[page+1u][px]&=(uint8_t)~(mask>>8);
    }
}
static void glyph_blit(int x,int y,int width,const uint8_t *columns,int value)
{
    /* The glyph cache already matches WouoUI's column-byte layout. Clip and
     * merge those bytes directly into its buffer, skipping transparent bytes. */
    int panel_width=screen_w();
    for(int col=0;col<width;++col) {
        int px=x+col;
        if(px<clip.start_x || px>=clip.start_x+clip.w || px<0 || px>=panel_width) continue;
        for(unsigned part=0;part<2;++part) {
            mono_byte(px,y+(int)part*8,columns[part*(unsigned)width+(unsigned)col],value);
        }
    }
}
static void text12(int x,int y,const char *text,int width,int value)
{
    /* Display-owner cache: transpose each glyph once, then merge clipped
     * column bytes into the WouoUI buffer instead of plotting each pixel. */
    typedef struct {uint32_t codepoint;uint8_t columns[24];} cached_glyph_t;
    static cached_glyph_t glyphs[128];
    static unsigned next;
    int left=x;
    ink(value);
    while(*text) {
        uint32_t cp=phone_utf8_next(&text); int cw=cp<128?6:12;
        if(x-left+cw>width) break;
        cached_glyph_t *glyph=NULL;
        for(unsigned i=0;i<128;++i) if(glyphs[i].codepoint==cp) {glyph=&glyphs[i];break;}
        if(!glyph) {
            glyph=&glyphs[next];next=(next+1u)%128u;
            glyph->codepoint=cp;memset(glyph->columns,0,sizeof glyph->columns);
            const uint16_t *rows=phone_font_rows(cp);
            for(unsigned row=0;row<12;++row) for(int col=0;col<cw;++col)
                if(rows[row]&(1u<<(11-col))) glyph->columns[(row/8u)*(unsigned)cw+(unsigned)col]|=(uint8_t)(1u<<(row%8u));
        }
        glyph_blit(x,y,cw,glyph->columns,value);
        x+=cw;
    }
}
static void center12(int y,const char *text)
{ text12((screen_w()-text_width(text))/2,y,text,screen_w()-8,1); }
static void header(const app_model_t *m,const phone_datetime_t *date,int valid)
{
    set_clip(0,0,screen_w(),PHONE_STATUS_H);
    char d[8]="-- --",t[8]="--:--";
    if(valid) {
        (void)snprintf(d,sizeof d,"%02u-%02u",date->month,date->day);
        (void)snprintf(t,sizeof t,"%02u:%02u",date->hour,date->minute);
    }
    small(4,4,d,1);
    small((screen_w()-30)/2,4,t,1); /* absolute panel center, independent of date/icon */
    const phone_network_t *n=&m->phone.network;
    int x=screen_w()-20;
    if(n->enabled && n->registered && n->lte && n->signal_valid) {
        unsigned bars=phone_signal_bars(n);
        for(unsigned i=0;i<5;++i) {
            int height=2+(int)i*2;
            if(i<bars) rect(x+(int)i*3,13-height,2,height,1);
            else rect(x+(int)i*3,12,2,1,1);
        }
    } else {
        /* No signal sample is represented as unknown, never a fabricated level. */
        for(int i=0;i<7;++i) { point(x+4+i,5+i,1); point(x+10-i,5+i,1); }
    }
    rect(0,15,screen_w(),1,1);
}
static void footer(const phone_state_t *p)
{
    set_clip(0,bottom_y(),screen_w(),PHONE_NAV_H);
    int starts[4]={0,53,107,160};
    if(screen_w()!=160) { starts[1]=screen_w()/3; starts[2]=screen_w()*2/3; starts[3]=screen_w(); }
    const char *labels[]={"选择","主界面","返回"};
    for(unsigned i=0;i<3;++i) {
        int selected=p->focus_bar && p->nav_selected==i && !p->navigation_locked;
        int width=starts[i+1]-starts[i];
        if(selected) rect(starts[i],bottom_y(),width,PHONE_NAV_H,1);
        text12(starts[i]+(width-text_width(labels[i]))/2,bottom_y()+4,labels[i],width, !selected);
    }
    rect(0,bottom_y(),screen_w(),1,1);
    rect(starts[1],bottom_y(),1,PHONE_NAV_H,1); rect(starts[2],bottom_y(),1,PHONE_NAV_H,1);
}
static void digit(int x,int y,char ch)
{
    static const uint8_t segments[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    unsigned mask=ch>='0' && ch<='9'?segments[ch-'0']:0x40;
    if(mask&1u) rect(x+2,y,10,3,1);
    if(mask&2u) rect(x+11,y+2,3,11,1);
    if(mask&4u) rect(x+11,y+15,3,11,1);
    if(mask&8u) rect(x+2,y+25,10,3,1);
    if(mask&16u) rect(x,y+15,3,11,1);
    if(mask&32u) rect(x,y+2,3,11,1);
    if(mask&64u) rect(x+2,y+12,10,3,1);
}
static void clock_page(int shift,const phone_datetime_t *date,int valid)
{
    static const char brand[]="UniKnect Gen1 Pro";
    small(shift+(screen_w()-(int)(sizeof brand-1)*6)/2,26,brand,1);
    char time[12]="--:--:--";
    if(valid) (void)snprintf(time,sizeof time,"%02u:%02u:%02u",date->hour,date->minute,date->second);
    int x=shift+(screen_w()-113)/2;
    for(unsigned i=0;i<8;++i) {
        if(time[i]==':') { rect(x,62,3,3,1); rect(x,74,3,3,1); x+=7; }
        else { digit(x,53,time[i]); x+=17; }
    }
}
static void contact_icon(int x,int y)
{
    box(x,y,28,30,2); box(x+9,y+5,10,10,4);
    box(x+5,y+17,18,9,3);
    rect(x-3,y+5,5,2,1); rect(x-3,y+14,5,2,1); rect(x-3,y+23,5,2,1);
}
static void envelope_icon(int x,int y)
{
    box(x,y,32,24,1);
    for(int i=0;i<16;++i) { point(x+i,y+i/2,1); point(x+31-i,y+i/2,1); }
}
static void top_pages_draw(const phone_datetime_t *date,int valid)
{
    int base=page_x/screen_w(); if(page_x<0 && page_x%screen_w()) --base;
    for(int position=base-1;position<=base+1;++position) {
        int shift=position*screen_w()-page_x;
        if(shift>=screen_w() || shift<=-screen_w()) continue;
        int index=(position%PHONE_TOP_COUNT+PHONE_TOP_COUNT)%PHONE_TOP_COUNT;
        if(index==PHONE_TOP_CLOCK) clock_page(shift,date,valid);
        else if(index==PHONE_TOP_CONTACT) {
            contact_icon(shift+(screen_w()-28)/2,35);
            text12(shift+(screen_w()-48)/2,79,"家人联系",48,1);
        } else if(index==PHONE_TOP_SMS) {
            envelope_icon(shift+(screen_w()-32)/2,40);
            text12(shift+(screen_w()-48)/2,79,"短信查看",48,1);
        } else if(index==PHONE_TOP_SIGNAL) {
            int x=shift+(screen_w()-34)/2;
            for(int bar=0;bar<4;++bar)rect(x+bar*9,65-(bar+1)*7,6,(bar+1)*7,1);
            text12(shift+(screen_w()-48)/2,79,"信号参数",48,1);
        } else {
            int cx=shift+screen_w()/2,cy=50;
            for(int y=-15;y<=15;++y)for(int x=-15;x<=15;++x) {
                int ax=x<0?-x:x,ay=y<0?-y:y,r=x*x+y*y;
                if((r>=36 && r<=144) || (ax<=3 && ay>=10) || (ay<=3 && ax>=10) ||
                   (ax>=8 && ax<=11 && ay>=8 && ay<=11))point(cx+x,cy+y,1);
            }
            text12(shift+(screen_w()-48)/2,79,"系统设置",48,1);
        }
    }
}
static void list_cursor(int height)
{ box(6,cursor_y-scroll_y,screen_w()-12,height,2); }
static void contacts(const phone_state_t *p)
{
    center12(23,"家人联系");
    if(!phone_contact_count()) { center12(63,"暂无联系人"); return; }
    text12(16,54,"家人1",screen_w()-30,1);
    if(!p->focus_bar) list_cursor(25);
    if(p->pending) center12(91,"正在联系");
    else if(p->call.state!=PHONE_CALL_IDLE && p->call.state!=PHONE_CALL_ENDED) center12(91,"返回通话");
}
static void inbox(const phone_state_t *p)
{
    center12(23,"短信查看");
    if(!p->message_count) { center12(63,"暂无短信"); return; }
    set_clip(4,40,screen_w()-8,66);
    unsigned first=(unsigned)scroll_y/22u;
    for(unsigned i=first;i<p->message_count && i<first+4u;++i) {
        int y=40+(int)i*22-scroll_y;
        text12(16,y+4,p->inbox[i].sender,screen_w()-30,1);
    }
    if(!p->focus_bar) list_cursor(21);
    set_clip(0,16,screen_w(),bottom_y()-16);
}
/* Keep the bounded document cache separate from the page renderer. */
__attribute__((noinline)) static void sms_body(const char *text,unsigned rotation,int offset)
{
    enum {CACHE_LINES=10,LINE_HEIGHT=14};
    static struct {
        ScreenBuff pixels;
        char text[PHONE_SMS_TEXT_MAX+1];
        uint16_t offsets[PHONE_SMS_TEXT_MAX+1];
        unsigned units,total,first;
        uint8_t valid,raster_valid;
    } cache;
    unsigned units=phone_message_units(rotation),visible=phone_message_visible(rotation);
    char line[PHONE_SMS_TEXT_MAX+1];
    if(!cache.valid || cache.units!=units || strncmp(cache.text,text,PHONE_SMS_TEXT_MAX+1u)) {
        size_t length=0;while(length<PHONE_SMS_TEXT_MAX && text[length]) ++length;
        memcpy(cache.text,text,length);cache.text[length]=0;
        cache.units=units;cache.total=0;
        const char *cursor=cache.text;
        while(*cursor && cache.total<PHONE_SMS_TEXT_MAX) {
            cache.offsets[cache.total++]=(uint16_t)(cursor-cache.text);
            size_t used=phone_wrap_line(cursor,units,line,sizeof line);
            if(!used) break;
            cursor+=used;
        }
        cache.offsets[cache.total]=(uint16_t)(cursor-cache.text);
        cache.valid=1;cache.raster_valid=0;
    }
    unsigned maximum=cache.total>visible?cache.total-visible:0;
    if(offset<0) offset=0;
    if(offset>(int)maximum*LINE_HEIGHT) offset=(int)maximum*LINE_HEIGHT;
    unsigned first=(unsigned)offset/LINE_HEIGHT;
    unsigned required=first+visible+((unsigned)offset%LINE_HEIGHT!=0u);
    if(required>cache.total) required=cache.total;
    int width=screen_w()-12,height=(int)visible*LINE_HEIGHT;
    if(!cache.raster_valid || first<cache.first || required>cache.first+CACHE_LINES) {
        cache.first=first?first-1u:0;
        unsigned latest=cache.total>CACHE_LINES?cache.total-CACHE_LINES:0;
        if(cache.first>latest) cache.first=latest;
        Canvas saved_clip=clip;
        drawing=&cache.pixels;WouoUI_GraphSetBuff(drawing);ink(1);WouoUI_BuffClear();
        set_clip(0,0,width,CACHE_LINES*LINE_HEIGHT);
        for(unsigned row=0;row<CACHE_LINES && cache.first+row<cache.total;++row) {
            phone_wrap_line(cache.text+cache.offsets[cache.first+row],units,line,sizeof line);
            text12(0,(int)row*LINE_HEIGHT,line,width,1);
        }
        drawing=&mono;WouoUI_GraphSetBuff(drawing);clip=saved_clip;cache.raster_valid=1;
    }
    set_clip(4,62,screen_w()-8,height);
    unsigned source=(unsigned)offset-cache.first*LINE_HEIGHT;
    for(int target_page=62/8;target_page<=(62+height-1)/8;++target_page) {
        int first=target_page*8;if(first<62) first=62;
        int last=target_page*8+8;if(last>62+height) last=62+height;
        unsigned destination_shift=(unsigned)(first-target_page*8);
        unsigned mask=((1u<<(last-first))-1u)<<destination_shift;
        unsigned sy=source+(unsigned)(first-62),page=sy/8u,shift=sy%8u;
        const uint8_t *low=cache.pixels[page],*high=cache.pixels[page+1u];
        uint8_t *destination=mono[target_page]+6;
        for(int col=0;col<width;++col) {
            unsigned bits=((unsigned)low[col]|((unsigned)high[col]<<8))>>shift;
            destination[col]|=(uint8_t)((bits<<destination_shift)&mask);
        }
    }
}
static void message(const app_model_t *m)
{
    const phone_state_t *p=&m->phone;
    center12(22,"短信详情");
    if(!p->message_count) { center12(63,"暂无短信"); return; }
    unsigned index=p->selected_message;
    if(index>=p->message_count || index>=PHONE_SMS_SLOTS) index=0;
    const phone_sms_t *sms=&p->inbox[index];
    text12(6,36,sms->sender,screen_w()-12,1);
    phone_datetime_t date; char time[20]="-- -- --:--";
    if(phone_time_to_beijing(sms->utc,&date)) (void)snprintf(time,sizeof time,"%02u-%02u %02u:%02u",date.month,date.day,date.hour,date.minute);
    small(6,51,time,1);
    sms_body(sms->text,m->rotation,scroll_y);
    set_clip(0,16,screen_w(),bottom_y()-16);
}
static void call_page(const app_model_t *m)
{
    const phone_state_t *p=&m->phone;
    center12(23,p->pending && p->pending_kind==PHONE_INTENT_DIAL?"正在呼叫":p->call.state==PHONE_CALL_INCOMING?"来电":p->call.state==PHONE_CALL_DIALING?"正在呼叫":
        p->call.state==PHONE_CALL_ACTIVE?"通话中":p->call.state==PHONE_CALL_ENDED?"通话结束":"暂无通话");
    if(phone_is_family_number(p->call.number))text12(8,42,"家人1",screen_w()-16,1);
    else small(8,42,*p->call.number?p->call.number:"--",1);
    if(p->call.state==PHONE_CALL_INCOMING) {
        text12(18,65,"接听",screen_w()-30,1); text12(18,85,"拒接",screen_w()-30,1);
        if(!p->focus_bar) list_cursor(19);
    } else {
        char time[16]; unsigned seconds=phone_call_seconds(p,m->uptime_ms);
        (void)snprintf(time,sizeof time,"%02u:%02u",seconds/60u,seconds%60u);
        small((screen_w()-30)/2,59,time,1);
        text12((screen_w()-24)/2,83,p->call.state==PHONE_CALL_ACTIVE || p->call.state==PHONE_CALL_DIALING?"挂断":"返回",24,1);
        if(!p->focus_bar) list_cursor(21);
    }
}
static void signal_details_draw(const phone_state_t *p,unsigned rotation)
{
    static struct {
        signal_sample_t sample;
        char lines[40][64];
        unsigned count,rotation;
        uint8_t valid;
    } cache;
    const signal_state_t *s=&p->signal;
    if(!cache.valid || cache.rotation!=rotation || memcmp(&cache.sample,&s->sample,sizeof cache.sample)) {
        cache.sample=s->sample;cache.rotation=rotation;cache.valid=1;cache.count=0;
        while(cache.count<40 && signal_line(&s->sample,phone_message_units(rotation),cache.count,
                                            cache.lines[cache.count],sizeof cache.lines[0]))++cache.count;
    }
    center12(20,"信号参数");
    if(s->request_id) center12(34,s->state==SIGNAL_DEFERRED?"通话中暂缓":"获取中");
    else {
        phone_datetime_t date;char time[12]="--:--:--";
        if(s->sample.utc && phone_time_to_beijing(s->sample.utc,&date))
            (void)snprintf(time,sizeof time,"%02u:%02u:%02u",date.hour,date.minute,date.second);
        const char *hint=s->state==SIGNAL_FAILED?"读取失败":s->sample.errors?"部分失败":
            s->sample.present && !s->sample.registered?"未注册":
            s->sample.present && !s->sample.lte?"非LTE":"中键刷新";
        small(6,36,time,1);text12(screen_w()-6-text_width(hint),34,hint,text_width(hint),1);
    }
    unsigned visible=signal_visible(rotation);
    set_clip(6,48,screen_w()-12,(int)visible*14);
    int maximum=cache.count>visible?(int)(cache.count-visible)*14:0;
    int offset=scroll_y;if(offset<0)offset=0;if(offset>maximum)offset=maximum;
    for(unsigned row=0;row<cache.count;++row) {
        int y=48+(int)row*14-offset;
        if(y>-12 && y<bottom_y())text12(6,y,cache.lines[row],screen_w()-12,1);
    }
}
static void targets(const app_model_t *m,int *x,int *cursor,int *scroll)
{
    *x=m->phone.top_position*screen_w(); *cursor=50; *scroll=0;
    if(m->page==APP_PAGE_INBOX) { *cursor=40+m->phone.selected_message*22; *scroll=m->phone.list_scroll*22; }
    if(m->page==APP_PAGE_MESSAGE) *scroll=m->phone.message_scroll*14;
    if(m->page==APP_PAGE_SIGNAL_INFO)*scroll=m->phone.signal.scroll*14;
    if(m->page==APP_PAGE_SETTINGS_MENU)*cursor=45+m->phone.selected_row*22;
    if(m->page==APP_PAGE_ROTATION)*cursor=48+m->phone.selected_row*14;
    if(m->page==APP_PAGE_CAL_CONFIRM)*cursor=m->phone.system.save_state>=SETTINGS_SAVE_FAILED?60+m->phone.selected_row*20:83;
    if(m->page==APP_PAGE_INCOMING || m->page==APP_PAGE_CALL)
        *cursor=m->phone.call.state==PHONE_CALL_INCOMING?62+m->phone.action_selected*20:80;
}
static const char *settings_hint(const phone_state_t *p,int calibration)
{
    const settings_state_t *s=&p->system;
    if(s->save_state==SETTINGS_FULL)return "重启后重新设置";
    if(s->save_state==SETTINGS_SAVE_FAILED)return "已生效未保存";
    if(s->save_state==SETTINGS_PENDING || s->save_state==SETTINGS_WRITING)
        return p->call.state!=PHONE_CALL_IDLE && p->call.state!=PHONE_CALL_ENDED?"待保存":"保存中";
    if(calibration) {
        if(s->cal_result==CAL_BUSY)return "通话中不可标定";
        if(s->cal_result==CAL_TIMEOUT)return "标定超时已恢复";
        if(s->cal_result==CAL_CANCELLED || s->cal_result==CAL_INCOMING || s->cal_result==CAL_INVALID)return "已恢复原标定";
        if(s->cal_result==CAL_SUCCESS)return "标定已保存";
    }
    return "已保存";
}
static void settings_draw(const app_model_t *m)
{
    const phone_state_t *p=&m->phone;
    if(m->page==APP_PAGE_SETTINGS_MENU) {
        center12(23,"系统设置");text12(18,48,"显示方向",screen_w()-30,1);text12(18,70,"摇杆标定",screen_w()-30,1);
        if(!p->focus_bar)list_cursor(18);
        center12(92,settings_hint(p,1));
    } else if(m->page==APP_PAGE_ROTATION) {
        center12(20,"显示方向");center12(33,m->rotation!=p->system.desired.rotation?"临时方向":settings_hint(p,0));
        for(unsigned i=0;i<4;++i) {
            char label[16];(void)snprintf(label,sizeof label,"%u度",i*90u);text12(18,50+(int)i*14,label,screen_w()-40,1);
            if(i==m->rotation) {int x=screen_w()-25,y=53+(int)i*14;rect(x,y,3,6,1);rect(x+3,y+3,6,3,1);rect(x+6,y-3,3,9,1);}
        }
        if(!p->focus_bar)list_cursor(14);
    } else {
        center12(22,"摇杆标定");
        if(p->pending || (p->call.state!=PHONE_CALL_IDLE && p->call.state!=PHONE_CALL_ENDED)) {
            center12(42,"通话中不可标定");center12(65,"返回后继续");
        } else if(p->system.save_state>=SETTINGS_SAVE_FAILED) {
            center12(40,settings_hint(p,1));center12(63,"重试保存");center12(83,"重新标定");
        } else {
            center12(42,"依次松开和保持");center12(57,"上下左右和中键");center12(86,"开始标定");
        }
        if(!p->focus_bar)list_cursor(17);
    }
}
/* Retained primitive for render/cache tests and project extensions. */
void ui_plot(ui_frame_t *f,int x,int y,int value)
{
    if(x<0 || y<0 || x>=screen_w() || y>=screen_h()) return;
    ui_cell_t *c=&f->cells[y/8][x/6]; uint8_t mask=(uint8_t)(32u>>(x%6));
    c->flags=UI_CELL_BITMAP;c->color=UI_INK;
    if(value) c->pixels[y%8]|=mask; else c->pixels[y%8]&=(uint8_t)~mask;
}
static void compose(ui_frame_t *f,const app_model_t *m)
{
    uint32_t begin=ui_clock_ms();
    WouoUI_GraphSetBuff(&mono); WouoUI_GraphSetPen(&pen); ink(1); WouoUI_BuffClear();
    set_clip(0,16,screen_w(),bottom_y()-16);
    phone_datetime_t date; int valid=phone_clock_read(&m->phone.clock,m->uptime_ms,&date);
    if(is_top(m->page)) top_pages_draw(&date,valid);
    else if(m->page==APP_PAGE_CONTACTS) contacts(&m->phone);
    else if(m->page==APP_PAGE_INBOX) inbox(&m->phone);
    else if(m->page==APP_PAGE_MESSAGE) message(m);
    else if(m->page==APP_PAGE_CALL || m->page==APP_PAGE_INCOMING) call_page(m);
    else if(m->page==APP_PAGE_SIGNAL_INFO)signal_details_draw(&m->phone,m->rotation);
    else if(m->page==APP_PAGE_SETTINGS_MENU || m->page==APP_PAGE_ROTATION || m->page==APP_PAGE_CAL_CONFIRM)settings_draw(m);
    else if(m->page==APP_PAGE_JOYSTICK) {
        static const char *const stages[]={"松开摇杆","向上保持","向下保持","向左保持","向右保持","按住中键","校准完成"};
        center12(30,"摇杆校准");
        unsigned stage=m->joy_stage<7u?m->joy_stage:0;
        center12(57,m->joy_wait_release?"松开摇杆":stages[stage]);
        center12(82,"操作栏已禁用");
    } else center12(60,"当前不可用");
    set_clip(0,16,screen_w(),bottom_y()-16);
    if(m->phone.note) {
        const char *note=m->phone.note==PHONE_NOTE_NO_SERVICE?"无语音服务":m->phone.note==PHONE_NOTE_CALL_FAILED?"呼叫失败":
            m->phone.note==PHONE_NOTE_BUSY?"操作处理中":m->phone.note==PHONE_NOTE_EMPTY?"号码未配置":"服务暂不可用";
        rect(4,bottom_y()-15,screen_w()-8,14,0); center12(bottom_y()-14,note);
    }
    if(!m->phone.focus_bar && !m->phone.navigation_locked) box(2,18,screen_w()-4,bottom_y()-20,1);
    header(m,&date,valid); footer(&m->phone);
    uint32_t drawn=ui_clock_ms();
    const unsigned width=(unsigned)screen_w();
    /* Transpose six OLED column bytes into eight 6-bit scanlines. A nibble
     * lookup avoids repeatedly querying display dimensions per pixel. */
    static const uint32_t spread[16]={0,1,0x100,0x101,0x10000,0x10001,0x10100,0x10101,
        0x1000000,0x1000001,0x1000100,0x1000101,0x1010000,0x1010001,0x1010100,0x1010101};
    memset(f,0,sizeof *f); f->cols=(uint8_t)((width+5)/6); f->rows=(uint8_t)(screen_h()/8);f->theme=UI_THEME_RETRO;
    for(unsigned row=0;row<f->rows;++row) for(unsigned col=0;col<f->cols;++col) {
        ui_cell_t *cell=&f->cells[row][col]; cell->flags=UI_CELL_BITMAP; cell->color=UI_INK;
        uint32_t low=0,high=0;
        for(unsigned x=0;x<6 && col*6+x<width;++x) {
            uint8_t byte=mono[row][col*6+x];
            low|=spread[byte&15u]<<(5u-x); high|=spread[byte>>4]<<(5u-x);
        }
        memcpy(cell->pixels,&low,4);memcpy(cell->pixels+4,&high,4);
    }
    s_graph_ms=drawn-begin;s_convert_ms=ui_clock_ms()-drawn;
}
void ui_compose(ui_frame_t *f,const app_model_t *m,const app_task_info_t tasks[APP_TASK_COUNT])
{
    (void)tasks; targets(m,&page_x,&cursor_y,&scroll_y); compose(f,m);
}
int ui_compose_animated(ui_frame_t *f,const app_model_t *m,const app_task_info_t tasks[APP_TASK_COUNT],ui_animation_t *a,uint32_t now)
{
    (void)tasks; int x,cursor,scroll; targets(m,&x,&cursor,&scroll);
    int reset=!a->valid || a->rotation!=m->rotation || ((!is_top(a->page) || !is_top(m->page)) && a->page!=m->page);
    if(a->valid && (x-a->carousel.pos_tgt>screen_w() || a->carousel.pos_tgt-x>screen_w())) reset=1;
    if(reset) {
        a->carousel=(AnimPos){(int16_t)x,(int16_t)x,0};
        a->cursor=(AnimPos){(int16_t)cursor,(int16_t)cursor,0};
        a->viewport=(AnimPos){(int16_t)scroll,(int16_t)scroll,0}; a->last_ms=now;
    }
    a->carousel.pos_tgt=(int16_t)x; a->cursor.pos_tgt=(int16_t)cursor; a->viewport.pos_tgt=(int16_t)scroll;
    unsigned steps=(now-a->last_ms)/CFG_DISPLAY_MS; if(steps>8u) { steps=8u;a->last_ms=now-steps*CFG_DISPLAY_MS; }
    for(unsigned i=0;i<steps;++i) {
        uint8_t finish=1;
        WouoUI_Animation(&a->carousel,CFG_UI_EASE_MS,CFG_DISPLAY_MS,&finish);
        WouoUI_Animation(&a->cursor,CFG_UI_EASE_MS,CFG_DISPLAY_MS,&finish);
        WouoUI_Animation(&a->viewport,CFG_UI_SCROLL_EASE_MS,CFG_DISPLAY_MS,&finish); a->last_ms+=CFG_DISPLAY_MS;
    }
    a->valid=1;a->page=m->page;a->rotation=m->rotation;
    page_x=a->carousel.pos_cur;cursor_y=a->cursor.pos_cur;scroll_y=a->viewport.pos_cur;
    compose(f,m);
    return a->carousel.pos_cur!=a->carousel.pos_tgt || a->cursor.pos_cur!=a->cursor.pos_tgt || a->viewport.pos_cur!=a->viewport.pos_tgt;
}
