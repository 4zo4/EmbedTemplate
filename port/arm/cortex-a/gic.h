#pragma once

#define MAX_IRQS 32
#define MAX_IRQ_ID 255 // 0 - 255 range
typedef void (*irq_handler_t)(void);

void init_gic(void);

int gic_register_interrupt(uint16_t irq_id, irq_handler_t handler);
int gic_enable_interrupt(uint16_t irq_id, uint8_t priority);
int gic_disable_interrupt(uint16_t irq_id);

extern volatile uintptr_t gicd_base_addr;
extern volatile uintptr_t gicc_base_addr;
