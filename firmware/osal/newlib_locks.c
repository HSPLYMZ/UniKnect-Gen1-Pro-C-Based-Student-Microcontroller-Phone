/* Single-core FreeRTOS newlib locks. Scheduler suspension is recursive and
 * leaves DMA/UART interrupts enabled. Fatal ISR logging bypasses libc. */
#include <sys/lock.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

struct __lock { unsigned char unused; };
struct __lock __lock___sinit_recursive_mutex;
struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___dd_hash_mutex;
struct __lock __lock___arc4random_mutex;
static struct __lock s_dynamic_lock;

void __retarget_lock_init(_LOCK_T *lock) { *lock = &s_dynamic_lock; }
void __retarget_lock_init_recursive(_LOCK_T *lock) { __retarget_lock_init(lock); }
void __retarget_lock_close(_LOCK_T lock) { (void)lock; }
void __retarget_lock_close_recursive(_LOCK_T lock) { (void)lock; }
void __retarget_lock_acquire(_LOCK_T lock)
{
    (void)lock;
    configASSERT(__get_IPSR() == 0u);
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) vTaskSuspendAll();
}
void __retarget_lock_acquire_recursive(_LOCK_T lock) { __retarget_lock_acquire(lock); }
void __retarget_lock_release(_LOCK_T lock)
{
    (void)lock;
    configASSERT(__get_IPSR() == 0u);
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) (void)xTaskResumeAll();
}
void __retarget_lock_release_recursive(_LOCK_T lock) { __retarget_lock_release(lock); }
int __retarget_lock_try_acquire(_LOCK_T lock) { __retarget_lock_acquire(lock); return 1; }
int __retarget_lock_try_acquire_recursive(_LOCK_T lock) { return __retarget_lock_try_acquire(lock); }
