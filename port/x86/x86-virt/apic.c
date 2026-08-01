/**
 * @file apic.c
 * @brief APIC (Advanced Programmable Interrupt Controller) specific implementations for QEMU x86 Virt.
 * This file contains the implementations of the APIC-specific functions for the QEMU x86 Virt,
 * including interrupt dispatch.
 */
#include <stdint.h>
#include <stdio.h>

#include "apic.h"
#include "common.h"

// An Interrupt Descriptor Table (IDT) Entry for x86-32
typedef struct idt_entry_s {
    uint16_t offset_low;  // Low 16 bits of handler address
    uint16_t selector;    // Segment Selector
    uint8_t  zero;        // Must be set to zero
    uint8_t  flags;       // Type|DPL|P bits
    uint16_t offset_high; // High 16 bits of handler address
} __attribute__((packed)) idt_entry_t;

typedef struct idt_ptr_s {
    uint16_t limit;
    uint32_t base; // IDT base address
} __attribute__((packed)) idt_ptr_t;

#define NULL_INDEX 0xFF // sentinel value to indicate an empty slot in the table
#define LAPIC_BASE_ADDRESS 0xFEE00000UL
#define IOAPIC_BASE_ADDRESS 0xFEC00000UL

// -- IO APIC Registers --
#define IOAPIC_INDEX (*(volatile uint32_t *)(IOAPIC_BASE_ADDRESS + IOAPICID))
#define IOAPIC_DATA (*(volatile uint32_t *)(IOAPIC_BASE_ADDRESS + IOAPICAD))
#define IOAPIC_EOI (*(volatile uint32_t *)(IOAPIC_BASE_ADDRESS + 0x40))

#define IOAPICID 0x00
#define IOAPICAD 0x10
#define IOAPICIRQTBL(n) (0x10 + 2 * n)

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
#define LAPIC_LDR (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x00D0))       // Logical Destination Register
#define LAPIC_DFR (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x00E0))       // Destination Format Register
#define LAPIC_EOI (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x00B0))       // End of Interrupt Register
#define LAPIC_TPR (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x0080))       // Task Priority Register
#define LAPIC_SIVR (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x00F0))      // Spurious Interrupt Vector Register
#define LAPIC_LVT0 (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x0350))      // Local Vector Table 0 (LINT0) Register
#define LAPIC_LVT1 (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x0360))      // Local Vector Table 1 (LINT1) Register
#define LAPIC_LVT_TIMER (*(volatile uint32_t *)(LAPIC_BASE_ADDRESS + 0x0320)) //

// -- 8259 PIC Controller I/O Ports --
#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

void apic_dispatch_interrupt(uint32_t vector_id); // forward declaration for IDT stub

#define DEF_IDT_STUB(index) \
    __attribute__((naked)) static void idt_stub_##index(void) \
    { \
        __asm__ volatile( \
            "pushal\n\t" \
            "pushl %0\n\t" \
            "call apic_dispatch_interrupt\n\t" \
            "addl $4, %%esp\n\t" \
            "popal\n\t" \
            "iretl\n\t" \
            : \
            : "i"((uint32_t)vector_id[index]) \
            : "memory" \
        ); \
    }

#define IDT_SET_GATE(index) \
    idt_set_gate(vector_id[index], idt_stub_##index)

// vector list must be sorted in ascending order
enum {
    VECTOR_PIT = X86_IRQ_VECTOR_BASE,                        // IRQ 0: PIT
    VECTOR_UART = X86_IRQ_VECTOR_BASE + UART_IRQ_ID,         // IRQ 4: UART NS16550 Serial console COM1
    VECTOR_TIMER = X86_IRQ_VECTOR_BASE + LAPIC_TIMER_IRQ_ID, // IRQ 15: LAPIC Timer
    VECTOR_MSI0 = MSI_IRQ_VECTOR_BASE,                       // IRQ 16: MSI Vector 0
    VECTOR_MSI1 = MSI_IRQ_VECTOR_BASE + 1,                   // IRQ 17: MSI Vector 1
    MAX_IRQ_VECTOR = VECTOR_MSI1                             // MAX_IRQ_VECTOR must be the last vector id in the list
};

// -- Globals --

static const uint8_t vector_id[] = {
    VECTOR_PIT, VECTOR_UART, VECTOR_TIMER, VECTOR_MSI0, VECTOR_MSI1
};

static irq_handler_t irq_handler_table[MAX_IRQS];
static uint8_t       irq_lookup_table[MAX_IRQ_ID + 1];
static int           irq_count;

alignas(32) static idt_entry_t idt[MAX_IRQ_VECTOR + 2];
alignas(16) static idt_ptr_t idt_ptr;

// -- End of globals --

DEF_IDT_STUB(0); // PIT
DEF_IDT_STUB(1); // UART
DEF_IDT_STUB(2); // LAPIC Timer
DEF_IDT_STUB(3); // MSI 0
DEF_IDT_STUB(4); // MSI 1

static void idt_set_gate(uint8_t vector, irq_handler_t handler)
{
    if (vector > MAX_IRQ_VECTOR)
        return;

    uintptr_t offset = (uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)(offset & 0xFFFFU);
    idt[vector].offset_high = (uint16_t)((offset >> 16) & 0xFFFFU);
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].flags = 0x8E;
}

