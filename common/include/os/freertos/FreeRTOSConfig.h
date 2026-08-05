#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifdef ARCH_RISCV
/* GD32VF103 Specifics */
#ifdef TARGET_GD32VF103_VIRT                                  // QEMU Virt
#define configCPU_CLOCK_HZ (10000000UL)                       // 10 MHz QEMU CLINT clock rate
#define configMTIME_BASE_ADDRESS (0x02000000UL + 0xBFF8UL)    // QEMU Virt CLINT MTIME Offset
#define configMTIMECMP_BASE_ADDRESS (0x02000000UL + 0x4000UL) // QEMU Virt CLINT MTIMECMP Offset
#elif defined(TARGET_GD32VF103)                               // Renode GD32VF103
#define configCPU_CLOCK_HZ ((unsigned long)108000000)         // 108MHz
#define configMTIME_BASE_ADDRESS (0xD1000000UL)
#define configMTIMECMP_BASE_ADDRESS (0xD1000008UL)
#else                                                 // TARGET_GD32VF103_HW
#define configCPU_CLOCK_HZ ((unsigned long)108000000) // 108MHz
#define configMTIME_BASE_ADDRESS (0xD1000000UL + 0xBFF8UL)
#define configMTIMECMP_BASE_ADDRESS (0xD1000000UL + 0x4000UL)
#endif
#define configMAX_PRIORITIES (7)
#elif defined(ARCH_ARM)
#ifdef TARGET_CORTEX_A9_VIRT
/* cortex-a9-virt Specifics */
#define configCPU_CLOCK_HZ (24000000UL) // 24MHz on QEMU
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS (0x08000000UL)
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET (0x00010000UL)
#define configINTERRUPT_SPLIT_DEACTIVATION 1
#define configMAX_PRIORITIES (5)
#define configMAX_API_CALL_INTERRUPT_PRIORITY (18)
#define configKERNEL_INTERRUPT_PRIORITY (31)
#define configUNIQUE_INTERRUPT_PRIORITIES (32)
#define FreeRTOS_TICK_HANDLER FreeRTOS_Tick_Handler
#define FreeRTOS_SWI_HANDLER vPortYieldProcessor
#define configSETUP_TICK_INTERRUPT() init_systick()
#define configCLEAR_TICK_INTERRUPT() clear_systick()
#ifndef __ASSEMBLER__
void init_systick(void);
void clear_systick(void);
#endif // !__ASSEMBLER__
#endif // CORTEX_A9_VIRT
#ifdef TARGET_STM32F4
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
#endif // STM32F4
#elif defined(ARCH_X86)
#ifdef TARGET_X86_VIRT
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_COMMON_INTERRUPT_ENTRY_POINT 1
#define configUSE_IOAPIC_EOI 1
#define xPortIoapicEoiHandler ioapic_eoi
#ifndef __ASSEMBLER__
void ioapic_eoi(uint32_t vector_id);
#endif // !__ASSEMBLER__
#define configMAX_API_CALL_INTERRUPT_PRIORITY 15
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 15
#define configISR_STACK_SIZE 512        // 2KB
#define configCPU_CLOCK_HZ 1000000000UL // 1GHz
#define configEOI_ADDRESS 0xFEE000B0UL
#else // X86_HOST
#define configCPU_CLOCK_HZ 1000000UL
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 1
#endif
#define configMAX_PRIORITIES 7
#define configKERNEL_INTERRUPT_PRIORITY 1
#endif // ARCH_X86

/* --- Global Settings --- */
#define configUSE_IDLE_HOOK (1)
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMINIMAL_STACK_SIZE 256 // 1 KB
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
#define configUSE_TRACE_FACILITY 0
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
#ifndef __ASSEMBLER__
#include <assert.h>
#define configASSERT(x) assert(x)
#else
#define configASSERT(x)
#endif

#endif /* FREERTOS_CONFIG_H */