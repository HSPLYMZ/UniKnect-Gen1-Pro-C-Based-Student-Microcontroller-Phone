#include "common.h"
#include <string.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/app_logic.c"
#include "../../firmware/app/ui_display.c"
#include "../../firmware/third_party/wououi/WouoUI_anim.c"
#include "../../firmware/third_party/wououi/WouoUI_font.c"
#include "../../firmware/third_party/wououi/WouoUI_graph.c"
#include "../../firmware/app/ui_phone.c"
#include "../../firmware/fonts/phone_font.c"

static uint16_t panel[LCD_W * LCD_H];
static uint16_t framebuffer[LCD_W * LCD_H];
static unsigned flushes, flushed_pixels;
static int error, fail_next;
static unsigned rotation;
int lcd_width(void) { return rotation & 1 ? LCD_H : LCD_W; }
int lcd_height(void) { return rotation & 1 ? LCD_W : LCD_H; }
unsigned lcd_get_rotation(void) { return rotation; }
void lcd_fill(uint16_t color)
{ for (unsigned i = 0; i < LCD_W*LCD_H; ++i) framebuffer[i] = color; }
int lcd_set_rotation(unsigned value)
{ if (value > 3) return 0; rotation = value; lcd_fill(LCD_BLACK); return 1; }
static unsigned pixel_index(int x, int y)
{
    CHECK(x >= 0 && x < lcd_width() && y >= 0 && y < lcd_height());
    switch (rotation) {
    case 1: return (unsigned)(x*LCD_W + LCD_W-1-y);
    case 2: return (unsigned)((LCD_H-1-y)*LCD_W + LCD_W-1-x);
    case 3: return (unsigned)((LCD_H-1-x)*LCD_W+y);
    default: return (unsigned)(y*LCD_W+x);
    }
}
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    CHECK(x >= 0 && y >= 0 && x+w <= lcd_width() && y+h <= lcd_height());
    for (int row = y; row < y+h; ++row)
        for (int col = x; col < x+w; ++col) framebuffer[pixel_index(col,row)] = color;
}
void lcd_draw_pixel(int x, int y, uint16_t color)
{
    framebuffer[pixel_index(x,y)] = color;
}
void lcd_draw_cell(int x,int y,const uint8_t rows[8],uint16_t fg,uint16_t bg)
{
    for(int r=0;r<8;++r) for(int c=0;c<6;++c)
        lcd_draw_pixel(x+c,y+r,(rows[r]&(32u>>c))?fg:bg);
}
int lcd_get_error(void) { return error; }
void lcd_flush_rect(int x, int y, int w, int h)
{
    CHECK(x >= 0 && y >= 0 && x+w <= lcd_width() && y+h <= lcd_height());
    ++flushes; flushed_pixels += (unsigned)(w*h);
    if (fail_next) { fail_next = 0; error = 1;return; }
    for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col) {
        unsigned i=pixel_index(col,row);panel[i]=framebuffer[i];
    }
}
void lcd_flush(void)
{
    ++flushes; flushed_pixels += LCD_W*LCD_H;
    if (fail_next) { fail_next = 0; error = 1;return; }
    memcpy(panel,framebuffer,sizeof panel);
}


