/**
 * @file gic.c
 * @brief GIC (Generic Interrupt Controller)-specific implementations for Cortex-A system.
 * This file contains the implementations of the GIC-specific functions for the Cortex-A,
 * including system interrupt dispatch.
 */
#include <stdint.h>
#include <stdio.h>

#include "gic.h"

#define GICD_BASE_ADDRESS gicd_base_addr                                                  // GIC Distributor Base Address
#define GICC_BASE_ADDRESS gicc_base_addr                                                  // GIC CPU Interface Base Address
#define GICD_ISENABLER(n) ((volatile uint32_t *)(GICD_BASE_ADDRESS + 0x0100 + ((n) * 4))) // Interrupt Set-Enable Registers
#define GICD_ICENABLER(n) ((volatile uint32_t *)(GICD_BASE_ADDRESS + 0x0180 + ((n) * 4))) // Interrupt Clear-Enable Registers
#define GICD_IPRIORITYR(n) (*(volatile uint8_t *)(GICD_BASE_ADDRESS + 0x0400 + (n)))      // Interrupt Priority Registers
#define GICD_ITARGETSR(n) (*(volatile uint8_t *)(GICD_BASE_ADDRESS + 0x0800 + (n)))       // Interrupt Target Registers
#define GICC_EOIR (*(volatile uint32_t *)(GICC_BASE_ADDRESS + 0x0010))                    // End of Interrupt Register
#define GICD_CTLR (*(volatile uint32_t *)(GICD_BASE_ADDRESS + 0x0000))                    // Distributor Control Register
#define GICC_CTLR (*(volatile uint32_t *)(GICC_BASE_ADDRESS + 0x0000))                    // CPU Interface Control Register
#define GICC_PMR (*(volatile uint32_t *)(GICC_BASE_ADDRESS + 0x0004))                     // Interrupt Priority Mask Register
#define GICC_DIR (*(volatile uint32_t *)(GICC_BASE_ADDRESS + 0x1000))                     // Deactivate Interrupt Register

#define GIC_NULL_INDEX 0xFF
#define GIC_BITMAP_DWORDS (MAX_IRQ_ID / 32)
#define HASH_BUCKETS 16
#define HASH_MASK (HASH_BUCKETS - 1)

// -- Globals --

// user port have to set platform-specific GIC base address
alignas(8) volatile uintptr_t gicd_base_addr;
alignas(8) volatile uintptr_t gicc_base_addr;
alignas(8) uint32_t volatile gic_iar;

// used by startup.S code, they must stay global
uint8_t       irq_lookup_table[MAX_IRQ_ID + 1];
irq_handler_t irq_handler_table[MAX_IRQS];
int           irq_count;

// -- End of globals --

void init_gic(void)
{
    irq_count = 0;

    for (int i = 0; i < (MAX_IRQ_ID + 1); i++) {
        irq_lookup_table[i] = GIC_NULL_INDEX;
    }
    for (int i = 0; i < MAX_IRQS; i++) {
        irq_handler_table[i] = nullptr;
    }
    GICC_PMR = 0xF0U;  // Unmask global priority filter thresholds to accept priorities 0x20 - 0xA0
    GICD_CTLR = 0x03U; // Enable Group 1 (Non-secure) interrupt forwarding at the distributor
    GICC_CTLR = 0x03U; // Enable CPU interface processing line to pass signaling to the core
}

int gic_register_interrupt(uint16_t irq_id, irq_handler_t handler)
{
    if (irq_id > MAX_IRQ_ID || !handler || irq_count >= MAX_IRQS)
        return -1;
    int idx = irq_count++;
    irq_handler_table[idx] = handler;
    irq_lookup_table[irq_id] = idx;
    return 0;
}

static int gic_dispatch_interrupt(uint32_t irq_id)
{
    int idx = irq_lookup_table[irq_id];
#ifdef ENABLE_SEFETY_CHECK
    if (idx < irq_count && irq_handler_table[idx] != nullptr)
        irq_handler_table[idx]();
#else
    irq_handler_table[idx]();
#endif
    return 0;
}

int gic_enable_interrupt(uint16_t irq_id, uint8_t priority)
{
    if (irq_id > MAX_IRQ_ID)
        return -1;
    GICD_IPRIORITYR(irq_id) = priority;                    // Set priority (0x00 is highest, 0xFF is lowest)
    GICD_ITARGETSR(irq_id) = 0x01;                         // Route the line to CPU Core 0 (Bit 0)
    *GICD_ISENABLER(irq_id >> 5) = (1U << (irq_id & 31U)); // Enable the line (32 lines per register row)
    return 0;
}

int gic_disable_interrupt(uint16_t irq_id)
{
    if (irq_id > MAX_IRQ_ID)
        return -1;
    *GICD_ICENABLER(irq_id >> 5) = (1U << (irq_id & 31U)); // Disable the line
    return 0;
}

// common interrupt handler for FreeRTOS and bare-metal
void vApplicationIRQHandler(uint32_t ulICCIAR)
{
    // Mask out CPU ID token bits
    // Extract the low 10 bits for the Interrupt ID
    uint32_t ulInterruptID = ulICCIAR & 0x3FFUL;
    gic_dispatch_interrupt(ulInterruptID);
    GICC_EOIR = ulInterruptID; // Clear the GIC active state priority ceiling
    GICC_DIR = ulICCIAR;       // Deactivate the interrupt
}
