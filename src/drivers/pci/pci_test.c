#include <stdint.h>
#include <stdio.h>

#include "arch_ops.h"
#include "event.h"
#include "log.h"
#include "log_marker.h"
#include "pci.h"
#include "term_codes.h"
#include "umacro.h"
#include "utils.h"

#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#define PCI_VENDOR_ID_VFIO 0x494f
#define PCI_DEVICE_ID_VFIO 0x0dc8
#define PCI_COMMAND_INTX_DISABLE 0x400

uint64_t get_timestamp48(void);
uint32_t pcie_get_msi_irq_count(uint32_t vector);

volatile uint32_t *bar0_base_addr;

void test_pci_validate_vfio_device(int test_idx, uint32_t offset, uint32_t value)
{
    bool pass = false;

    pci_device_info_t *dev_info = pci_find_by_id(PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);
    if (!dev_info)
        return;

    printf(UI_COLOR_CYAN "[TEST %d]" UI_STYLE_RESET " Initiating PCI validation sequence for PCI device: 0x%04X:0x%04X\r\n", test_idx, PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    LOG_PCI_INFO("[TEST %d] Starting PCI validation sequence", test_idx);
    LOG_PCI_DEBUG("VFIO device identified at %02x:%02x.%x", dev_info->bus, dev_info->dev, dev_info->func);

    uint32_t status_cmd = pci_read_config_dword(dev_info, PCI_CONFIG_COMMAND);
    uint16_t initial_cmd = (uint16_t)(status_cmd & 0xFFFF);
    uint16_t initial_status = (uint16_t)((status_cmd >> 16) & 0xFFFF);

    LOG_PCI_INFO("Initial Config Command: 0x%04X Status: 0x%04X", initial_cmd, initial_status);

    pci_config_device_resources(dev_info);

    uint32_t cmd = pci_read_config_dword(dev_info, PCI_CONFIG_COMMAND);
    uint32_t flipped_cmd = cmd ^ PCI_COMMAND_INTX_DISABLE;

    pci_write_config_dword(dev_info, PCI_CONFIG_COMMAND, flipped_cmd);

    uint32_t verify_cmd = pci_read_config_dword(dev_info, PCI_CONFIG_COMMAND);

    if ((verify_cmd & PCI_COMMAND_INTX_DISABLE) != (flipped_cmd & PCI_COMMAND_INTX_DISABLE)) {
        LOG_PCI_ERROR("Configuration space write verification mismatch!");
        pci_write_config_dword(dev_info, PCI_CONFIG_COMMAND, cmd);
        goto exit;
    }

    pci_write_config_dword(dev_info, PCI_CONFIG_COMMAND, cmd);
    LOG_PCI_INFO("Configuration space read/write loopback verified");

    void *bar0 = pci_ioremap_bar(dev_info, 0);
    if (!bar0) {
        LOG_PCI_WARNING("BAR0 mapping omitted or unconfigured. Skipping structural memory runtime check.");
    } else {
        LOG_PCI_DEBUG("BAR0 mapped to CPU memory address: %p", bar0);

        volatile uint32_t *bar0_addr = (volatile uint32_t *)((uintptr_t)bar0 + offset);

        uint32_t read_val = *bar0_addr;
        LOG_PCI_INFO("Initial value at offset 0x%X: 0x%X", offset, read_val);

        LOG_PCI_INFO("Writing 0x%X to offset 0x%X", value, offset);
        *bar0_addr = value;

        read_val = *bar0_addr;
        LOG_PCI_INFO("Readback verification value: 0x%X", read_val);

        if (read_val == value)
            pass = true;
    }
exit:
    LOG_PCI_INFO("[TEST %d] VFIO Device validation %s", test_idx, pass ? "PASS" : "FAIL");
}

void test_pci_msi_irq(int test_idx)
{
    bool pass = false;

    pci_device_info_t *dev_info = pci_find_by_id(PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    if (!dev_info)
        return;

    printf(UI_COLOR_CYAN "[TEST %d]" UI_STYLE_RESET " Initiating MSI validation for PCI device: 0x%04X:0x%04X\r\n", test_idx, PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    void *bar0 = pci_ioremap_bar(dev_info, 0);
    if (!bar0) {
        printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET
                               "[WARN] BAR 0 mapping omitted or unconfigured. Exiting\r\n");
    } else {
        LOG_PCI_INFO("[TEST %d] Starting MSI validation sequence", test_idx);

        uint32_t baseline = pcie_get_msi_irq_count(0); // MSI vector 0
        LOG_PCI_INFO("Capturing a baseline interrupt count: %u", baseline);

        // set write to memory base address
        bar0_base_addr = (volatile uint32_t *)((uintptr_t)bar0 + 0);

        LOG_PCI_INFO("Triggering RTL MSI via BAR0 write 'acdcbabe' to PCIe device");
#ifndef ENABLE_RTOS
        *bar0_base_addr = 0xACDCBABE; // trigger irq
        pass = pass;                  // unused
    }
#else // RTOS
        xTaskNotifyStateClear(nullptr);
        uint32_t discard_bits = 0;
        xTaskNotifyWait(0xFFFFFFFF, 0x00000000, &discard_bits, 0);

        *bar0_base_addr = 0xACDCBABE; // trigger irq

        LOG_PCI_INFO("Waiting for interrupt propagation...");
        uint32_t   msi_vector_bmp = 0;
        BaseType_t notified = xTaskNotifyWait(0x00000000, 0xFFFFFFFF, &msi_vector_bmp, 40);
        if ((notified == pdTRUE) && (msi_vector_bmp & BIT(0))) { // check is vector 0
            uint32_t post = pcie_get_msi_irq_count(0);           // MSI vector 0
            LOG_PCI_INFO("MSI verification done. Interrupt count baseline: %u post: %u", baseline, post);
            if (post == baseline + 1) {
                pass = true;
                LOG_PCI_INFO("MSI interrupt captured by the RTOS");
            }
        } else if (notified == pdFALSE) {
            LOG_PCI_ERROR("Timeout waiting for MSI interrupt");
        }
    }
    LOG_PCI_INFO("[TEST %d] VFIO Device MSI validation %s", test_idx, pass ? "PASS" : "FAIL");
#endif
}

void test_pci(void)
{
    log_set_level(DOMAIN_SYS, ENTITY_PCI, LOG_LEVEL_DEBUG);
    pci_dump_device_database();
    uint64_t value = get_timestamp48();
    test_pci_validate_vfio_device(1, 0x20, (uint32_t)(value & 0xFFFFFFFF));
#ifdef ENABLE_RTOS
    test_pci_msi_irq(2);
    log_set_level(DOMAIN_SYS, ENTITY_PCI, LOG_LEVEL_INFO);
#endif
}

#ifndef ENABLE_RTOS
void pci_test_start(void)
{
    uint32_t primask = disable_interrupts();
    event_notify |= EVT_PCI_TEST;
    restore_interrupts(primask);
}

// used in event loop
void test_pci_post_msi_irq(int test_idx)
{
    LOG_PCI_INFO("MSI interrupt captured");
    uint32_t post = pcie_get_msi_irq_count(0); // MSI vector 0
    LOG_PCI_INFO("MSI verification done. Interrupt count post: %u", post);
    LOG_PCI_INFO("[TEST %d] VFIO Device MSI validation PASS", test_idx);
    log_set_level(DOMAIN_SYS, ENTITY_PCI, LOG_LEVEL_INFO);
}
#endif