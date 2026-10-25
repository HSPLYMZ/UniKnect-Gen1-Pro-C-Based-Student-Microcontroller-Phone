/* Native FreeRTOS implementation of the small OSAL surface that the vendored
 * at_client needs. No CMSIS-RTOS2 layer: every call maps straight onto a
 * FreeRTOS primitive.
 *
 * Two details that are easy to get wrong and are handled here:
 *  1. at_client_rx_ind() is invoked from the UART ISR, and it calls
 *     qosa_sem_release(). A plain xSemaphoreGive() there would be illegal, so
 *     the release path checks IPSR and uses the FromISR variant.
 *  2. qosa_sem_wait() receives QOSA_WAIT_FOREVER ((u32_t)-1), which must become
 *     portMAX_DELAY rather than a 49-day timeout.
 */
#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "board.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_system.h"
#include "qosa_utils.h"

/* A "data available" semaphore only needs to hold a few pending wakeups; the
 * parser drains the ring buffer in a loop, so saturation is harmless. */
#define QOSA_SEM_MAX_COUNT  32u

/* ------------------------------------------------------------------ log --- */

LogLevel g_debug_level = LOG_INFO;

void qosa_log_write(int level, const char *tag, const char *func,
                    const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    int n;

    if (level < (int)g_debug_level) {
        return;
    }

    n = snprintf(buf, sizeof buf, "[%s] %s: ", tag, func ? func : "?");
    if (n < 0 || n >= (int)sizeof buf) {
        return;
    }

    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    va_end(ap);
    if (m < 0) {
        return;
    }

    board_log("%s", buf);
}

void qosa_assert_failed(const char *expr, const char *file, int line)
{
    board_log("ASSERT failed: %s (%s:%d)", expr ? expr : "?", file ? file : "?", line);
    board_fault_report(0xDEAD0006u);
}

/* ----------------------------------------------------------------- task --- */

int qosa_task_create(osa_task_t *task_ref, u32_t stack_size, uint8_t priority,
                     char *task_name, void (*task_start)(void *), void *argv, ...)
{
    if (task_ref == NULL || task_start == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }

    /* The vendor call sites pass bytes (e.g. 4*1024); FreeRTOS wants words. */
    configSTACK_DEPTH_TYPE words = (configSTACK_DEPTH_TYPE)(stack_size / sizeof(StackType_t));
    if (words < configMINIMAL_STACK_SIZE) {
        words = configMINIMAL_STACK_SIZE;
    }

    UBaseType_t prio = priority;
    if (prio >= configMAX_PRIORITIES) {
        prio = configMAX_PRIORITIES - 1u;
    }

    TaskHandle_t handle = NULL;
    BaseType_t ok = xTaskCreate((TaskFunction_t)task_start, task_name, words, argv,
                                prio, &handle);
    *task_ref = (osa_task_t)handle;
    return (ok == pdPASS) ? QOSA_OK : QOSA_ERROR_NO_MEMORY;
}

int qosa_task_delete(osa_task_t task_ref)
{
    vTaskDelete((TaskHandle_t)task_ref);
    return QOSA_OK;
}

void qosa_task_sleep_ms(u32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void qosa_task_sleep_sec(u32_t s)
{
    vTaskDelay(pdMS_TO_TICKS(s * 1000u));
}

osa_task_t qosa_task_get_current_ref(void)
{
    return (osa_task_t)xTaskGetCurrentTaskHandle();
}

u32_t qosa_task_get_stack_space(osa_task_t task_ref)
{
    return (u32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)task_ref) * sizeof(StackType_t);
}

size_t qosa_task_get_free_heap_size(void)
{
    return (size_t)xPortGetFreeHeapSize();
}

int qosa_task_exit(void)
{
    vTaskDelete(NULL);
    return QOSA_OK;
}

/* ------------------------------------------------------------ semaphore --- */

int qosa_sem_create(osa_sem_t *sem_ref, u32_t initial_count)
{
    if (sem_ref == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }
    if (initial_count > QOSA_SEM_MAX_COUNT) {
        initial_count = QOSA_SEM_MAX_COUNT;
    }

    SemaphoreHandle_t sem = xSemaphoreCreateCounting(QOSA_SEM_MAX_COUNT, initial_count);
    *sem_ref = (osa_sem_t)sem;
    return (sem != NULL) ? QOSA_OK : QOSA_ERROR_NO_MEMORY;
}

int qosa_sem_wait(osa_sem_t sem_ref, u32_t timeout_ms)
{
    if (sem_ref == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }

    TickType_t ticks = (timeout_ms == QOSA_WAIT_FOREVER) ? portMAX_DELAY
                                                         : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake((SemaphoreHandle_t)sem_ref, ticks) == pdTRUE)
               ? QOSA_OK
               : QOSA_ERROR_TIMEOUT;
}

int qosa_sem_release(osa_sem_t sem_ref)
{
    if (sem_ref == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }

    /* Called from the UART ISR through at_client_rx_ind(). */
    if (__get_IPSR() != 0u) {
        BaseType_t woken = pdFALSE;
        (void)xSemaphoreGiveFromISR((SemaphoreHandle_t)sem_ref, &woken);
        portYIELD_FROM_ISR(woken);
        return QOSA_OK;
    }

    return (xSemaphoreGive((SemaphoreHandle_t)sem_ref) == pdTRUE)
               ? QOSA_OK
               : QOSA_ERROR_GENERAL;
}

int qosa_sem_get_cnt(osa_sem_t sem_ref, u32_t *cnt_ptr)
{
    if (sem_ref == NULL || cnt_ptr == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }
    *cnt_ptr = (u32_t)uxSemaphoreGetCount((SemaphoreHandle_t)sem_ref);
    return QOSA_OK;
}

int qosa_sem_delete(osa_sem_t sem_ref)
{
    if (sem_ref != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)sem_ref);
    }
    return QOSA_OK;
}

/* ---------------------------------------------------------------- mutex --- */

int qosa_mutex_create(osa_mutex_t *mutex_ref)
{
    if (mutex_ref == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }

    SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    *mutex_ref = (osa_mutex_t)m;
    return (m != NULL) ? QOSA_OK : QOSA_ERROR_NO_MEMORY;
}

int qosa_mutex_try_lock(osa_mutex_t mutex_ref)
{
    if (mutex_ref == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }
    return (xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex_ref, 0) == pdTRUE)
               ? QOSA_OK
               : QOSA_ERROR_BUSY;
}

int qosa_mutex_lock(osa_mutex_t mutex_ref, u32_t timeout_ms)
{
    if (mutex_ref == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }

    TickType_t ticks = (timeout_ms == QOSA_WAIT_FOREVER) ? portMAX_DELAY
                                                         : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex_ref, ticks) == pdTRUE)
               ? QOSA_OK
               : QOSA_ERROR_TIMEOUT;
}

int qosa_mutex_unlock(osa_mutex_t mutex_ref)
{
    if (mutex_ref == NULL) {
        return QOSA_ERROR_PARAM_IS_NULL;
    }
    return (xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex_ref) == pdTRUE)
               ? QOSA_OK
               : QOSA_ERROR_GENERAL;
}

int qosa_mutex_delete(osa_mutex_t mutex_ref)
{
    if (mutex_ref != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)mutex_ref);
    }
    return QOSA_OK;
}

/* --------------------------------------------------------------- misc ----- */

uint64_t qosa_get_uptime_milliseconds(void)
{
    return (uint64_t)xTaskGetTickCount() * (uint64_t)portTICK_PERIOD_MS;
}
