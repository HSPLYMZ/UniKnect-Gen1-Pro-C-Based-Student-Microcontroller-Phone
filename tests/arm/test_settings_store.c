#include "common.h"
#include <string.h>
#include "../../firmware/app/settings_store.c"
#define memory ((uint8_t *)(uintptr_t)SETTINGS_FLASH_BASE)
static int budget=-1,erase_fail;
static unsigned programs,erases;
static void read_mem(uint32_t a,void *out,unsigned n) {CHECK(a>=SETTINGS_FLASH_BASE && a+n<=SETTINGS_FLASH_BASE+(2*SETTINGS_SECTOR_BYTES));memcpy(out,memory+a-SETTINGS_FLASH_BASE,n);}
static int program_mem(uint32_t a,uint32_t v)
{
    if(budget==0)return 0;if(budget>0)--budget;
    unsigned at=a-SETTINGS_FLASH_BASE;uint32_t old;memcpy(&old,memory+at,4);CHECK((old&v)==v);
    memcpy(memory+at,&v,4);++programs;return 1;
}
static int erase_mem(unsigned sector) {CHECK(sector<2);++erases;memset(memory+sector*SETTINGS_SECTOR_BYTES,255,erase_fail?1024:SETTINGS_SECTOR_BYTES);return !erase_fail;}
static const settings_io_t io={read_mem,program_mem,erase_mem};
static settings_store_t store,loaded;
static void complete(settings_config_t *c) {CHECK(settings_store_begin(&store,c)==0);for(unsigned i=0;i<16;++i)CHECK(settings_store_step(&store)==(i==15?1:0));}
void test_main(void)
{
    settings_config_t a,b;settings_defaults(&a);CHECK(settings_valid(&a));
    memset(memory,255,(2*SETTINGS_SECTOR_BYTES));settings_store_scan(&store,&io);CHECK(!store.valid && settings_equal(&a,&store.config));
    CHECK(settings_store_prepare(&store));CHECK(settings_store_begin(&store,&a)==1 && !programs);
    a.rotation=1;complete(&a);CHECK(store.sequence==1 && !erases);
    static uint8_t saved[64];memcpy(saved,memory,64);b=a;b.rotation=2;
    for(int cut=0;cut<=16;++cut) {
        memset(memory,255,(2*SETTINGS_SECTOR_BYTES));memcpy(memory,saved,64);settings_store_scan(&store,&io);
        budget=cut;CHECK(settings_store_begin(&store,&b)==0);
        for(unsigned i=0;i<16 && store.busy;++i)(void)settings_store_step(&store);
        budget=-1;settings_store_scan(&loaded,&io);
        CHECK(settings_equal(&loaded.config,cut==16?&b:&a));
    }
    memcpy(memory,saved,64);memset(memory+64,255,(2*SETTINGS_SECTOR_BYTES)-64);
    /* CRC, format version, invalid ADC, and missing commit cannot supersede old data. */
    uint32_t w[16];memcpy(w,saved,64);w[2]=2;w[3]=2;w[14]=crc(w);
    for(unsigned fault=0;fault<4;++fault) {
        uint32_t bad[16];memcpy(bad,w,64);
        if(fault==0)bad[14]^=1;
        if(fault==1) {bad[1]=9;bad[14]=crc(bad);}
        if(fault==2) {bad[4]=0;bad[14]=crc(bad);}
        if(fault==3)bad[15]=~0u;
        memcpy(memory+64,bad,64);settings_store_scan(&loaded,&io);CHECK(settings_equal(&loaded.config,&a));
    }
    memset(memory,255,(2*SETTINGS_SECTOR_BYTES));memcpy(w,saved,64);w[2]=0xffffffffu;w[14]=crc(w);memcpy(memory,w,64);
    settings_store_scan(&store,&io);complete(&b);CHECK(store.sequence==1);settings_store_scan(&loaded,&io);CHECK(settings_equal(&loaded.config,&b));
    /* Mark active journal full without thousands of flash writes. */
    memset(memory,0,SETTINGS_SECTOR_BYTES);memcpy(memory,saved,64);memset(memory+SETTINGS_SECTOR_BYTES,255,SETTINGS_SECTOR_BYTES);
    settings_store_scan(&store,&io);CHECK(store.cursor[0]==RECORDS);unsigned before=erases;complete(&b);
    CHECK(store.active==1 && erases==before);settings_store_scan(&loaded,&io);CHECK(settings_equal(&loaded.config,&b));
    erase_fail=1;CHECK(!settings_store_prepare(&loaded));settings_store_scan(&store,&io);CHECK(settings_equal(&store.config,&b));
    erase_fail=0;CHECK(settings_store_prepare(&store));CHECK(!store.cursor[0]);
    /* Full journals never trigger runtime erase. */
    memset(memory,0,(2*SETTINGS_SECTOR_BYTES));memcpy(memory,saved,64);settings_store_scan(&store,&io);
    before=erases;CHECK(settings_store_begin(&store,&b)==-2 && erases==before);
    b.idle=100;CHECK(!settings_valid(&b));b=a;b.levels[1]=b.levels[0];CHECK(!settings_valid(&b));
    test_finish(0);
}
