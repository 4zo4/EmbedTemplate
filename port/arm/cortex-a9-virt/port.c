/**
 * @file port.c
 * @brief Port-specific implementations for QEMU Cortex-A15 Virt.
 * This file contains the implementations of the hardware-specific functions for the QEMU Cortex-A15 Virt,
 * including UART initialization and interrupt handlers. It defines the GIC registers base addresses and
 * PrimeCell base address for configuring the UART and GIC. The UART is set up for 115200 baud communication.
 */
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "gic.h"
#include "init.h"

// QEMU Virt PrimeCell UART (PL011) Base Address and Register Definitions
#define PL011_BASE_ADDRESS 0x09000000
#define PL011_DR (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x00))   // Data Register
#define PL011_FR (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x18))   // Flag Register
#define PL011_CR (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x30))   // Control Register
#define PL011_IBRD (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x24)) // Integer Baud Rate Register
#define PL011_FBRD (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x28)) // Fractional Baud Rate Register
#define PL011_MIS (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x40))  // Masked Interrupt Status
#define PL011_ICR (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x44))  // Interrupt Clear Register
#define PL011_IMSC (*(volatile uint32_t *)(PL011_BASE_ADDRESS + 0x38)) // Interrupt Mask Set/Clear Register
#define PL011_BUSY BIT(3)                                              // Busy flag
#define PL011_TXFF BIT(5)                                              // Transmit FIFO Full flag
#define PL011_RXMIS BIT(4)                                             // Receive Masked Interrupt Status

#define GICD_BASE_ADDRESS 0x08000000UL // GIC Distributor Base Address for QEMU Cortex-A15 Virt
#define GICC_BASE_ADDRESS 0x08010000UL // GIC CPU Interface Base Address for QEMU Cortex-A15 Virt

#define QEMU_GICV2M_DOORBELL_ADDR 0x08020040UL         // QEMU GICv2m MSI Doorbell Address for PCIe devices
#define PCI_MSI_BASE_ADDRESS QEMU_GICV2M_DOORBELL_ADDR // Base address for PCIe MSI messages

#define UART_IRQ_ID 33U

alignas(8) uint32_t initialized;

int putchar(int c)
{
    // Wait until the transmit FIFO is not full
    while (PL011_FR & PL011_TXFF) {
        NOP();
    }
    PL011_DR = c;
    return c;
}

void uart_flush(void)
{
    while (PL011_FR & PL011_BUSY) {
        NOP();
    }
}

void PL011_irq_handler(void)
{
    uint32_t active_interrupts = PL011_MIS;
    PL011_ICR = active_interrupts;

    while (!(PL011_FR & PL011_RXMIS)) {
        char c = (char)(PL011_DR & 0xFF);

        if (echo_enabled)
            putchar(c);

        fifo_push(c);

        if (!buffered_mode || (c == '\n' || c == '\r'))
            signal_data_ready();
    }

    data_sync_barrier();
    ins_sync_barrier();
}

void fault_handler(void)
{
    uint32_t dfsr = 0, dfar = 0, ifsr = 0, ifar = 0;
    uint32_t sp, lr, cpsr;

    __asm__ volatile("mov %0, sp" : "=r"(sp));
    __asm__ volatile("mov %0, lr" : "=r"(lr));
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));

    printf("\r\n[CRITICAL] Fault Detected!\r\n");

    printf("SP: 0x%08lX LR: 0x%08lX CPSR: 0x%08lX\r\n", sp, lr, cpsr);
    printf("DFSR: 0x%08lX (Data Fault Status)\r\n", dfsr);
    printf("DFAR: 0x%08lX (Data Fault Address)\r\n", dfar);
    printf("IFSR: 0x%08lX (Instruction Fault Status)\r\n", ifsr);
    printf("IFAR: 0x%08lX (Instruction Fault Address)\r\n", ifar);

    printf("[HALT] System locked. Waiting for power-on reset (POR)...\r\n");

    while (true) {
        HALT_CPU();
    }
}

void init_uart(void)
{
    if (initialized & UART_INITIALIZED)
        return;

    // Set baud rate to 115200
    PL011_IBRD = 27; // Integer part of the baud rate divisor
    PL011_FBRD = 8;  // Fractional part of the baud rate

    PL011_IMSC = BIT(4);                 // Enable RX interrupt
    PL011_CR = BIT(0) | BIT(8) | BIT(9); // Enable UART, TX, RX

    gic_register_interrupt(UART_IRQ_ID, PL011_irq_handler);
    gic_enable_interrupt(UART_IRQ_ID, 0xA0); // Enable the PL011 Interrupt (IRQ 33)

    initialized |= UART_INITIALIZED;

    log_set_level(DOMAIN_SYS, ENTITY_UART, LOG_LEVEL_INFO);
    LOG_UART_INFO("UART initialized with baud rate 115200");
}

void init_watchdog(void)
{
    NOP();
}

extern alignas(8) volatile uintptr_t pcie_msi_base_addr;
static void init_cortex_a9_virt_globals(void)
{
    if (initialized & GLOBALS_INITIALIZED)
        return;
    cpu_hz = 24000000UL; // QEMU '-M virt' base timer frequency is 24MHz
    gicd_base_addr = GICD_BASE_ADDRESS;
    gicc_base_addr = GICC_BASE_ADDRESS;
#ifdef ENABLE_PCI
    pcie_msi_base_addr = PCI_MSI_BASE_ADDRESS;
#endif
    init_gic();
#ifndef ENABLE_RTOS
    enable_interrupts();
#endif
    initialized |= GLOBALS_INITIALIZED;
}

void (*volatile init_port_globals)(void) = init_cortex_a9_virt_globals;