static void init_idt(void)
{
    IDT_SET_GATE(0);
    IDT_SET_GATE(1);
    IDT_SET_GATE(2);
    IDT_SET_GATE(3);
    IDT_SET_GATE(4);
}

#define ENABLE_LEVEL_TRIGGERED
void init_apic(void)
{
    irq_count = 0;

    for (int i = 0; i < (MAX_IRQ_ID + 1); i++) {
        irq_lookup_table[i] = NULL_INDEX;
    }
    for (int i = 0; i < MAX_IRQS; i++) {
        irq_handler_table[i] = nullptr;
    }

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

    // write canary guard
    uint32_t *u32 = (uint32_t *)&idt[MAX_IRQ_VECTOR + 1];
    u32[0] = 0xDEADBEEFUL;
    u32[1] = 0xACDCBABEUL;

    init_idt();

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));

    LAPIC_DFR = 0xFFFFFFFF;   // Set Flat Model
    LAPIC_LDR = 0x01000000;   // Assign CPU 0 to Logical ID 1
    LAPIC_TPR = 0x00000000;   // Allow all IRQ groups
    LAPIC_SIVR = 0x1FF;       // Enable local APIC spurious register interface
    LAPIC_LVT0 = 0x00010000U; // Masked Fixed Vector
    LAPIC_LVT1 = 0x00010000U; // Masked Fixed Vector

    outb(0x22, 0x70); // Select the IMCR Register
    outb(0x23, 0x01); // Force NMI and INTR signals to flow through the APIC

    const uint8_t ioapic_uart_irq = IOAPICIRQTBL(UART_IRQ_ID);
    ioapic_write(ioapic_uart_irq + 1, 0x00000000); // High 32-bits (Destination: APIC ID 0)
#ifdef LEVEL_TRIGGERED
    // UART IRQ config Level-Triggered (Bit 15 = 1), Active-Low (Bit 13 = 1)
    ioapic_write(ioapic_uart_irq, // Low 32-bits (Triggers line unmasking atomically)
                 (X86_IRQ_VECTOR_BASE + UART_IRQ_ID) | BIT(IRQ_TRIGGER_MODE) | BIT(IRQ_PIN_POLARITY));
#else
    // UART IRQ config Edge-Triggered (Bit 15 = 0), Active-High (Bit 13 = 0)
    ioapic_write(ioapic_uart_irq, // Low 32-bits (Triggers line unmasking atomically)
                 (X86_IRQ_VECTOR_BASE + UART_IRQ_ID));
#endif
}

int apic_register_interrupt(uint16_t irq_id, irq_handler_t handler)
{
    if (irq_id > MAX_IRQ_ID || !handler || irq_count >= MAX_IRQS)
        return -1;
    int idx = irq_count++;
    irq_handler_table[idx] = handler;
    irq_lookup_table[irq_id] = idx;
    return 0;
}

int apic_enable_interrupt(uint16_t irq_id)
{
    if (irq_id > MAX_IRQ_ID)
        return -1;

    if (irq_id == LAPIC_TIMER_IRQ_ID) {
        LAPIC_LVT_TIMER &= ~BIT(16);
        return 0;
    }

    if (irq_id < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & ~BIT(irq_id));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) & ~BIT(irq_id - 8));
    }

    return 0;
}

int apic_disable_interrupt(uint16_t irq_id)
{
    if (irq_id > MAX_IRQ_ID)
        return -1;

    if (irq_id == LAPIC_TIMER_IRQ_ID) {
        LAPIC_LVT_TIMER |= BIT(16);
        return 0;
    }

    if (irq_id < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) | BIT(irq_id));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) | BIT(irq_id - 8));
    }
    return 0;
}

void apic_dispatch_interrupt(uint32_t vector_id)
{
    if (vector_id >= X86_IRQ_VECTOR_BASE) {
        uint32_t irq_id = vector_id - X86_IRQ_VECTOR_BASE;
        if (irq_id <= MAX_IRQ_ID) {
            int idx = irq_lookup_table[irq_id];
#ifdef ENABLE_SEFETY_CHECK
            if (idx < irq_count && irq_handler_table[idx] != nullptr)
                irq_handler_table[idx]();
#else
            irq_handler_table[idx]();
#endif
        }
    }

    // Line originated from legacy PIC block
    if (vector_id >= X86_IRQ_VECTOR_BASE &&
        vector_id < (MSI_IRQ_VECTOR_BASE)) {
        if (vector_id >= (X86_IRQ_VECTOR_BASE + 8)) {
            outb(PIC2_COMMAND, PIC_EOI);
        }
        outb(PIC1_COMMAND, PIC_EOI);
    }
    LAPIC_EOI = 0; // EOI to the Local APIC
}
