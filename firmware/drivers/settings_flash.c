#include "settings_store.h"
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
/* This driver has no application-address or mass-erase entry point. */
#define ERRORS (FLASH_SR_OPERR|FLASH_SR_WRPERR|FLASH_SR_PGAERR|FLASH_SR_PGPERR|FLASH_SR_PGSERR)
static void unlock(void)
{if(FLASH->CR&FLASH_CR_LOCK) {FLASH->KEYR=0x45670123u;FLASH->KEYR=0xCDEF89ABu;}}
static void refresh_cache(void)
{
    uint32_t acr=FLASH->ACR;FLASH->ACR=acr&~FLASH_ACR_DCEN;
    FLASH->ACR=(acr&~FLASH_ACR_DCEN)|FLASH_ACR_DCRST;
    FLASH->ACR=acr&~FLASH_ACR_DCRST;__DSB();__ISB();
}
static void read_flash(uint32_t address,void *out,unsigned n)
{memcpy(out,(const void *)(uintptr_t)address,n);}
static int program(uint32_t address,uint32_t value)
{
    if(address<SETTINGS_FLASH_BASE || address>=SETTINGS_FLASH_BASE+2*SETTINGS_SECTOR_BYTES || address&3u || FLASH->SR&FLASH_SR_BSY)return 0;
    uint32_t mask=__get_PRIMASK();__disable_irq();unlock();FLASH->SR=ERRORS|FLASH_SR_EOP;
    FLASH->CR=(FLASH->CR&~(FLASH_CR_PSIZE|FLASH_CR_SER|FLASH_CR_SNB|FLASH_CR_MER))|FLASH_CR_PSIZE_1|FLASH_CR_PG;
    *(volatile uint32_t *)(uintptr_t)address=value;
    while(FLASH->SR&FLASH_SR_BSY) {}
    int ok=!(FLASH->SR&ERRORS);FLASH->CR&=~FLASH_CR_PG;FLASH->CR|=FLASH_CR_LOCK;
    refresh_cache();__set_PRIMASK(mask);return ok;
}
static int erase(unsigned sector)
{
    if(sector>1 || xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED || FLASH->SR&FLASH_SR_BSY)return 0;
    /* settings_store_prepare calls this before scheduler/AT initialization. */
    unlock();FLASH->SR=ERRORS|FLASH_SR_EOP;
    FLASH->CR=(FLASH->CR&~(FLASH_CR_PSIZE|FLASH_CR_SNB|FLASH_CR_PG|FLASH_CR_MER))|FLASH_CR_PSIZE_1|FLASH_CR_SER|((14u+sector)<<FLASH_CR_SNB_Pos);
    FLASH->CR|=FLASH_CR_STRT;while(FLASH->SR&FLASH_SR_BSY) {}
    int ok=!(FLASH->SR&ERRORS);FLASH->CR&=~FLASH_CR_SER;FLASH->CR|=FLASH_CR_LOCK;refresh_cache();return ok;
}
const settings_io_t settings_flash_io={read_flash,program,erase};
