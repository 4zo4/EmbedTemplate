/**
 * @file startup.c
 * @brief Unified startup and interrupt handling for GD32VF103 (RISC-V).
 *
 * This file provides a library-agnostic entry point for both Newlib and Picolibc
 * environments. It handles:
 *  - Core initialization (Global Pointer and Stack Pointer setup).
 *  - Memory initialization (Copying .data and zeroing .bss).
 *  - Unified trap configuration (mtvec) for FreeRTOS and bare-metal dispatch.
 *  - Hardware-specific ECLIC vector table setup for the GD32VF103 chip.
 */
#include <stdint.h>
#include <stdio.h>
#include <arch/arch_ops.h>

#ifdef TARGET_HW_GD32VF103
#define IRQ_HANDLER __attribute__((interrupt))
#else
#define IRQ_HANDLER
#endif

int  main(void);
void freertos_risc_v_trap_handler(void);

extern uint32_t _sdata, _edata, _sbss, _ebss, _sidata, _estack, __global_pointer$;

void IRQ_HANDLER timer_handler(void);
void IRQ_HANDLER UART0_irq_handler(void);
void IRQ_HANDLER default_irq_handler(void)
{
    printf("[HALT] System locked. Waiting for power-on reset (POR)...\r\n");
    while (true) {
        HALT_CPU();
    }
}

void (*const vector_table[])(void) __attribute__((section(".vectors"), aligned(64))) = {
    [0 ... 2] = default_irq_handler,
#ifdef ENABLE_RTOS
    [3] = freertos_risc_v_trap_handler,
    [4 ... 6] = default_irq_handler,
    [7] = freertos_risc_v_trap_handler,
#else // !ENABLE_RTOS
    [3 ... 6] = default_irq_handler,
    [7] = timer_handler, // 7: Machine Timer
#endif
    // Peripheral Interrupts
    [56] = UART0_irq_handler, // 56: UART0
};

void __attribute__((interrupt, aligned(4))) trap_dispatch(void) 
{
    uintptr_t mcause;
    __asm volatile("csrr %0, mcause" : "=r"(mcause));

    if (mcause & (1ULL << (sizeof(uintptr_t) * 8 - 1))) {
        uintptr_t code = mcause & 0x3FF;

        switch (code) {
        case 7:  // Machine Timer Interrupt (MTIME)
            timer_handler(); 
            break;
        case 11: // Machine External Interrupt (From PLIC)
            UART0_irq_handler(); 
            break;
        default:
            printf("[CRITICAL] Unhandled Interrupt %lu Detected!\r\n", (unsigned long)code);
            default_irq_handler();
            break;
        }
    } else {
        printf("[CRITICAL] Unhandled Exception %lu Detected!\r\n", (unsigned long)mcause);
        default_irq_handler(); // Exception (Illegal Instruction, Load Fault, etc.)
    }
}

void __attribute__((section(".init"), naked)) _start(void)
{
    __asm volatile(".option push; .option norelax; la gp, __global_pointer$; .option pop"); // Set Global Pointer (GP) for RISC-V ABI
    __asm volatile("la sp, __stack");                                                       // Set Stack Pointer (SP) to top of stack

    // Copy .data from FLASH to RAM
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    // Zero out .bss
    dst = &_sbss;
    while (dst < &_ebss)
        *dst++ = 0;

#ifdef TARGET_HW_GD32VF103
    // Real Hardware: ECLIC Mode (Vector Table + Mode Bits 0x3)
    __asm volatile (
        ".option push\n"
        ".option norelax\n"
        "la t0, vector_table\n"
        "ori t0, t0, 0x3\n"
        "csrw mtvec, t0\n"
        ".option pop"
    );
#else // Renode simulated hardware
#ifdef ENABLE_RTOS
    // Renode RTOS: Direct Mode to FreeRTOS Assembly Handler
    __asm volatile("la t0, freertos_risc_v_trap_handler; csrw mtvec, t0");
#else
    // Renode non-RTOS: Direct Mode to IRQ Dispatcher
    __asm volatile (
        ".option push\n"
        ".option norelax\n"
        "lui t0, %%hi(trap_dispatch)\n"
        "addi t0, t0, %%lo(trap_dispatch)\n"
        "csrw mtvec, t0\n"
        ".option pop\n"
        : : : "t0"
    );
#endif // ENABLE_RTOS
#endif // TARGET_HW_GD32VF103

#ifdef __cplusplus
    extern void __libc_init_array(void);
    __libc_init_array();
#endif
    __asm volatile("j main"); // Jump to main
}

#ifdef ENABLE_RTOS
/*
 * Application-level interrupt handler for RISC-V FreeRTOS
 */
void freertos_risc_v_application_interrupt_handler(uint32_t mcause)
{
    // mcause 11 is Machine External Interrupt (PLIC)
    if ((mcause & 0x3FF) == 11) {
        UART0_irq_handler();
    }
}
#endif