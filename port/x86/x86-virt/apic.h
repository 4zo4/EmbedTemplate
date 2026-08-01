#pragma once

#define MAX_IRQS 32
#define MAX_IRQ_ID (MSI_IRQ_OFFSET + 8)

#ifdef ENABLE_RTOS
#define X86_IRQ_VECTOR_BASE 0x30 // Priority Class 3
#else                            // !RTOS
#define X86_IRQ_VECTOR_BASE 0x30
#endif
#define MSI_IRQ_OFFSET 16
#define LEGACY_VECTOR_BASE 0x20
#define MSI_IRQ_VECTOR_BASE (X86_IRQ_VECTOR_BASE + MSI_IRQ_OFFSET) // Priority Class 4

#define PIT_IRQ_ID 0U          // PIT is physically wired to IO-APIC GSI Pin 0
#define UART_IRQ_ID 4U         // COM1 routes to IO-APIC GSI Pin 4
#define LAPIC_TIMER_IRQ_ID 15U // LAPIC LVT Timer

typedef void (*irq_handler_t)(void);

void init_apic(void);

int apic_register_interrupt(uint16_t irq_id, irq_handler_t handler);
int apic_enable_interrupt(uint16_t irq_id);
int apic_disable_interrupt(uint16_t irq_id);
