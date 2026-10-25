/* Minimal OSAL type definitions for the vendored at_client.
 *
 * The vendored RT-Thread at_client (firmware/third_party/at_client) was written
 * against Quectel's "qosa" layer. We keep the interface names; local safety
 * patches are documented in third_party/at_client/LOCAL_CHANGES.md. Every
 * definition here targets native FreeRTOS directly: no CMSIS-RTOS2
 * wrapper, no Quectel platform code.
 */
#ifndef QOSA_DEF_H
#define QOSA_DEF_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int             qosa_bool_t;
typedef uint8_t         u8_t;
typedef int8_t          s8_t;
typedef uint16_t        u16_t;
typedef int16_t         s16_t;
typedef uint32_t        u32_t;
typedef int32_t         s32_t;
typedef int64_t         s64_t;
typedef uint64_t        u64_t;

#define QOSA_TRUE           1
#define QOSA_FALSE          0

#define QOSA_SUCCESS        0
#define QOSA_FAILURE        (-1)

#define QOSA_NO_WAIT        0u
#define QOSA_WAIT_FOREVER   ((u32_t)-1)
#define QOSA_MAX_DELAY      ((u32_t)-1)

#define QOSA_NULL           0

#define QOSA_UNUSED(x)      ((void)(x))

/* at_client.c multiplies timeouts by this (e.g. 5 * RT_TICK_PER_SECOND).
 * Must match configTICK_RATE_HZ in FreeRTOSConfig.h, which is 1000. */
#ifndef RT_TICK_PER_SECOND
#define RT_TICK_PER_SECOND  1000
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

/* Common error codes. QOSA_OK is 0 because at_client compares against it. */
typedef enum {
    QOSA_OK = 0,
    QOSA_ERROR_GENERAL = 1,
    QOSA_ERROR_NO_MEMORY,
    QOSA_ERROR_PARAM_INVALID,
    QOSA_ERROR_PARAM_IS_NULL,
    QOSA_ERROR_TIMEOUT,
    QOSA_ERROR_BUSY,
} qosa_error_e;

/* Priority mapping. FreeRTOSConfig.h has configMAX_PRIORITIES = 8, so 0..6
 * leaves room for the timer service task at 7. */
typedef enum {
    QOSA_PRIORITY_IDLE = 0,
    QOSA_PRIORITY_LOW = 1,
    QOSA_PRIORITY_BELOW_NORMAL = 2,
    QOSA_PRIORITY_NORMAL = 3,
    QOSA_PRIORITY_ABOVE_NORMAL = 4,
    QOSA_PRIORITY_HIGH = 5,
    QOSA_PRIORITY_REALTIME = 6,
} qosa_thread_priority_e;

/* Implemented in osal_freertos.c: reports through board_fault_report(). */
void qosa_assert_failed(const char *expr, const char *file, int line);

#define QOSA_ASSERT(a)                                              \
    do {                                                            \
        if (!(a)) {                                                 \
            qosa_assert_failed(#a, __FILE__, __LINE__);             \
        }                                                           \
    } while (0)

#endif /* QOSA_DEF_H */
