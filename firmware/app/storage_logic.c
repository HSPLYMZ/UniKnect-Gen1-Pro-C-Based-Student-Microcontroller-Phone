#include "storage_logic.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

int storage_format_record(char *out,size_t capacity,const storage_record_t *r)
{
    if(!out || !capacity || !r) return 0;
    /* newlib-nano omits %llu unless linked with float/long-long printf support. */
    char digits[21],reversed[21];
    uint64_t value=r->uptime_ms;
    size_t count=0;
    do { reversed[count++]=(char)('0'+value%10u);value/=10u; } while(value);
    for(size_t i=0;i<count;++i) digits[i]=reversed[count-i-1u];
    digits[count]=0;
    int n=snprintf(out,capacity,
        "\n{\"schema\":1,\"uptime_ms\":%s,\"ldr\":%u,\"joy\":%u,"
        "\"x_mg\":%d,\"y_mg\":%d,\"z_mg\":%d,\"lte_rsrp\":%d}\n",
        digits,r->light_raw,r->input_raw,r->x_mg,r->y_mg,r->z_mg,r->rsrp_dbm);
    return n>0 && (size_t)n<capacity ? n : 0;
}

static int decimal(const char **cursor, uint64_t maximum, uint64_t *out)
{
    const char *p=*cursor;
    uint64_t value=0;
    if(*p<'0' || *p>'9') return 0;
    do {
        unsigned digit=(unsigned)(*p-'0');
        if(value>(maximum-digit)/10u) return 0;
        value=value*10u+digit;
        ++p;
    } while(*p>='0' && *p<='9');
    *cursor=p;*out=value;return 1;
}

static int prefix(const char **cursor,const char *name)
{
    size_t n=strlen(name);
    if(strncmp(*cursor,name,n)) return 0;
    *cursor+=n;
    while(**cursor==' ') ++*cursor;
    return 1;
}

static int end(const char *p)
{
    while(*p=='\r' || *p=='\n' || *p==' ') ++p;
    return *p==0;
}

int storage_parse_space(const char *line,uint64_t *free_bytes,uint64_t *total_bytes)
{
    if(!line || !free_bytes || !total_bytes) return 0;
    const char *p=line;uint64_t free_value,total_value;
    if(!prefix(&p,"+QFLDS:") || !decimal(&p,UINT64_MAX,&free_value) || *p++!=',' ||
       !decimal(&p,UINT64_MAX,&total_value) || !end(p) || free_value>total_value) return 0;
    *free_bytes=free_value;*total_bytes=total_value;return 1;
}

static int parse_one(const char *line,const char *name,uint64_t max,uint64_t *value)
{
    if(!line || !value) return 0;
    const char *p=line;
    return prefix(&p,name) && decimal(&p,max,value) && end(p);
}

int storage_parse_handle(const char *line,int *handle)
{
    uint64_t value;
    if(!handle || !parse_one(line,"+QFOPEN:",INT_MAX,&value)) return 0;
    *handle=(int)value;return 1;
}

int storage_parse_position(const char *line,uint32_t *position)
{
    uint64_t value;
    if(!position || !parse_one(line,"+QFPOSITION:",UINT32_MAX,&value)) return 0;
    *position=(uint32_t)value;return 1;
}

int storage_parse_write(const char *line,uint32_t *written)
{
    if(!line || !written) return 0;
    const char *p=line;uint64_t value,total;
    if(!prefix(&p,"+QFWRITE:") || !decimal(&p,UINT32_MAX,&value) || *p++!=',' ||
       !decimal(&p,UINT32_MAX,&total) || !end(p) || value>total) return 0;
    *written=(uint32_t)value;return 1;
}
