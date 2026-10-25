/* STM32 BSP -> ordered components -> native FreeRTOS task registry. */

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "app_runtime.h"
#if APP_MODEM_ENABLE
#include "at_probe.h"
#endif
#include "board.h"
#include "settings_service.h"
#if PHONE_PROBE_ENABLE
#include "phone_probe.h"
#endif

/* heap_4 with configAPPLICATION_ALLOCATED_HEAP: the RTOS heap lives in SRAM2,
 * away from DMA buffers that will sit in SRAM1. */
__attribute__((aligned(8), section(".freertos_heap")))
uint8_t ucHeap[configTOTAL_HEAP_SIZE];

static volatile uint32_t s_idle_count;

/* configSUPPORT_STATIC_ALLOCATION = 1 makes the kernel ask the application for
 * the idle and timer task storage. Keeping them static (not from the heap)
 * means those two tasks can never fail to start. */
static StaticTask_t s_idle_tcb;
static StackType_t  s_idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t s_timer_tcb;
static StackType_t  s_timer_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *stack_size)
{
    *tcb = &s_idle_tcb;
    *stack = s_idle_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                    configSTACK_DEPTH_TYPE *stack_size)
{
    *tcb = &s_timer_tcb;
    *stack = s_timer_stack;
    *stack_size = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationIdleHook(void)
{
    s_idle_count++;
}

uint32_t app_idle_counter(void) { return s_idle_count; }

void vApplicationMallocFailedHook(void)
{
    board_fault_report(0xDEAD0003u);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    board_console_write("STACK OVERFLOW\n", 15);
    /* Hook may run in PendSV: print only a bounded task name, no libc calls. */
    board_console_write("task=", 5);
    if(name) {
        for(unsigned i=0;i<configMAX_TASK_NAME_LEN && name[i];++i)
            board_console_putc(name[i]);
    }
    board_console_write("\n", 1);
    board_fault_report(0xDEAD0004u);
}

int main(void)
{
    board_init();
    settings_boot_init();
    app_framework_start();
#if APP_MODEM_ENABLE
    at_probe_start();
#endif
#if PHONE_PROBE_ENABLE
    phone_probe_start();
#endif

    board_log("scheduler start: framework_tasks=%u modem=%u lcd_diagnostic=%u heap=%u bytes",
              app_task_active_count(), APP_MODEM_ENABLE, APP_LCD_DIAGNOSTIC, (unsigned)configTOTAL_HEAP_SIZE);
    vTaskStartScheduler();

    /* Only reached when the heap could not hold the idle task. */
    board_fault_report(0xDEAD0005u);
    return 0;
}
