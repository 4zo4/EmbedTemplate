#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Kernel Settings */
#define configUSE_PREEMPTION 1
#define configUSE_TICKLESS_IDLE 0
#define configCPU_CLOCK_HZ \
    ((unsigned long)1000000)                  /* Ignored by POSIX port \
                                               */
#define configTICK_RATE_HZ ((TickType_t)1000) /* 1ms Tick */
#define configMAX_PRIORITIES (7)
#define configMINIMAL_STACK_SIZE \
    ((unsigned short)128) /* Specified in words \
                           */
#define configMAX_TASK_NAME_LEN (16)
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 8
#define configUSE_QUEUE_SETS 0
#define configUSE_TIME_SLICING 1
#define configUSE_NEWLIB_REENTRANT 0 /* Keep 0 for TFA libc */

/* Memory Management */
/* For POSIX, heap_3.c is standard as it wraps malloc/free */
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configSUPPORT_STATIC_ALLOCATION 1
// configTOTAL_HEAP_SIZE is not used by heap_3.c

/* Hook Functions */
#define configUSE_IDLE_HOOK 1
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 0 /* Limited utility in POSIX simulator */
#define configUSE_MALLOC_FAILED_HOOK 0

/* Run-time and Task Stats */
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* Software Timer Definitions */
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

/* Interrupt Nesting - POSIX port specific */
/* These are placeholders; the POSIX port uses signals to simulate interrupts */
#define configKERNEL_INTERRUPT_PRIORITY 1
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 1

/* API Includes */
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskCleanUpResources 0
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTimerPendFunctionCall 1

/* Assert Definition */
#include <assert.h>
#define configASSERT(x) assert(x)

#endif /* FREERTOS_CONFIG_H */