/**
 * @file pci.c
 * @brief PCI (Peripheral Component Interconnect)-specific implementations for QEMU x86 Virt.
 * This file contains the implementations of the PCI-specific functions for the QEMU x86 Virt,
 * including PCI enumeration and PCI interrupt configuration.
 */
#include <stdint.h>
#include <stdio.h>

#include "apic.h"
#include "common.h"
#include "event.h"
#include "init.h"
#include "log.h"
#include "log_marker.h"
#include "pci.h"
#include "term_codes.h"
#include "umacro.h"

#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#define PCI_VENDOR_ID_VFIO 0x494f
#define PCI_DEVICE_ID_VFIO 0x0dc8

#undef MAX_PCI_INT
#define MAX_PCI_INT 2                 // Maximum number of PCI interrupt vectors supported for the system
#define MAX_INT_SEQUENCE MAX_SEQUENCE // Maximum PCI interrupt sequence for trampoline generation

#if MAX_PCI_INT > MAX_INT_SEQUENCE
#error "MAX_PCI_INT cannot exceed MAX_INT_SEQUENCE"
#endif

#ifndef ENABLE_PCI
#undef MAX_PCI_INT
#define MAX_PCI_INT 0
#endif

void pcie_int_vector_handler(uint32_t index); // Forward declaration of the P vector handler

#if (MAX_PCI_INT > 0)

typedef struct pcie_int_stats_s {
    uint32_t count[MAX_PCI_INT];
} pcie_int_stats_t;

#define ENABLE_PCI_INT // Enable PCI interrupt handling
/*
   Boilerplate X-Macros to generate trampolines for each PCI interrupt vector
   Example for MAX_PCI_INT = 2:
   static void pcie_int_trampoline_0(void) { pcie_int_vector_handler(0); }
   static void pcie_int_trampoline_1(void) { pcie_int_vector_handler(1); }
   static const irq_handler_t pci_trampoline_table[] = {
       pcie_int_trampoline_0, pcie_int_trampoline_1,
   };
*/
#define CONCAT(a, b) a##_##b
#define ADD_NAME(prefix, index) CONCAT(prefix, index)
// Rollup the specified number of trampoline functions
#define CREATE_TRAMPOLINE_FUNC(prefix, index) \
    static void ADD_NAME(prefix, index)(void) \
    { \
        pcie_int_vector_handler(index); \
    }
CREATE_LIST(CREATE_TRAMPOLINE_FUNC, pcie_int_trampoline, MAX_PCI_INT)
#undef CREATE_TRAMPOLINE_FUNC
// Rollup the specified number of trampoline functions into a trampoline table
#define ADD_TRAMPOLINES(prefix, index) ADD_NAME(prefix, index),
static const irq_handler_t pci_trampoline_table[] = {
    CREATE_LIST(ADD_TRAMPOLINES, pcie_int_trampoline, MAX_PCI_INT)
};
#undef ADD_TRAMPOLINES
#endif // MAX_PCI_INT > 0

#ifdef ENABLE_PCI_INT
static pcie_int_stats_t pcie_int_stats;

uint32_t pcie_get_int_count(uint32_t vector)
{
    return pcie_int_stats.count[vector];
}

#ifdef ENABLE_RTOS
extern TaskHandle_t xPciHandle;

void pcie_int_vector_handler(uint32_t index)
{
    LOG_PCI_DEBUG("PCI interrupt %u received", PCI_IRQ_VECTOR_BASE + index);

    pcie_int_stats.count[index]++;

    if (xPciHandle != nullptr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        io_barrier();
        xTaskNotifyFromISR(xPciHandle, BIT(index), eSetBits, &xHigherPriorityTaskWoken);
        io_barrier();
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
#else // !ENABLE_RTOS
void pcie_int_vector_handler(uint32_t index)
{
    LOG_PCI_DEBUG("PCI interrupt %u received, event 0x%x set", PCI_IRQ_VECTOR_BASE + index, EVT_PCI_INT_IDX(index));

    pcie_int_stats.count[index]++;

    event_notify |= EVT_PCI_INT_IDX(index);
}
#endif
#endif // ENABLE_PCI_INT

static int pcie_register_int_vector(pci_device_info_t *dev_info, uint32_t *vector_idx)
{
#ifdef ENABLE_PCI_INT
    static uint32_t trampoline_idx = 0;
    uint32_t        vector_id;

    if (dev_info->vendor_id != PCI_VENDOR_ID_VFIO ||
        dev_info->device_id != PCI_DEVICE_ID_VFIO)
        return -1;

    if (dev_info->vector_count >= MAX_PCI_INT)
        return -1; // No more vectors available

    if (dev_info->int_mode == PCI_INT_MODE_INTX) {
        vector_id = X86_IRQ_VECTOR_BASE + 11; // INTx uses a single vector
        dev_info->vector_count = MAX_PCI_INT;
    } else { // MSI and MSI-X use multiple vectors
        vector_id = PCI_IRQ_VECTOR_BASE + dev_info->vector_count;
    }

    uint32_t irq_id = vector_id - X86_IRQ_VECTOR_BASE;
#ifdef ENABLE_RTOS
    xPortRegisterCInterruptHandler(pci_trampoline_table[trampoline_idx], vector_id);
#else
    apic_register_interrupt(irq_id, pci_trampoline_table[trampoline_idx]);
#endif
    apic_enable_interrupt(irq_id);

    *vector_idx = vector_id;
    dev_info->vector_count++;
    trampoline_idx++;

    LOG_PCI_INFO("Allocated %s vector %u (IRQ ID: %u) for PCIe device: %04x:%04x (%02x:%02x.%x)", int_mode_to_string(dev_info->int_mode), vector_id, irq_id, dev_info->vendor_id, dev_info->device_id, dev_info->bus, dev_info->dev, dev_info->func);
    return 0;
#else // !ENABLE_PCI_INT
    (void)vector_idx;
    if (dev_info->int_mode != PCI_INT_MODE_NONE) {
        LOG_PCI_WARNING("PCI interrupt handling not enabled. Cannot allocate %s interrupt vector for PCIe device: %04x:%04x (%02x:%02x.%x)", int_mode_to_string(dev_info->int_mode), dev_info->vendor_id, dev_info->device_id, dev_info->bus, dev_info->dev, dev_info->func);
    }
    return -1;
#endif
}

uint16_t pci_irq_id_to_vector_idx(uint16_t irq_id)
{
    return irq_id - PCI_IRQ_VECTOR_BASE;
}

void init_pci(void)
{
#ifdef ENABLE_PCI
    if (initialized & PCI_INITIALIZED)
        return;
    pci_register_int_vector = pcie_register_int_vector;
    pci_database_t *pci_db = get_pci_database();
    log_set_level(DOMAIN_SYS, ENTITY_PCI, LOG_LEVEL_INFO);

    printf("\r[PCI] Starting PCIe enumeration...\n");
    pci_scan(0); // Start scanning from bus 0
    pci_alloc(); // Allocate MMIO resources and enable devices

    initialized |= PCI_INITIALIZED;

    uint16_t device_count = pci_db->device_count;
    LOG_PCI_INFO("PCI initialized. %lu device%s found.", device_count, device_count == 1 ? "" : "s");
#endif
}
