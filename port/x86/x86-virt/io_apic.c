/**
 * @file io_apic.c
 * @brief IO APIC specific implementations for QEMU x86 Virt.
 * This file contains the implementations of the IO APIC-specific functions for the QEMU x86 Virt,
 * including IO APIC EOI. This implementation complements FreeRTOS GCC/IA32_flat port interrupt framework
 * by providing
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "apic.h"
#include "common.h"

#define VECTOR_BASE X86_IRQ_VECTOR_BASE
#define LAPIC_BASE_ADDRESS 0xFEE00000UL
#define IOAPIC_BASE_ADDRESS 0xFEC00000UL

// -- IO APIC Registers --
#define IOAPIC_INDEX (*(volatile uint32_t *)(IOAPIC_BASE_ADDRESS + IOAPICID))
#define IOAPIC_DATA (*(volatile uint32_t *)(IOAPIC_BASE_ADDRESS + IOAPICDA))
#define IOAPIC_EOI (*(volatile uint32_t *)(IOAPIC_BASE_ADDRESS + 0x40))

#define IOAPICID 0x00                  // IO APIC Index Offset
#define IOAPICDA 0x10                  // IO APIC Data Access Offset
#define IOAPICIRQTBL(n) (0x10 + 2 * n) // IO APIC Irq Table Entry Offset

#define IRQ_PIN_POLARITY 13 // 0: Active high, 1: Active low
#define IRQ_TRIGGER_MODE 15 // 0: Edge, 1: Level
#define IRQ_MASK 16         // 0: Enable, 1: Disable Irq

[[maybe_unused]] static void ioapic_write(uint8_t reg, uint32_t value)
{
    IOAPIC_INDEX = reg;
    IOAPIC_DATA = value;
}

[[maybe_unused]] static uint32_t ioapic_read(uint8_t reg)
{
    IOAPIC_INDEX = reg;
    return IOAPIC_DATA;
}

// -- Local APIC Registers --
#define LAPIC_EOI (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x00B0))  // End of Interrupt Register
#define LAPIC_SIVR (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x00F0)) // Spurious Interrupt Vector Register
#define LAPIC_LVT0 (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x0350)) // Local Vector Table 0 (LINT0) Register
#define LAPIC_LVT1 (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x0360)) // Local Vector Table 1 (LINT1) Register

// -- 8259 PIC Controller I/O Ports --
#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

#define ENABLE_LEVEL_TRIGGERED
void init_apic(void)
{
    // Mask out legacy PIC chips
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, LEGACY_VECTOR_BASE);
    outb(PIC2_DATA, LEGACY_VECTOR_BASE + 8);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
#ifdef ENABLE_LEVEL_TRIGGERED
    outb(0x4D0, BIT(4) | BIT(0));
    outb(0x4D1, 0x00); // Config Edge/Level Control Registers (ELCR) in Level-Triggered mode
#else
    outb(0x4D0, 0x00);
    outb(0x4D1, 0x00); // Config Edge/Level Control Registers (ELCR) in Edge-Triggered mode
#endif
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF); // Disable/mask all inputs on the legacy master and slave PICs

    LAPIC_SIVR = 0x1FF;       // Enable local APIC spurious register interface
    LAPIC_LVT0 = 0x00010000U; // Masked Fixed Vector
    LAPIC_LVT1 = 0x00010000U; // Masked Fixed Vector

    outb(0x22, 0x70); // Select the IMCR Register
    outb(0x23, 0x01); // Force NMI and INTR signals to flow through the APIC

    const uint8_t ioapic_uart_irq = IOAPICIRQTBL(UART_IRQ_ID);
    ioapic_write(ioapic_uart_irq + 1, 0x00000000); // High 32-bits (Destination: APIC ID 0)
#ifdef ENABLE_LEVEL_TRIGGERED
    // UART IRQ config Level-Triggered (Bit 15 = 1), Active-Low (Bit 13 = 1)
    ioapic_write(ioapic_uart_irq, // Low 32-bits (Triggers line unmasking atomically)
                 (VECTOR_BASE + UART_IRQ_ID) | BIT(IRQ_TRIGGER_MODE) | BIT(IRQ_PIN_POLARITY));
#else
    // UART IRQ config Edge-Triggered (Bit 15 = 0), Active-High (Bit 13 = 0)
    ioapic_write(ioapic_uart_irq, // Low 32-bits (Triggers line unmasking atomically)
                 (VECTOR_BASE + UART_IRQ_ID));
#endif
}

int apic_enable_interrupt(uint16_t irq_id)
{
    if (irq_id > X86_IRQ_VECTOR_BASE + MAX_IRQ_ID)
        return -1;

    if (irq_id >= X86_IRQ_VECTOR_BASE + 16)
        return 0;

    if (irq_id < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & ~BIT(irq_id));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) & ~BIT((irq_id - 8)));
    }

    return 0;
}

int apic_disable_interrupt(uint16_t irq_id)
{
    if (irq_id > X86_IRQ_VECTOR_BASE + MAX_IRQ_ID)
        return -1;

    if (irq_id >= X86_IRQ_VECTOR_BASE + 16)
        return 0;

    if (irq_id < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) | BIT(irq_id));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) | BIT((irq_id - 8)));
    }
    return 0;
}

void ioapic_eoi(uint32_t vector_id)
{
    // Line originated from legacy PIC block
    if ((vector_id >= LEGACY_VECTOR_BASE &&
         vector_id < (LEGACY_VECTOR_BASE + 16))) {
        if (vector_id >= (LEGACY_VECTOR_BASE + 8)) {
            outb(PIC2_COMMAND, PIC_EOI);
        }
        outb(PIC1_COMMAND, PIC_EOI);
    }
#if (X86_IRQ_VECTOR_BASE != LEGACY_VECTOR_BASE)
    if ((vector_id >= X86_IRQ_VECTOR_BASE &&
         vector_id < (MSI_IRQ_VECTOR_BASE))) {
        if (vector_id >= (X86_IRQ_VECTOR_BASE + 8)) {
            outb(PIC2_COMMAND, PIC_EOI);
        }
        outb(PIC1_COMMAND, PIC_EOI);
    }
#endif
}
