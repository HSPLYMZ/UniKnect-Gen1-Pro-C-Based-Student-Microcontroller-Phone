#include "signal_data.h"
#include "phone_logic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* Quoted commas are data. Every field is bounded, including UCS2 hex names. */
static int sig_field(const char **cursor,char *out,size_t capacity)
{
    const char *p=*cursor;while(*p==' ')++p;
    int quoted=*p=='"';if(quoted)++p;
    size_t n=0;
    while(*p && (quoted?*p!='"':*p!=',' && *p!='\r' && *p!='\n')) {
        if(n+1>=capacity || (unsigned char)*p<32) return -1;
        out[n++]=*p++;
    }
    if(quoted) {if(*p!='"')return -1;++p;}
    else while(n && out[n-1]==' ')--n;
    out[n]=0;while(*p==' ')++p;
    int more=*p==',';
    if(more)++p;
    else {while(*p=='\r' || *p=='\n' || *p==' ')++p;if(*p)return -1;}
    *cursor=p;return more;
}
static int sig_number(const char *p,long low,long high,int base,long *value)
{
    if(!*p || !strcmp(p,"-") || *p=='+' || *p==' ')return 0;
    char *end;errno=0;long v=strtol(p,&end,base);
    if(errno || *end || v<low || v>high)return 0;
    *value=v;return 1;
}
int signal_parse_serving(const char *line,signal_sample_t *s)
{
    if(!line || !s || strncmp(line,"+QENG:",6))return 0;
    const char *p=line+6;char part[64];unsigned count=0;long v;
    signal_sample_t parsed={0};
    for(;;) {
        int more=sig_field(&p,part,sizeof part);if(more<0 || count>=24)return 0;
        switch(count) {
        case 0:if(strcmp(part,"servingcell"))return 0;break;
        case 1:
            parsed.registered=!strcmp(part,"NOCONN") || !strcmp(part,"CONNECT");
            if(!parsed.registered && strcmp(part,"SEARCH") && strcmp(part,"LIMSRV"))return 0;
            break;
        case 2:parsed.lte=!strcmp(part,"LTE");break;
        case 6:if(sig_number(part,0,0x0fffffff,16,&v)) {parsed.cell_id=(uint32_t)v;parsed.valid|=SIGNAL_CELL;}break;
        case 9:if(sig_number(part,1,255,10,&v)) {parsed.band=(uint16_t)v;parsed.valid|=SIGNAL_BAND;}break;
        case 13:if(sig_number(part,-140,-44,10,&v)) {parsed.rsrp=(int16_t)v;parsed.valid|=SIGNAL_RSRP;}break;
        case 14:if(sig_number(part,-40,10,10,&v)) {parsed.rsrq=(int16_t)v;parsed.valid|=SIGNAL_RSRQ;}break;
        case 15:if(sig_number(part,-140,-1,10,&v)) {parsed.rssi=(int16_t)v;parsed.valid|=SIGNAL_RSSI;}break;
        case 16:
            if(sig_number(part,INT16_MIN,INT16_MAX,10,&v)) {
                parsed.sinr_raw=(int16_t)v;
                if(v>=1 && v<=127)parsed.valid|=SIGNAL_SINR;
            }
            break;
        default:break;
        }
        ++count;if(!more)break;
    }
    if(count<2 || (parsed.lte && count!=18))return 0;
    if(!parsed.registered || !parsed.lte)parsed.valid=0;
    s->registered=parsed.registered;s->lte=parsed.lte;
    s->valid=(s->valid&SIGNAL_OPERATOR)|parsed.valid;
    s->rsrp=parsed.rsrp;s->rsrq=parsed.rsrq;s->rssi=parsed.rssi;s->sinr_raw=parsed.sinr_raw;
    s->cell_id=parsed.cell_id;s->band=parsed.band;return 1;
}
static int sig_hex4(const char *p,uint32_t *out)
{
    uint32_t v=0;
    for(unsigned i=0;i<4;++i) {
        unsigned c=(unsigned char)p[i],n;
        if(!c)return 0;
        if(c>='0' && c<='9')n=c-'0';
        else if(c>='a' && c<='f')n=c-'a'+10;
        else if(c>='A' && c<='F')n=c-'A'+10;
        else return 0;
        v=(v<<4)|n;
    }
    *out=v;return 1;
}
static int sig_decode_name(const char *input,int ucs2,char out[64])
{
    size_t used=0;int clipped=0;
    if(!*input)return 0;
    if(!ucs2) {
        if(!phone_utf8_valid(input,257))return 0;
        const char *p=input;
        while(*p) {
            const char *begin=p;(void)phone_utf8_next(&p);size_t n=(size_t)(p-begin);
            if(used+n>60) {clipped=1;break;}
            memcpy(out+used,begin,n);used+=n;
        }
    } else {
        size_t length=strlen(input);if(length%4)return 0;
        for(size_t i=0;i<length;i+=4) {
            uint32_t cp;if(!sig_hex4(input+i,&cp) || cp<32 || cp==0x7f)return 0;
            if(cp>=0xd800 && cp<=0xdbff) {
                uint32_t lo;i+=4;
                if(i>=length || !sig_hex4(input+i,&lo) || lo<0xdc00 || lo>0xdfff)return 0;
                cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);
            } else if(cp>=0xdc00 && cp<=0xdfff)return 0;
            char bytes[4];unsigned n;
            if(cp<0x80) {bytes[0]=(char)cp;n=1;}
            else if(cp<0x800) {bytes[0]=(char)(0xc0|(cp>>6));bytes[1]=(char)(0x80|(cp&63));n=2;}
            else if(cp<0x10000) {bytes[0]=(char)(0xe0|(cp>>12));bytes[1]=(char)(0x80|((cp>>6)&63));bytes[2]=(char)(0x80|(cp&63));n=3;}
            else {bytes[0]=(char)(0xf0|(cp>>18));bytes[1]=(char)(0x80|((cp>>12)&63));bytes[2]=(char)(0x80|((cp>>6)&63));bytes[3]=(char)(0x80|(cp&63));n=4;}
            if(used+n>60)clipped=1;
            if(!clipped) {memcpy(out+used,bytes,n);used+=n;}
        }
    }
    if(clipped) {memcpy(out+used,"...",3);used+=3;}
    out[used]=0;return used>0;
}
int signal_parse_operator(const char *line,signal_sample_t *s)
{
    if(!line || !s || strncmp(line,"+QSPN:",6))return 0;
    const char *p=line+6;char names[3][257],part[257];long alphabet;
    for(unsigned i=0;i<3;++i)if(sig_field(&p,names[i],sizeof names[i])!=1)return 0;
    if(sig_field(&p,part,sizeof part)!=1 || !sig_number(part,0,1,10,&alphabet))return 0;
    if(sig_field(&p,part,sizeof part)!=0)return 0;
    const unsigned order[]={2,1,0};char decoded[64];
    for(unsigned i=0;i<3;++i)if(sig_decode_name(names[order[i]],(int)alphabet,decoded)) {
        strcpy(s->operator_name,decoded);s->valid|=SIGNAL_OPERATOR;return 1;
    }
    return 0;
}
void signal_half_db(char *out,size_t cap,int raw)
{
    int v=raw-47;unsigned mag=(unsigned)(v<0?-v:v);
    (void)snprintf(out,cap,"%s%u.%u",v<0?"-":"",mag/2,mag%2?5:0);
}
unsigned signal_visible(unsigned rotation)
{ return ((((rotation&1u)?160u:128u)-20u-48u)/14u); }
static void sig_record(const signal_sample_t *s,unsigned row,char out[128])
{
    char value[24]="--";unsigned mask=0;const char *label="";
    switch(row) {
    case 0:label="RSRP";mask=SIGNAL_RSRP;if(s->valid&mask)(void)snprintf(value,sizeof value,"%d dBm",s->rsrp);break;
    case 1:label="SINR";mask=SIGNAL_SINR;if(s->valid&mask) {signal_half_db(value,sizeof value,s->sinr_raw);strcat(value," dB");}break;
    case 2:label="RSRQ";mask=SIGNAL_RSRQ;if(s->valid&mask)(void)snprintf(value,sizeof value,"%d dB",s->rsrq);break;
    case 3:label="RSSI";mask=SIGNAL_RSSI;if(s->valid&mask)(void)snprintf(value,sizeof value,"%d dBm",s->rssi);break;
    case 4:label="Band";if(s->valid&SIGNAL_BAND)(void)snprintf(value,sizeof value,"B%u",s->band);break;
    case 5:label="Cell ID";if(s->valid&SIGNAL_CELL)(void)snprintf(value,sizeof value,"0x%07lX",(unsigned long)s->cell_id);break;
    case 6:(void)snprintf(out,128,"运营商:%s",s->valid&SIGNAL_OPERATOR?s->operator_name:"未知");return;

    }
    (void)snprintf(out,128,"%s:%s",label,value);
}
unsigned signal_line(const signal_sample_t *s,unsigned units,unsigned index,char *out,size_t cap)
{
    unsigned line=0;char record[128],wrapped[128];
    for(unsigned row=0;row<7u;++row) {
        sig_record(s,row,record);const char *p=record;
        do {
            size_t n=phone_wrap_line(p,units,wrapped,sizeof wrapped);if(!n)break;
            if(line++==index) {if(cap)(void)snprintf(out,cap,"%s",wrapped);return 1;}
            p+=n;
        } while(*p);
    }
    if(cap)*out=0;
    return 0;
}
unsigned signal_lines(const signal_sample_t *s,unsigned units)
{ unsigned n=0;while(signal_line(s,units,n,NULL,0))++n;return n; }
