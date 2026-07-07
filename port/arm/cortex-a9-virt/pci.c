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
#include "umacro.h"
#include "pci.h"

#define LOG_PCI_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_PCI), __VA_ARGS__)

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

#define GICV2M_SPI_BASE 48 // Shared Peripheral Interrupt (SPI) IRQ ID base for QEMU's GICv2m MSI frame

void pcie_msi_vector_handler(uint32_t index); // Forward declaration of the MSI vector handler

#if (MAX_PCI_MSI > 0)

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

static uint32_t vector_count;

#ifdef ENABLE_RTOS
void pcie_msi_vector_handler(uint32_t index)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    LOG_PCI_DEBUG("PCI MSI interrupt %u received.", GICV2M_SPI_BASE + index);

    extern TaskHandle_t xPciHandle;
    if (xPciHandle != nullptr) {
        xTaskNotifyFromISR(xPciHandle, BIT(index), eSetBits, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
#else // !ENABLE_RTOS
void pcie_msi_vector_handler(uint32_t index)
{
    event_notify |= EVT_MSI_IDX(index);

    LOG_PCI_DEBUG("PCI MSI interrupt %u received, event 0x%x set.", GICV2M_SPI_BASE + index, EVT_MSI_IDX(index));
}
#endif
#endif // ENABLE_MSI

int pcie_register_msi_vector(pci_device_info_t *dev_info, uint32_t *vector_idx)
{
#if ENABLE_MSI
    if (vector_count >= MAX_PCI_MSI)
        return -1; // No more vectors available

    uint32_t irq_id = GICV2M_SPI_BASE + vector_count;
    gic_register_interrupt(irq_id, pci_trampoline_table[vector_count]);
    gic_enable_interrupt(irq_id, 0x20);
    *vector_idx = vector_count++;

    LOG_PCI_INFO("Allocated MSI vector %u (IRQ ID: %u) for PCIe device: %04x:%04x (%02x:%02x.%x)", *vector_idx, irq_id, dev_info->vendor_id, dev_info->device_id, dev_info->bus, dev_info->dev, dev_info->func);
    return 0;
#else
    (void)vector_idx;
    LOG_PCI_WARNING("MSI not enabled. Cannot allocate MSI vector for PCIe device: %04x:%04x (%02x:%02x.%x)", dev_info->vendor_id, dev_info->device_id, dev_info->bus, dev_info->dev, dev_info->func);
    return -1;
#endif
}

void init_pci(void)
{
#ifdef ENABLE_PCI
    pci_device_database_t *pci_db = get_pci_device_database();
    vector_count = 0;

    printf("\r[PCI] Starting PCIe enumeration...\n");
    pcie_scan(0); // Start scanning from bus 0
    pcie_alloc(); // Allocate MMIO resources and enable devices

    LOG_PCI_INFO("PCI initialized. %lu device(s) found. MSI vectors available: %lu", pci_db->device_count, vector_count);
#endif
}