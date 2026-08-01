/**
 * @file port.c
 * @brief Port-specific implementations for QEMU x86 Virt.
 * This file contains the implementations of the hardware-specific functions for the QEMU x86 Virt,
 * including UART initialization and interrupt handlers.
 *  The UART is set up for 115200 baud communication.
 */
#include <stdint.h>
#include <stdio.h>

#include "apic.h"
#include "common.h" // defines static inline outb/inb
#include "init.h"

#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#endif

#define COM1_PORT 0x3F8U // Legacy x86 Serial Port 1 IO Address Base

// Register Offsets for the physical x86 NS16550 Chip
#define UART_REG_RBR 0 // Receiver Buffer Register (Read)
#define UART_REG_THR 0 // Transmitter Holding Register (Write)
#define UART_REG_IER 1 // Interrupt Enable Register
#define UART_REG_IIR 2 // Interrupt Identification Register
#define UART_REG_FCR 2 // FIFO Control Register
#define UART_REG_LCR 3 // Line Control Register
#define UART_REG_MCR 4 // Modem Control Register
#define UART_REG_LSR 5 // Line Status Register
#define UART_REG_MSR 6 // Modem Status Register

#define UART_LSR_DR 0x01   // Data Ready flag bit
#define UART_LSR_THRE 0x20 // Transmitter Holding Register Empty flag bit
#define UART_LSR_TEMT 0x40 // Transmitter Empty (Line Idle) flag bit

#define X86_LOCAL_APIC_MSI_DOORBELL 0xFEE00000UL         // QEMU APIC MSI Doorbell Address for PCIe devices
#define PCI_MSI_BASE_ADDRESS X86_LOCAL_APIC_MSI_DOORBELL // Base address for PCIe MSI messages

alignas(8) uint32_t initialized;

static void init_ns16550_uart(uint32_t baud_rate)
{
    // Disable all interrupts temporarily during configuration
    outb(COM1_PORT + UART_REG_IER, 0x00);

    // Assert DLAB flag (Bit 7 of LCR) to configure the clock Baud Rate Divisor
    outb(COM1_PORT + UART_REG_LCR, 0x80);

    if (baud_rate == 115200) {
        outb(COM1_PORT + 0, 0x01); // DLL = 1 (Low Byte)
        outb(COM1_PORT + 1, 0x00); // DLM = 0 (High Byte)
    } else {
        while (true) {
            HALT_CPU();
        }
    }

    // Clear DLAB and configure transmission frame: 8 Data Bits, No Parity, 1 Stop Bit
    outb(COM1_PORT + UART_REG_LCR, 0x03);

    // Configure FIFO: Enable, clear queues, set trigger threshold to 1 byte
    outb(COM1_PORT + UART_REG_FCR, 0x07);

    // Assert DTR (0x01) + RTS (0x02) + OUT2 (0x08) = 0x0B
    outb(COM1_PORT + UART_REG_MCR, 0x0B);

    // Enable the Receiver Data Available Interrupt (Bit 0)
    outb(COM1_PORT + UART_REG_IER, 0x01);
}

int putchar(int c)
{
    while (!(inb(COM1_PORT + UART_REG_LSR) & UART_LSR_THRE)) {
        NOP();
    }
    outb(COM1_PORT + UART_REG_THR, (uint8_t)c);
    return c;
}

void uart_flush(void)
{
    while (!(inb(COM1_PORT + UART_REG_LSR) & UART_LSR_TEMT)) {
        NOP();
    }
}

void NS16550_irq_handler(void)
{
    // Loop until the FIFO and holding registers are empty
    while (inb(COM1_PORT + UART_REG_LSR) & UART_LSR_DR) {
        char c = (char)(inb(COM1_PORT + UART_REG_RBR) & 0xFF);

        fifo_push(c);

        if (!buffered_mode || (c == '\n' || c == '\r')) {
            signal_data_ready();
        }
    }
}

void init_uart(void)
{
    if (initialized & UART_INITIALIZED)
        return;

    init_ns16550_uart(115200);

// Register and enable the NS16550 Interrupt (IRQ 4)
#ifdef ENABLE_RTOS
    xPortRegisterCInterruptHandler(NS16550_irq_handler, X86_IRQ_VECTOR_BASE + UART_IRQ_ID);
    apic_enable_interrupt(UART_IRQ_ID);
#else
    apic_register_interrupt(UART_IRQ_ID, NS16550_irq_handler);
    apic_enable_interrupt(UART_IRQ_ID);
#endif

    initialized |= UART_INITIALIZED;

    log_set_level(DOMAIN_SYS, ENTITY_UART, LOG_LEVEL_INFO);
    LOG_UART_INFO("UART initialized with baud rate 115200");
}

void init_watchdog(void)
{
    NOP();
}

extern alignas(8) volatile uintptr_t pcie_msi_base_addr;
static void init_x86_virt_globals(void)
{
    if (initialized & GLOBALS_INITIALIZED)
        return;
#ifdef ENABLE_PCI
    pcie_msi_base_addr = PCI_MSI_BASE_ADDRESS;
#endif
    init_apic();
#ifndef ENABLE_RTOS
    enable_interrupts();
#endif
    initialized |= GLOBALS_INITIALIZED;
}

void (*volatile init_port_globals)(void) = init_x86_virt_globals;
