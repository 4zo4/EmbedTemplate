#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifdef ARCH_RISCV
/* GD32VF103 Specifics */
#define configMTIME_BASE_ADDRESS (0xD1000000UL)
#define configMTIMECMP_BASE_ADDRESS (0xD1000008UL)
#define configCPU_CLOCK_HZ ((unsigned long)108000000) // 108MHz
#define configMAX_PRIORITIES (7)
#elif defined(ARCH_ARM)
/* STM32F4 Specifics */
#define configCPU_CLOCK_HZ ((unsigned long)168000000) // 168MHz
#define configMAX_PRIORITIES (5)
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0xf
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5 << (8 - configPRIO_BITS))
#elif defined(ARCH_X86)
#define configCPU_CLOCK_HZ ((unsigned long)1000000)
#define configMAX_PRIORITIES (7)
#define configKERNEL_INTERRUPT_PRIORITY 1
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 1
#endif

/* --- Global Settings --- */
#define configUSE_IDLE_HOOK (1)
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMINIMAL_STACK_SIZE ((unsigned short)256)
/* Kernel Settings */
#define configUSE_PREEMPTION 1
#define configUSE_TICKLESS_IDLE 0
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
#define configUSE_NEWLIB_REENTRANT 0

/* Memory Management */
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configSUPPORT_STATIC_ALLOCATION 1

/* Hook Functions */
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 0
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