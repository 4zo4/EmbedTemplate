/**
 * @file startup.c
 * @brief Startup code and interrupt vector table for GD32VF103.
 * This file defines the reset handler, default interrupt handlers, and the interrupt vector table for the GD32VF103 microcontroller.
 * It supports both the TARGET_LIB_PICO (RISC-V Pico SDK) and a standard reset_handler startup for newlibc-based environments.
 */
#include <stdint.h>
#include <arch/arch_ops.h>

int  main(void);
void freertos_risc_v_trap_handler(void);

extern uint32_t _sdata, _edata, _sbss, _ebss, _sidata, _estack;

void __attribute__((interrupt)) timer_handler(void);
void __attribute__((interrupt)) UART0_irq_handler(void);
void __attribute__((interrupt)) default_irq_handler(void)
{
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

static inline void init_vector_table(void)
{
    __asm volatile("csrw mtvec, %0" : : "r"((uintptr_t)vector_table | 0x1));
}

#ifdef TARGET_LIB_PICO
void __attribute__((used)) software_init_hook(void)
{
    init_vector_table();
}
#else // Standard reset handler for newlibc-based environments
void __attribute__((section(".reset_handler"), used)) reset_handler(void)
{
    __asm volatile(".option push; .option norelax; la gp, __global_pointer$; .option pop"); // Load the global pointer
    __asm volatile("la sp, _estack");                                                       // Set stack pointer to the top of the stack

    // Copy .data from FLASH to RAM
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    // Zero out .bss
    dst = &_sbss;
    while (dst < &_ebss)
        *dst++ = 0;

    init_vector_table(); // Set mtvec to point to the vector table

    // Jump to main
    main();
    // Should never reach here
    for (;;)
        ;
}
#endif
