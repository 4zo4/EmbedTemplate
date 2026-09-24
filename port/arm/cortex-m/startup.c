/**
 * @file startup.c
 * @brief Startup code and interrupt vector table for Cortex-M.
 * This file defines the reset handler, default interrupt handlers, and the interrupt vector table for the Cortex-M microcontroller.
 * It sets up the initial stack pointer, copies the .data section from flash to RAM, zeroes the .bss section, and then jumps to the main function.
 * The interrupt handlers include the default handlers for NMI, Hard Fault, Memory Management Fault, Bus Fault, Usage Fault, and Debug Monitor, as well as the UART1 interrupt handler and the Watchdog interrupt handler.
 * The vector table is placed in the .isr_vector section and is aligned to 8 bytes as required by the Cortex-M architecture.
 */
#include <stdint.h>

// prototypes without include file
int  main(void);
void reset_handler(void);
void UART_irq_handler(void);
void nmi_handler(void);
void hard_fault_handler(void);
void mem_manage_handler(void);
void bus_fault_handler(void);
void usage_fault_handler(void);
void debug_mon_handler(void);
void WWDG_irq_handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
#define WWDG_VEC 16
#define UART_VEC 48 // Rx IRQ
void secure_fault_handler(void);
#else
#define WWDG_VEC 16
#define UART_VEC 53 // Rx IRQ
#define secure_fault_handler 0
#endif
extern uint32_t _sdata, _edata, _sbss, _ebss, _sidata, _estack;

// System Control Block (SCB) register definitions
#define SCB_VTOR (*((volatile uint32_t *)0xE000ED08))  // Vector Table Offset Register
#define SCB_CPACR (*((volatile uint32_t *)0xE000ED88)) // Coprocessor Access Control Register

void (*const vector_table[])(void);

static inline void fpu_enable(void)
{
    /* Set bits 20-23 to 11 (Full Access) for CP10 and CP11 */
    SCB_CPACR |= (0xF << 20);
    __asm volatile("dsb; isb");
}

static inline void vector_table_enable(void)
{
    SCB_VTOR = (uint32_t)vector_table;
    __asm volatile("dsb; isb");
}

void reset_handler(void)
{
#ifndef ENABLE_SECURE
    vector_table_enable();
    fpu_enable();
#endif
    // Copy .data from FLASH to RAM
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    // Zero out .bss
    dst = &_sbss;
    while (dst < &_ebss)
        *dst++ = 0;

    // Jump to main
    main();
    // Should never reach here
    for (;;)
        ;
}

// clang-format off
__attribute__((section(".isr_vector"), aligned(8)))
void (*const vector_table[])(void) = {
    (void (*)(void))&_estack, // 0: Initial Stack Pointer
    reset_handler,            // 1: Reset
    nmi_handler,              // 2: NMI
    hard_fault_handler,       // 3: Hard Fault
    mem_manage_handler,       // 4: MPU
    bus_fault_handler,        // 5: Bus Fault
    usage_fault_handler,      // 6: Usage Fault
    secure_fault_handler,     // 7: Secure Fault for Armv8-M or Reserved for Armv7-M
    0, 0, 0,                  // 8-10: Reserved
#ifdef ENABLE_RTOS
    [11] = SVC_Handler,       // 11: SVC
    [12] = debug_mon_handler, // 12: Debug Monitor
    [13] = 0,                 // 13: Reserved
    [14] = PendSV_Handler,    // 14: PendSV
    [15] = SysTick_Handler,   // 15: SysTick
#else // event-loop
    [11] = 0,                 // SVC
    [12] = debug_mon_handler, // 12: Debug Monitor
    [13] = 0,                 // 13: Reserved
    [14] = 0,                 // 14: PendSV
    [15] = SysTick_Handler,   // 15: SysTick
#endif
    [WWDG_VEC] = WWDG_irq_handler, // 16: Watchdog IRQ 0
    [UART_VEC] = UART_irq_handler, // 37: Cortex-M4 UART1 Rx is IRQ 37 (Vector 53 for Cortex-M4)
                                   // 32: Cortex-M33 UART0 RX is IRQ 32 (Vector 48 for Cortex-M33 MPS2-AN505)
};
// clang-format on