static void fixture(app_model_t *m)
{
    app_model_init(m);m->uptime_ms=10000;
    phone_datetime_t dt={2026,10,24,16,30,45};uint32_t utc;
    CHECK(phone_time_from_beijing(&dt,&utc));
    CHECK(phone_clock_set(&m->phone.clock,utc,m->uptime_ms,PHONE_SOURCE_DEMO));
    m->phone.network=(phone_network_t){.updated_ms=10000,.enabled=1,.registered=1,.lte=1,.signal_valid=1,.rsrp_dbm=-89};
    m->phone.message_count=4;
    for(unsigned i=0;i<4;++i) {
        m->phone.inbox[i].id=i+1;m->phone.inbox[i].utc=utc-60;
        strcpy(m->phone.inbox[i].sender,"测试短信");
        strcpy(m->phone.inbox[i].text,"仅用于离线验证的文字。上下滚动后可以回到底栏。ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789。继续阅读下一行，直到内容底部，然后向下进入操作栏。");
        m->phone.inbox[i].source=PHONE_SOURCE_DEMO;
    }
    m->joy_stage=6;
    m->phone.signal.sample=(signal_sample_t){.valid=127,.registered=1,.lte=1,.present=1,
        .rsrp=-79,.rsrq=-4,.rssi=-56,.sinr_raw=116,.band=40,.cell_id=0x6097c02,
        .utc=utc};
    strcpy(m->phone.signal.sample.operator_name,"中国广电");
    m->phone.signal.state=SIGNAL_DONE;
}
void test_preview(unsigned request)
{
    unsigned page=request&255u,angle=(request>>8)&3u;
    CHECK(lcd_set_rotation(angle));
    static app_model_t m;fixture(&m);m.page=(uint8_t)page;m.rotation=(uint8_t)angle;
    if(page==APP_PAGE_CONTACT_ENTRY) {m.phone.top_page=PHONE_TOP_CONTACT;m.phone.top_position=1;}
    if(page==APP_PAGE_SMS_ENTRY) {m.phone.top_page=PHONE_TOP_SMS;m.phone.top_position=2;}
    if(page==APP_PAGE_SETTINGS_ENTRY) {m.phone.top_page=PHONE_TOP_SETTINGS;m.phone.top_position=4;}
    if(page==APP_PAGE_SETTINGS_MENU) {
        if(request&0x40000u)m.phone.system.save_state=SETTINGS_SAVE_FAILED;
        if(request&0x200000u)m.phone.system.save_state=SETTINGS_PENDING;
    }
    if(page==APP_PAGE_SIGNAL_ENTRY) {m.phone.top_page=PHONE_TOP_SIGNAL;m.phone.top_position=3;}
    if(page==APP_PAGE_SIGNAL_INFO) {
        if(request&0x10000u)memset(&m.phone.signal.sample,0,sizeof m.phone.signal.sample);
        if(request&0x800000u)m.phone.signal.scroll=(uint8_t)(signal_lines(&m.phone.signal.sample,phone_message_units(angle))-signal_visible(angle));
        if(request&0x200000u) {m.phone.signal.request_id=1;m.phone.signal.state=SIGNAL_DEFERRED;}
        if(request&0x40000u) {m.phone.signal.state=SIGNAL_FAILED;m.phone.signal.sample.errors=3;m.phone.signal.sample.valid=0;}
    }
    if(request&0x10000u) m.phone.message_count=0;
    if(request&0x20000u) {m.phone.clock.valid=0;memset(&m.phone.network,0,sizeof m.phone.network);}
    if(request&0x80000u) {m.phone.focus_bar=1;m.phone.nav_selected=(uint8_t)((request>>20)&3u);}
    if(request&0x400000u) {m.phone.selected_message=3;m.phone.list_scroll=1;}
    if(page==APP_PAGE_CALL || page==APP_PAGE_INCOMING) {
        m.phone.call.state=page==APP_PAGE_CALL?PHONE_CALL_ACTIVE:PHONE_CALL_INCOMING;
        strcpy(m.phone.call.number,CFG_GUARDIAN_PHONE_NUMBER);
        m.phone.call.has_connection=page==APP_PAGE_CALL;m.phone.call.connected_ms=0;
        if(request&0x40000u) m.phone.call.state=PHONE_CALL_DIALING;
        if(request&0x10000u) {m.phone.call.state=PHONE_CALL_ENDED;m.phone.call.changed_ms=m.uptime_ms;}
    }
    if(page==APP_PAGE_JOYSTICK) {m.phone.navigation_locked=1;m.joy_stage=3;}
    static ui_frame_t frame;
    error=fail_next=0;ui_cache_reset();ui_compose(&frame,&m,NULL);
    CHECK(ui_render_full(&frame)>0);
    if(request&0x1000000u) {
        static ui_animation_t a;
        memset(&a,0,sizeof a);m.page=APP_PAGE_HOME;m.phone.top_position=0;m.phone.top_page=0;
        (void)ui_compose_animated(&frame,&m,NULL,&a,0);
        m.page=APP_PAGE_CONTACT_ENTRY;m.phone.top_position=1;m.phone.top_page=1;
        for(unsigned step=1;step<=((request>>25)&31u);++step)
            (void)ui_compose_animated(&frame,&m,NULL,&a,step*CFG_DISPLAY_MS);
        CHECK(ui_render_full(&frame)>0);
    }
    for(unsigned i=0;i<LCD_W*LCD_H;++i) CHECK(panel[i]==LCD_WHITE || panel[i]==LCD_BLACK);
    CHECK(ui_flush_step(&frame,520)==0);
}
static ui_frame_t desired;
static app_model_t m;
static uint16_t saved[LCD_W*LCD_H];
static uint16_t sample(int x,int y) {return panel[pixel_index(x,y)];}
static void draw(void) {ui_compose(&desired,&m,NULL);CHECK(ui_render_full(&desired)>0);}
static void partial_equals_full(void)
{
    ui_compose(&desired,&m,NULL);CHECK(ui_flush_step(&desired,520)>=0);
    memcpy(saved,panel,sizeof saved);CHECK(ui_render_full(&desired)>0);
    CHECK(!memcmp(saved,panel,sizeof saved));
}
static void glyph_blit_matches_reference(void)
{
    static ScreenBuff reference;
    static const int xs[]={-11,-2,0,17,141,155};
    static const int ys[]={5,15,54,61,99,107};
    const char *sample="选择主界面返回短信Ab09";
    WouoUI_GraphSetBuff(&mono);WouoUI_GraphSetPen(&pen);
    for(unsigned angle=0;angle<4;++angle) {
        CHECK(lcd_set_rotation(angle));
        for(unsigned i=0;i<6;++i) for(int value=0;value<2;++value) {
            set_clip(4,16,lcd_width()-8,lcd_height()-36);
            memset(mono,0x5a,sizeof mono);
            text12(xs[i],ys[i],sample,110,value);
            memcpy(reference,mono,sizeof mono);memset(mono,0x5a,sizeof mono);
            int x=xs[i];const char *p=sample;
            while(*p) {
                uint32_t cp=phone_utf8_next(&p);int cw=cp<128?6:12;
                if(x-xs[i]+cw>110) break;
                const uint16_t *rows=phone_font_rows(cp);
                for(unsigned row=0;row<12;++row) for(int col=0;col<cw;++col)
                    if(rows[row]&(1u<<(11-col))) point(x+col,ys[i]+(int)row,value);
                x+=cw;
            }
            CHECK(!memcmp(reference,mono,sizeof mono));
        }
    }
    /* More code points than the cache holds, then revisit the first page. */
    for(unsigned cp=0x4e00;cp<0x4f00;++cp) {
        char text[4]={(char)(0xe0u|(cp>>12)),(char)(0x80u|((cp>>6)&63u)),(char)(0x80u|(cp&63u)),0};
        text12(20,40,text,12,1);
    }
    CHECK(lcd_set_rotation(0));
}
static void sms_cache_matches_reference(void)
{
    static ScreenBuff actual;
    const char *texts[]={"短信和 ABC 混合内容，用于逐像素检查自动换行与阅读位置。回到底栏后还可以继续阅读。",
        "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30"};
    const int offsets[]={-1,0,1,7,13,14,63,129,260,2000};
    WouoUI_GraphSetBuff(&mono);WouoUI_GraphSetPen(&pen);
    for(unsigned angle=0;angle<4;++angle) {
        CHECK(lcd_set_rotation(angle));
        unsigned visible=phone_message_visible(angle),units=phone_message_units(angle);
        for(unsigned text=0;text<2;++text) for(unsigned i=0;i<10;++i) {
            memset(mono,0x5a,sizeof mono);set_clip(0,16,lcd_width(),lcd_height()-36);
            sms_body(texts[text],angle,offsets[i]);memcpy(actual,mono,sizeof mono);
            memset(mono,0x5a,sizeof mono);set_clip(4,62,lcd_width()-8,(int)visible*14);
            unsigned total=phone_wrapped_lines(texts[text],units),maximum=total>visible?total-visible:0;
            int offset=offsets[i];if(offset<0) offset=0;if(offset>(int)maximum*14) offset=(int)maximum*14;
            const char *p=texts[text];char line[PHONE_SMS_TEXT_MAX+1];
            for(unsigned j=0;j<(unsigned)offset/14u && *p;++j) p+=phone_wrap_line(p,units,line,sizeof line);
            for(unsigned row=0;row<=visible && *p;++row) {
                p+=phone_wrap_line(p,units,line,sizeof line);const char *letter=line;int x=6;
                while(*letter) {
                    uint32_t cp=phone_utf8_next(&letter);int cw=cp<128?6:12;
                    const uint16_t *rows=phone_font_rows(cp);
                    for(unsigned y=0;y<12;++y) for(int col=0;col<cw;++col)
                        if(rows[y]&(1u<<(11-col))) point(x+col,62+(int)row*14-offset%14+(int)y,1);
                    x+=cw;
                }
            }
            CHECK(!memcmp(actual,mono,sizeof mono));
        }
    }
    CHECK(lcd_set_rotation(0));
}
void test_main(void)
{
    const char *signal_glyphs="信号参数运营商中国广电移动联通获取暂缓失败中键刷新未知系统设置显示方向摇杆标定保存生效重启开始依次度临时";
    while(*signal_glyphs)CHECK(phone_font_rows(phone_utf8_next(&signal_glyphs))!=phone_font_rows(0xfffd));
    glyph_blit_matches_reference();
    sms_cache_matches_reference();
    CHECK(lcd_set_rotation(0));fixture(&m);draw();
    CHECK(desired.cols==27 && desired.rows==16);
    for(int x=0;x<160;++x) {CHECK(sample(x,15)==LCD_BLACK);CHECK(sample(x,108)==LCD_BLACK);}
    for(int y=109;y<128;++y) {CHECK(sample(53,y)==LCD_BLACK);CHECK(sample(107,y)==LCD_BLACK);}
    /* Case-sensitive brand is precisely the requested 17-character 6x8 text. */
    const char *brand="UniKnect Gen1 Pro";
    for(unsigned c=0;c<strlen(brand);++c) for(unsigned x=0;x<6;++x) for(unsigned y=0;y<8;++y) {
        unsigned bits=Font_6_8.table[((unsigned char)brand[c]-32u)*6u+x];
        CHECK(sample(29+(int)c*6+(int)x,26+(int)y)==((bits&(1u<<y))?LCD_BLACK:LCD_WHITE));
    }
    /* Header HH:MM is centered at x=65..94, not shifted by date/signal widths. */
    const char *time="16:30";
    for(unsigned c=0;c<5;++c) for(unsigned x=0;x<6;++x) for(unsigned y=0;y<8;++y) {
        unsigned bits=Font_6_8.table[((unsigned char)time[c]-32u)*6u+x];
        CHECK(sample(65+(int)c*6+(int)x,4+(int)y)==((bits&(1u<<y))?LCD_BLACK:LCD_WHITE));
    }
    memcpy(saved,panel,sizeof saved);m.uptime_ms+=1000;ui_compose(&desired,&m,NULL);
    unsigned before=flushed_pixels;CHECK(ui_flush_step(&desired,520)>0);
    CHECK(flushed_pixels-before<LCD_W*LCD_H/3u);
    unsigned changes=0;
    for(int y=0;y<128;++y) for(int x=0;x<160;++x)
        if(panel[pixel_index(x,y)]!=saved[pixel_index(x,y)]) {CHECK(y>=53 && y<81 && x>=105 && x<136);++changes;}
    CHECK(changes>0); /* seconds really change the large clock */
    m.page=APP_PAGE_CONTACT_ENTRY;m.phone.top_page=1;m.phone.top_position=1;partial_equals_full();
    memcpy(saved,panel,sizeof saved);
    m.page=APP_PAGE_SMS_ENTRY;m.phone.top_page=2;m.phone.top_position=2;partial_equals_full();
    for(int y=0;y<16;++y) for(int x=0;x<160;++x) CHECK(panel[pixel_index(x,y)]==saved[pixel_index(x,y)]);
    for(int y=108;y<128;++y) for(int x=0;x<160;++x) CHECK(panel[pixel_index(x,y)]==saved[pixel_index(x,y)]);
    for(unsigned nav=0;nav<3;++nav) {
        m.phone.focus_bar=1;m.phone.nav_selected=(uint8_t)nav;partial_equals_full();
        int start=nav==0?0:nav==1?53:107,end=nav==0?53:nav==1?107:160;
        for(int x=start;x<end;++x) CHECK(sample(x,110)==LCD_BLACK);
        CHECK(sample(2,18)==LCD_WHITE); /* no stale content focus frame */
    }
    m.phone.focus_bar=0;m.page=APP_PAGE_INBOX;
    for(unsigned row=0;row<4;++row) {
        m.phone.selected_message=(uint8_t)row;m.phone.list_scroll=row>=3?1:0;partial_equals_full();
    }
    m.page=APP_PAGE_MESSAGE;
    for(unsigned row=0;row<5;++row) {m.phone.message_scroll=(uint8_t)row;partial_equals_full();}
    /* Complete right edge is covered by the four-pixel final tile. */
    desired.cells[15][26].pixels[7]^=8;
    unsigned before_pixels=flushed_pixels;
    CHECK(ui_flush_step(&desired,520)==1);
    CHECK(flushed_pixels-before_pixels==1); /* tight 1x1 transfer, not a whole tile */
    /* Failed windows do not advance the displayed cache. */
    desired.cells[0][0].pixels[0]^=32;fail_next=1;
    CHECK(ui_flush_step(&desired,520)==-1);error=0;
    CHECK(ui_flush_step(&desired,520)==1 && ui_flush_step(&desired,520)==0);
    fixture(&m);static ui_animation_t a;
    CHECK(!ui_compose_animated(&desired,&m,NULL,&a,0));
    m.page=APP_PAGE_CONTACT_ENTRY;m.phone.top_position=1;m.phone.top_page=1;
    CHECK(ui_compose_animated(&desired,&m,NULL,&a,CFG_DISPLAY_MS));
    CHECK(a.carousel.pos_cur==40); /* actual upstream nonlinear step, not a linear tween */
    CHECK(ui_compose_animated(&desired,&m,NULL,&a,2u*CFG_DISPLAY_MS));CHECK(a.carousel.pos_cur==70);
    for(unsigned tick=3u*CFG_DISPLAY_MS;tick<=1200;tick+=CFG_DISPLAY_MS) (void)ui_compose_animated(&desired,&m,NULL,&a,tick);
    CHECK(a.carousel.pos_cur==160 && !ui_compose_animated(&desired,&m,NULL,&a,1224));
    uint32_t random_state=12345;
    for(unsigned angle=0;angle<4;++angle) {
        CHECK(lcd_set_rotation(angle));fixture(&m);m.rotation=(uint8_t)angle;draw();
        for(unsigned step=0;step<12;++step) {
            for(unsigned pixel=0;pixel<32;++pixel) {
                random_state=random_state*1664525u+1013904223u;
                int x=(int)(random_state%(unsigned)lcd_width());
                random_state=random_state*1664525u+1013904223u;
                int y=(int)(random_state%(unsigned)lcd_height());
                ui_cell_t *cell=&desired.cells[y/8][x/6];
                ui_plot(&desired,x,y,!(cell->pixels[y%8]&(32u>>(x%6))));
            }
            CHECK(ui_flush_step(&desired,520)>0);
            for(int y=0;y<lcd_height();++y) for(int x=0;x<lcd_width();++x) {
                const ui_cell_t *cell=&desired.cells[y/8][x/6];
                CHECK(sample(x,y)==((cell->pixels[y%8]&(32u>>(x%6)))?LCD_BLACK:LCD_WHITE));
            }
        }
    }
    for(unsigned rotation=0;rotation<4;++rotation) {
        CHECK(lcd_set_rotation(rotation));fixture(&m);m.rotation=(uint8_t)rotation;
        m.page=APP_PAGE_SIGNAL_INFO;draw();
        for(int y=48+(int)signal_visible(rotation)*14;y<lcd_height()-23;++y)
            for(int x=6;x<lcd_width()-6;++x)CHECK(sample(x,y)==LCD_WHITE);
        for(unsigned scroll=0;scroll<12;++scroll) {m.phone.signal.scroll=(uint8_t)scroll;partial_equals_full();}
        m.phone.signal.sample.rsrp=-105;strcpy(m.phone.signal.sample.operator_name,"中国移动中国联通中国电信中国广电");partial_equals_full();
        m.phone.signal.sample.errors=3;partial_equals_full();
    }
    for(unsigned rotation=0;rotation<4;++rotation)
        for(unsigned page=0;page<APP_PAGE_COUNT;++page) test_preview(page|(rotation<<8));
    test_finish(0);
}
