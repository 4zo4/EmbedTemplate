/**
 * @file startup.c
 * @brief Startup code and interrupt vector table for STM32F4.
 * This file defines the reset handler, default interrupt handlers, and the interrupt vector table for the STM32F4 microcontroller.
 * It sets up the initial stack pointer, copies the .data section from flash to RAM, zeroes the .bss section, and then jumps to the main function.
 * The interrupt handlers include the default handlers for NMI, Hard Fault, Memory Management Fault, Bus Fault, Usage Fault, and Debug Monitor, as well as the UART1 interrupt handler and the Watchdog interrupt handler.
 * The vector table is placed in the .isr_vector section and is aligned to 8 bytes as required by the Cortex-M4 architecture.
 */
#include <stdint.h>

// prototypes without include file
int main(void);
void reset_handler(void);
void UART1_irq_handler(void);
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

extern uint32_t _sdata, _edata, _sbss, _ebss, _sidata, _estack;

#define SCB_CPACR (*((volatile uint32_t *)0xE000ED88))

static inline void fpu_enable(void)
{
    /* Set bits 20-23 to 11 (Full Access) for CP10 and CP11 */
    SCB_CPACR |= (0xF << 20); 
    __asm volatile ("dsb; isb");
}

void reset_handler(void)
{
    fpu_enable();

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
    0, 0, 0, 0,               // 7-10: Reserved
#ifdef ENABLE_RTOS
    [11] = SVC_Handler,       // 11: SVC
    [12] = debug_mon_handler, // 12: Debug Monitor
    0,                        // 13: Reserved
    [14] = PendSV_Handler,    // 14: PendSV
    [15] = SysTick_Handler,   // 15: SysTick
#else
    [11] = 0,                 // SVC
    [12] = debug_mon_handler, // 12: Debug Monitor
    0,                        // 13: Reserved
    [14] = 0,                 // 14: PendSV
    [15] = SysTick_Handler,   // 15: SysTick
#endif
    [16] = WWDG_irq_handler,  // 16: Watchdog IRQ 0
    [53] = UART1_irq_handler, // 53: STM32F4 UART1 is IRQ 37 (No 53 with Cortex-M4 offset 16 for system IRQs)
};
// clang-format on
