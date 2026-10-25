#include "voice_logic.h"
#include <string.h>

static void spaces(const char **p) { while(**p==' ') ++*p; }
static int integer(const char **p,unsigned *out)
{
    spaces(p);
    if(**p<'0' || **p>'9') return 0;
    unsigned value=0;
    do { unsigned d=(unsigned)(**p-'0');if(value>100000u) return 0;
         value=value*10u+d;++*p; } while(**p>='0' && **p<='9');
    *out=value;return 1;
}
static int comma(const char **p) { spaces(p);if(**p!=',') return 0;++*p;return 1; }
static int prefix(const char **p,const char *name)
{
    size_t n=strlen(name);
    if(strncmp(*p,name,n)) return 0;
    *p+=n;return 1;
}
static int phone_number(const char **p,char out[PHONE_NUMBER_MAX+1])
{
    spaces(p);int quoted=**p=='"';if(quoted) ++*p;
    unsigned n=0;
    while(**p && (quoted ? **p!='"' : **p!=',' && **p!='\r' && **p!='\n')) {
        char c=**p;
        if(n>=PHONE_NUMBER_MAX || !((c>='0' && c<='9') || (c=='+' && n==0))) return 0;
        out[n++]=c;++*p;
    }
    if(quoted) {if(**p!='"') return 0;++*p;}
    out[n]=0;return 1;
}
int voice_parse_clcc(const char *line,voice_snapshot_t *out)
{
    if(!line || !out) return 0;
    const char *p=line;unsigned id,dir,stat,mode,mpty;
    if(!prefix(&p,"+CLCC:") || !integer(&p,&id) || !comma(&p) ||
       !integer(&p,&dir) || !comma(&p) || !integer(&p,&stat) || !comma(&p) ||
       !integer(&p,&mode) || !comma(&p) || !integer(&p,&mpty) ||
       !id || dir>1u || stat>5u || mpty>1u || mode!=0u) return 0;
    voice_snapshot_t result={.id=id};result.incoming=(uint8_t)dir;
    result.waiting=stat==5u;
    result.state=stat==0u || stat==1u?PHONE_CALL_ACTIVE:
        stat==2u || stat==3u?PHONE_CALL_DIALING:PHONE_CALL_INCOMING;
    if(comma(&p) && !phone_number(&p,result.number)) return 0;
    *out=result;return 1;
}
int voice_parse_dsci(const char *line,voice_snapshot_t *out)
{
    if(!line || !out) return 0;
    const char *p=line;unsigned id,dir,stat,type;
    if(!prefix(&p,"^DSCI:") || !integer(&p,&id) || !comma(&p) ||
       !integer(&p,&dir) || !comma(&p) || !integer(&p,&stat) || !comma(&p) ||
       !integer(&p,&type) || !id || dir>1u || stat<1u || stat>7u || type!=0u)
        return 0;
    voice_snapshot_t result={.id=id};result.incoming=(uint8_t)dir;
    result.waiting=stat==5u;
    result.state=stat==2u || stat==7u?PHONE_CALL_DIALING:
        stat==4u || stat==5u?PHONE_CALL_INCOMING:
        stat==6u?PHONE_CALL_ENDED:PHONE_CALL_ACTIVE;
    if(comma(&p) && !phone_number(&p,result.number)) return 0;
    *out=result;return 1;
}
int voice_parse_clip(const char *line,char number[PHONE_NUMBER_MAX+1])
{
    if(!line || !number) return 0;
    const char *p=line;
    if(!prefix(&p,"+CLIP:") || !phone_number(&p,number)) return 0;
    return 1;
}
