/**
 * @file pci.c
 * @brief PCI (Peripheral Component Interconnect)-specific implementations for QEMU Cortex-A15 Virt.
 * This file contains the implementations of the PCI-specific functions for the QEMU Cortex-A15 Virt,
 * including PCI enumeration and MSI configuration.
 */
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "event.h"
#include "gic.h"
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
#define GICV2M_SPI_BASE 48 // Shared Peripheral Interrupt (SPI) IRQ ID base for QEMU's GICv2m MSI frame

#define MAX_MSI_SEQUENCE MAX_SEQUENCE // Maximum MSI sequence for trampoline generation
#ifndef MAX_PCI_MSI
#define MAX_PCI_MSI 2 // Maximum number of PCI MSI vectors supported for the system
#endif
#if MAX_PCI_MSI > MAX_MSI_SEQUENCE
#error "MAX_PCI_MSI cannot exceed MAX_MSI_SEQUENCE"
#endif

#ifndef ENABLE_PCI
#undef MAX_PCI_MSI
#define MAX_PCI_MSI 0
#endif

void pcie_msi_vector_handler(uint32_t index); // Forward declaration of the MSI vector handler

#if (MAX_PCI_MSI > 0)

typedef struct pcie_msi_stats_s {
    uint32_t count[MAX_PCI_MSI];
} pcie_msi_stats_t;

#define ENABLE_MSI
/*
   Boilerplate X-Macros to generate trampolines for each MSI vector
   Example for MAX_PCI_MSI = 2:
   static void pcie_msi_trampoline_0(void) { pcie_msi_vector_handler(0); }
   static void pcie_msi_trampoline_1(void) { pcie_msi_vector_handler(1); }
   static const irq_handler_t pci_trampoline_table[] = {
       pcie_msi_trampoline_0, pcie_msi_trampoline_1,
   };
*/
#define CONCAT(a, b) a##_##b
#define ADD_NAME(prefix, index) CONCAT(prefix, index)
// Rollup the specified number of trampoline functions
#define CREATE_TRAMPOLINE_FUNC(prefix, index) \
    static void ADD_NAME(prefix, index)(void) \
    { \
        pcie_msi_vector_handler(index); \
    }
CREATE_LIST(CREATE_TRAMPOLINE_FUNC, pcie_msi_trampoline, MAX_PCI_MSI)
#undef CREATE_TRAMPOLINE_FUNC
// Rollup the specified number of trampoline functions into a trampoline table
#define ADD_TRAMPOLINES(prefix, index) ADD_NAME(prefix, index),
static const irq_handler_t pci_trampoline_table[] = {
    CREATE_LIST(ADD_TRAMPOLINES, pcie_msi_trampoline, MAX_PCI_MSI)
};
#undef ADD_TRAMPOLINES
#endif // MAX_PCI_MSI > 0

#ifdef ENABLE_MSI
static pcie_msi_stats_t pcie_msi_stats;

uint32_t pcie_get_msi_irq_count(uint32_t vector)
{
    return pcie_msi_stats.count[vector];
}

#ifdef ENABLE_RTOS
extern TaskHandle_t xPciHandle;
void                pcie_msi_vector_handler(uint32_t index)
{
    LOG_PCI_DEBUG("PCI MSI interrupt %u received", GICV2M_SPI_BASE + index);

    pcie_msi_stats.count[index]++;

    if (xPciHandle != nullptr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        io_barrier();
        xTaskNotifyFromISR(xPciHandle, BIT(index), eSetBits, &xHigherPriorityTaskWoken);
        io_barrier();
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
#else // !ENABLE_RTOS
void pcie_msi_vector_handler(uint32_t index)
{
    LOG_PCI_DEBUG("PCI MSI interrupt %u received, event 0x%x set", GICV2M_SPI_BASE + index, EVT_MSI_IDX(index));

    pcie_msi_stats.count[index]++;

    event_notify |= EVT_MSI_IDX(index);
}
#endif
#endif // ENABLE_MSI

int pcie_register_msi_vector(pci_device_info_t *dev_info, uint32_t *vector_idx)
{
#ifdef ENABLE_MSI
    static uint32_t vector_count = 0;

    if (dev_info->vendor_id != PCI_VENDOR_ID_VFIO &&
        dev_info->device_id != PCI_DEVICE_ID_VFIO)
        return -1;

    if (vector_count >= MAX_PCI_MSI)
        return -1; // No more vectors available

    uint32_t vector_id = GICV2M_SPI_BASE + vector_count;
    uint32_t irq_id = vector_id - GICV2M_SPI_BASE;

    gic_register_interrupt(irq_id, pci_trampoline_table[vector_count]);
    gic_enable_interrupt(irq_id, 0x20);
    *vector_idx = vector_id;

    vector_count++;

    LOG_PCI_INFO("Allocated MSI vector %u (IRQ ID: %u) for PCIe device: %04x:%04x (%02x:%02x.%x)", vector_id, irq_id, dev_info->vendor_id, dev_info->device_id, dev_info->bus, dev_info->dev, dev_info->func);
    return 0;
#else // !ENABLE_MSI
    (void)vector_idx;
    LOG_PCI_WARNING("MSI not enabled. Cannot allocate MSI vector for PCIe device: %04x:%04x (%02x:%02x.%x)", dev_info->vendor_id, dev_info->device_id, dev_info->bus, dev_info->dev, dev_info->func);
    return -1;
#endif
}

void init_pci(void)
{
#ifdef ENABLE_PCI
    if (initialized & PCI_INITIALIZED)
        return;
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
