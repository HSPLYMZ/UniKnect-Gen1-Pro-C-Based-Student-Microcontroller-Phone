#include "settings_store.h"
#include <string.h>
#define RECORDS (SETTINGS_SECTOR_BYTES/SETTINGS_RECORD_BYTES)
#define MAGIC 0x554B5331u
#define COMMIT 0x43544D31u
static uint32_t crc(const uint32_t *w)
{
    uint32_t v=~0u;
    for(unsigned i=0;i<56;++i) {
        v^=((const uint8_t *)w)[i];
        for(unsigned b=0;b<8;++b)v=(v>>1)^(0xEDB88320u&(0u-(v&1u)));
    }
    return ~v;
}
static uint32_t address(unsigned sector,unsigned record)
{return SETTINGS_FLASH_BASE+sector*SETTINGS_SECTOR_BYTES+record*SETTINGS_RECORD_BYTES;}
static int blank(const uint32_t *w)
{for(unsigned i=0;i<16;++i)if(w[i]!=~0u)return 0;return 1;}
static int decode(const uint32_t *w,settings_config_t *c)
{
    if(w[0]!=MAGIC || w[1]!=1 || !w[2] || w[15]!=COMMIT || w[14]!=crc(w) || w[3]&0xff000000u)return 0;
    memset(c,0,sizeof *c);c->rotation=(uint8_t)w[3];c->calibration_rotation=(uint8_t)(w[3]>>8);c->manual=(uint8_t)(w[3]>>16);
    c->idle=(uint16_t)w[4];c->levels[0]=(uint16_t)(w[4]>>16);
    c->levels[1]=(uint16_t)w[5];c->levels[2]=(uint16_t)(w[5]>>16);
    c->levels[3]=(uint16_t)w[6];c->levels[4]=(uint16_t)(w[6]>>16);
    return settings_valid(c);
}
void settings_store_scan(settings_store_t *s,const settings_io_t *io)
{
    memset(s,0,sizeof *s);s->io=io;settings_defaults(&s->config);
    uint32_t w[16];settings_config_t c;
    for(unsigned sector=0;sector<2;++sector)for(unsigned i=0;i<RECORDS;++i) {
        io->read(address(sector,i),w,sizeof w);
        if(!blank(w))s->cursor[sector]=i+1;
        if(decode(w,&c) && (!s->valid || (int32_t)(w[2]-s->sequence)>0)) {
            s->config=c;s->sequence=w[2];s->active=sector;s->valid=1;
        }
    }
}
int settings_store_prepare(settings_store_t *s)
{
    if(s->valid) {
        unsigned other=s->active^1u;
        if(s->cursor[other] && !s->io->erase(other))return 0;
        s->cursor[other]=0;
    } else {
        for(unsigned i=0;i<2;++i) {
            if(s->cursor[i] && !s->io->erase(i))return 0;
            s->cursor[i]=0;
        }
    }
    return 1;
}
int settings_store_begin(settings_store_t *s,const settings_config_t *c)
{
    if(s->busy || !settings_valid(c))return -1;
    if(settings_equal(c,&s->config))return 1;
    unsigned sector=s->active;
    if(s->cursor[sector]>=RECORDS)sector^=1u;
    if(s->cursor[sector]>=RECORDS)return -2;
    memset(s->words,0,sizeof s->words);s->words[0]=MAGIC;s->words[1]=1;
    s->words[2]=s->sequence+1u;if(!s->words[2])s->words[2]=1;
    s->words[3]=c->rotation|((uint32_t)c->calibration_rotation<<8)|((uint32_t)c->manual<<16);
    s->words[4]=c->idle|((uint32_t)c->levels[0]<<16);
    s->words[5]=c->levels[1]|((uint32_t)c->levels[2]<<16);
    s->words[6]=c->levels[3]|((uint32_t)c->levels[4]<<16);
    s->words[14]=crc(s->words);s->words[15]=COMMIT;
    s->pending=*c;s->target=sector;s->word=0;s->busy=1;s->error=0;return 0;
}
int settings_store_step(settings_store_t *s)
{
    if(!s->busy)return s->error?-1:1;
    uint32_t a=address(s->target,s->cursor[s->target])+s->word*4u;
    uint32_t readback;
    s->io->read(a,&readback,4);
    if(readback!=~0u || !s->io->program(a,s->words[s->word]))goto failed;
    s->io->read(a,&readback,4);if(readback!=s->words[s->word])goto failed;
    if(++s->word<16)return 0;
    s->active=s->target;++s->cursor[s->target];s->sequence=s->words[2];s->config=s->pending;
    s->valid=1;s->busy=0;return 1;
failed:
    ++s->cursor[s->target];s->busy=0;s->error=1;return -1;
}
