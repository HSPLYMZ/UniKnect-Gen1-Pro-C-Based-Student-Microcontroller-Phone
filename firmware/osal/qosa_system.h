/* OSAL surface required by the vendored at_client, implemented on native
 * FreeRTOS (firmware/osal/osal_freertos.c).
 *
 * Only the subset at_client actually calls is declared here. Deliberately no
 * msgq / timer / event wrappers yet: adding them later is additive, and an
 * unused declaration that has no definition is a link error waiting to happen.
 */
#ifndef QOSA_SYSTEM_H
#define QOSA_SYSTEM_H

#include "qosa_def.h"

/* Opaque handles, same shape as Quectel's headers so vendor code compiles. */
typedef void *osa_sem_t;
typedef void *osa_mutex_t;
typedef void *osa_task_t;

/* ---- task ------------------------------------------------------------- */
/* stack_size is in BYTES (the vendor call sites pass 4*1024 and mean bytes). */
int       qosa_task_create(osa_task_t *task_ref, u32_t stack_size, uint8_t priority,
                           char *task_name, void (*task_start)(void *), void *argv, ...);
int       qosa_task_delete(osa_task_t task_ref);
void      qosa_task_sleep_ms(u32_t ms);
void      qosa_task_sleep_sec(u32_t s);
osa_task_t qosa_task_get_current_ref(void);
u32_t     qosa_task_get_stack_space(osa_task_t task_ref);
size_t    qosa_task_get_free_heap_size(void);
int       qosa_task_exit(void);

/* ---- semaphore (counting) --------------------------------------------- */
int qosa_sem_create(osa_sem_t *sem_ref, u32_t initial_count);
int qosa_sem_wait(osa_sem_t sem_ref, u32_t timeout_ms);
int qosa_sem_release(osa_sem_t sem_ref);
int qosa_sem_get_cnt(osa_sem_t sem_ref, u32_t *cnt_ptr);
int qosa_sem_delete(osa_sem_t sem_ref);

/* ---- mutex (recursive-capable) ---------------------------------------- */
int qosa_mutex_create(osa_mutex_t *mutex_ref);
int qosa_mutex_try_lock(osa_mutex_t mutex_ref);
int qosa_mutex_lock(osa_mutex_t mutex_ref, u32_t timeout_ms);
int qosa_mutex_unlock(osa_mutex_t mutex_ref);
int qosa_mutex_delete(osa_mutex_t mutex_ref);

#endif /* QOSA_SYSTEM_H */
