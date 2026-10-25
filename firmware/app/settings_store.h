#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H
#include "settings_data.h"
#define SETTINGS_FLASH_BASE 0x08140000u
#define SETTINGS_SECTOR_BYTES 0x20000u
#define SETTINGS_RECORD_BYTES 64u
typedef struct {
    void (*read)(uint32_t address,void *out,unsigned bytes);
    int (*program)(uint32_t address,uint32_t word);
    int (*erase)(unsigned sector);
} settings_io_t;
typedef struct {
    settings_config_t config,pending;
    uint32_t sequence,words[16];
    unsigned cursor[2],active,target,word;
    uint8_t valid,busy,error;
    const settings_io_t *io;
} settings_store_t;
void settings_store_scan(settings_store_t *s,const settings_io_t *io);
/* Startup only: never called while the scheduler/AT services are running. */
int settings_store_prepare(settings_store_t *s);
int settings_store_begin(settings_store_t *s,const settings_config_t *c);
/* One word per call. 0 busy, 1 verified complete, -1 failed, -2 full. */
int settings_store_step(settings_store_t *s);
extern const settings_io_t settings_flash_io;
#endif
