#include "common.h"
#include <errno.h>
#include <sys/stat.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
volatile int test_status = -1;
__attribute__((noreturn)) void test_finish(int status)
{
    test_status = status;
    __asm volatile("bkpt #0");
    for (;;) { }
}
void board_fault_report(uint32_t code) { test_finish((int)code); }
uint32_t SystemCoreClock = 100000000u;
static uint8_t heap[110 * 1024] __attribute__((aligned(8)));
void *_sbrk(ptrdiff_t n)
{
    static size_t used;
    if (n < 0 || (size_t)n > sizeof heap - used) { errno = ENOMEM; return (void *)-1; }
    void *p = heap + used; used += n; return p;
}
int _write(int fd, char *p, int n) { (void)fd; (void)p; return n; }
int _read(int fd, char *p, int n) { (void)fd; (void)p; (void)n; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void _exit(int n) { test_finish(n ? n : 9999); }
BaseType_t test_scheduler = taskSCHEDULER_RUNNING;
static UBaseType_t suspend_depth;
BaseType_t xTaskGetSchedulerState(void) { return suspend_depth ? taskSCHEDULER_SUSPENDED : test_scheduler; }
void vTaskSuspendAll(void) { ++suspend_depth; }
BaseType_t xTaskResumeAll(void) { CHECK(suspend_depth > 0); --suspend_depth; return pdTRUE; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
TickType_t xTaskGetTickCount(void) { static TickType_t n; return n++; }
QueueHandle_t xQueueGenericCreateStatic(UBaseType_t a, UBaseType_t b, uint8_t *c,
                                       StaticQueue_t *d, uint8_t e)
{ (void)a; (void)b; (void)c; (void)e; return (QueueHandle_t)d; }
unsigned test_semaphore_successes;
BaseType_t xQueueSemaphoreTake(QueueHandle_t q, TickType_t t)
{
    (void)q;
    if(t && test_semaphore_successes) { --test_semaphore_successes; return pdTRUE; }
    return pdFALSE;
}
BaseType_t xQueueGenericSendFromISR(QueueHandle_t q, const void *p, BaseType_t *w, BaseType_t pos)
{ (void)q; (void)p; (void)w; (void)pos; return pdTRUE; }
BaseType_t xQueueGiveFromISR(QueueHandle_t q, BaseType_t *w)
{ (void)q; (void)w; return pdTRUE; }
