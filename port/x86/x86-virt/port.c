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
#include "stats.h"

#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#endif

// COM1 ISA IRQ 4, COM2 ISA IRQ 3
#define COM1_PORT 0x3F8U // Legacy x86 Serial Port 1 IO Address Base
#define COM2_PORT 0x2F8U // Legacy x86 Serial Port 2 IO Address Base

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

static int uart_rx_null(void *arg, uint8_t byte);

// -- Globals --

alignas(8) uint32_t initialized;
stats_irq_t stats_irq;
stats_tx_t  stats_tx;

// NimBLE HCI Uart Rx
int (*uart_rx)(void *arg, uint8_t byte) = uart_rx_null;
void *cb_arg = nullptr;

// -- End of globals --

static int uart_rx_null(void *arg, uint8_t byte)
{
    (void)arg;
    (void)byte;
    return 0;
}
static void init_ns16550_uart_port(uint16_t port, uint32_t baud_rate)
{
    // Disable all interrupts temporarily during configuration
    outb(port + UART_REG_IER, 0x00);
    // Assert DLAB flag (Bit 7 of LCR) to configure the clock Baud Rate Divisor
    outb(port + UART_REG_LCR, 0x80);

    if (baud_rate == 115200) {
        outb(port + 0, 0x01); // DLL = 1 (Low Byte)
        outb(port + 1, 0x00); // DLM = 0 (High Byte)
    } else {
        while (true) {
            HALT_CPU();
        }
    }

    // Clear DLAB and configure transmission frame: 8 Data Bits, No Parity, 1 Stop Bit
    outb(port + UART_REG_LCR, 0x03);
    // Configure FIFO: Enable, clear queues, set trigger threshold to 1 byte
    outb(port + UART_REG_FCR, 0x07);
    // Assert DTR (0x01) + RTS (0x02) + OUT2 (0x08) = 0x0B
    outb(port + UART_REG_MCR, 0x0B);
    // Enable the Receiver Data Available Interrupt (Bit 0)
    outb(port + UART_REG_IER, 0x01);
}

static void init_ns16550_uart(uint32_t baud_rate)
{
    init_ns16550_uart_port(COM1_PORT, baud_rate);
    init_ns16550_uart_port(COM2_PORT, baud_rate);
}

static inline int _uart_tx(uint16_t port, int c)
{
    while (!(inb(port + UART_REG_LSR) & UART_LSR_THRE)) {
        NOP();
    }
    outb(port + UART_REG_THR, (uint8_t)c);
    return c;
}

// also used for NimBLE HCI Uart Tx
int uart_tx(int n, int c)
{
    stats_tx.count.uart[n]++;
    uint16_t port[] = {COM1_PORT, COM2_PORT};
    return _uart_tx(port[n], c);
}

int putchar(int c)
{
    return uart_tx(0, c);
}

void uart_flush(void)
{
    while (!(inb(COM1_PORT + UART_REG_LSR) & UART_LSR_TEMT)) {
        NOP();
    }
}

void NS16550_com1_irq_handler(void)
{
    stats_irq.count.uart[0]++;
    // Loop until the FIFO and holding registers are empty
    while (inb(COM1_PORT + UART_REG_LSR) & UART_LSR_DR) {
        char c = (char)(inb(COM1_PORT + UART_REG_RBR) & 0xFF);

        fifo_push(c);

        if (!buffered_mode || (c == '\n' || c == '\r')) {
            signal_data_ready();
        }
    }
}

void NS16550_com2_irq_handler(void)
{
    stats_irq.count.uart[1]++;
    // Loop until the FIFO and holding registers are empty
    while (inb(COM2_PORT + UART_REG_LSR) & UART_LSR_DR) {
        uint8_t c = (inb(COM2_PORT + UART_REG_RBR) & 0xFF);
        uart_rx(cb_arg, c);
    }
}

void init_uart(void)
{
    if (initialized & UART_INITIALIZED)
        return;

    init_ns16550_uart(115200);

// Register and enable the NS16550 Interrupts (IRQ 3 & IRQ 4)
#ifdef ENABLE_RTOS
    xPortRegisterCInterruptHandler(NS16550_com1_irq_handler, X86_IRQ_VECTOR_BASE + UART1_IRQ_ID);
    xPortRegisterCInterruptHandler(NS16550_com2_irq_handler, X86_IRQ_VECTOR_BASE + UART2_IRQ_ID);
#else
    apic_register_interrupt(UART1_IRQ_ID, NS16550_com1_irq_handler);
    apic_register_interrupt(UART1_IRQ_ID, NS16550_com1_irq_handler);
#endif
    apic_enable_interrupt(UART1_IRQ_ID);
    apic_enable_interrupt(UART2_IRQ_ID);

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
