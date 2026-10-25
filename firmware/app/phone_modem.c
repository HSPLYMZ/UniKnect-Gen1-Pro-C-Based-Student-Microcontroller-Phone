#include "phone_modem.h"
#include <string.h>
#include <stdio.h>

int phone_modem_clock(const char *line,phone_event_t *event)
{
    if(!line || !event) return 0;
    unsigned year,month,day,hour,minute,second,offset;
    char sign;int end=0;
    if(!strncmp(line,"+QLTS:",6)) {
        /* Queried ONLY with AT+QLTS=2: current local network time, not
         * QLTS=0 (stale last update) or QLTS=1 (GMT with local TZ suffix). */
        unsigned dst;
        if(sscanf(line,"+QLTS: \"%4u/%2u/%2u,%2u:%2u:%2u%c%2u,%1u\"%n",
            &year,&month,&day,&hour,&minute,&second,&sign,&offset,&dst,&end)!=9 || year<2024u || year>2099u || dst>2u) return 0;
        year-=2000u;
    } else if(!strncmp(line,"+CCLK:",6)) {
        if(sscanf(line,"+CCLK: \"%2u/%2u/%2u,%2u:%2u:%2u%c%2u\"%n",
            &year,&month,&day,&hour,&minute,&second,&sign,&offset,&end)!=8) return 0;
    } else return 0;
    if(!end || (sign!='+' && sign!='-') || offset>56u || (sign=='-' && offset>48u)) return 0;
    for(const char *p=line+end;*p;++p) if(*p!='\r' && *p!='\n' && *p!=' ') return 0;
    /* A reset/unset modem RTC must not replace a valid host clock. */
    if(year<24u || year>99u || month>12u || day>31u || hour>23u || minute>59u || second>59u) return 0;
    phone_datetime_t date={(uint16_t)(2000u+year),(uint8_t)month,(uint8_t)day,(uint8_t)hour,(uint8_t)minute,(uint8_t)second};
    uint32_t as_beijing;
    if(!phone_time_from_beijing(&date,&as_beijing)) return 0;
    int64_t utc=(int64_t)as_beijing+28800-(sign=='+'?1:-1)*(int64_t)offset*900;
    if(utc<0 || utc>UINT32_MAX) return 0;
    phone_datetime_t check;
    if(!phone_time_to_beijing((uint32_t)utc,&check)) return 0;
    memset(event,0,sizeof *event);event->kind=PHONE_EVENT_CLOCK;event->source=PHONE_SOURCE_MODEM;
    event->value.utc=(uint32_t)utc;return 1;
}

/* Bounded CSV decoder for quoted/unquoted AT fields. No heap, no escapes. */
static int field(const char **cursor, char out[32])
{
    const char *p=*cursor;
    while (*p==' ') ++p;
    int quoted=*p=='"'; if(quoted) ++p;
    unsigned n=0;
    while (*p && (quoted ? *p!='"' : *p!=',' && *p!='\r' && *p!='\n')) {
        if(n==31) return -1;
        out[n++]=*p++;
    }
    if(quoted) { if(*p!='"') return -1; ++p; }
    while(n && out[n-1]==' ') --n;
    out[n]=0;
    while(*p==' ') ++p;
    if(*p && *p!=',' && *p!='\r' && *p!='\n') return -1;
    int more=*p==',';
    if(more) ++p;
    else { while(*p=='\r' || *p=='\n') ++p; if(*p) return -1; }
    *cursor=p; return more;
}
static int rsrp_value(const char *s, int16_t *value)
{
    if(*s++!='-' || *s<'0' || *s>'9') return 0;
    int n=0;
    while(*s>='0' && *s<='9') { n=n*10+*s++-'0'; if(n>140) return 0; }
    if(*s || n<44) return 0;
    *value=(int16_t)-n; return 1;
}
int phone_modem_qeng(const char *line, phone_event_t *event)
{
    if(!line || !event || strncmp(line,"+QENG:",6)) return 0;
    const char *p=line+6; char part[32],state[32]={0},rat[32]={0},rsrp[32]={0};
    unsigned count=0;
    for(;;) {
        int more=field(&p,part); if(more<0 || count>=24) return 0;
        if(!count && strcmp(part,"servingcell")) return 0;
        if(count==1) strcpy(state,part);
        if(count==2) strcpy(rat,part);
        if(count==13) strcpy(rsrp,part);
        ++count; if(!more) break;
    }
    int registered=!strcmp(state,"NOCONN") || !strcmp(state,"CONNECT");
    if(count<2 || (!registered && strcmp(state,"SEARCH") && strcmp(state,"LIMSRV"))) return 0;
    phone_event_t parsed={.kind=PHONE_EVENT_NETWORK,.source=PHONE_SOURCE_MODEM};
    parsed.value.network.enabled=1; parsed.value.network.registered=registered;
    parsed.value.network.lte=!strcmp(rat,"LTE");
    if(parsed.value.network.lte) {
        if(count!=18) return 0;
        if(strcmp(rsrp,"-") && strcmp(rsrp,"")) {
            if(!rsrp_value(rsrp,&parsed.value.network.rsrp_dbm)) return 0;
            parsed.value.network.signal_valid=registered;
        }
    } else if(registered && !*rat) return 0;
    *event=parsed; return 1;
}
