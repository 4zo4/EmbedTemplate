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

typedef struct {
    uint32_t magic;     // magic number (e.g., 0xACDCBABE)
    uint16_t vector_id; // 16-bit vector ID
    uint8_t  int_type;  // 8-bit interrupt type (e.g., INTx, MSI, MSI-X)
    uint8_t  pad;       // Padding to 64-bit boundary
} __attribute__((aligned(8))) trigger_irq_pkt_t;

static_assert(sizeof(trigger_irq_pkt_t) == 8, "trigger_irq_pkt_t struct size mismatch");

#define PCI_VENDOR_ID_VFIO 0x494f
#define PCI_DEVICE_ID_VFIO 0x0dc8
#define PCI_COMMAND_INTX_DISABLE 0x400 // IntX Disable bit in PCI command register
#define TRIGGER_IRQ_MAGIC 0xACDCBABE

uint64_t get_timestamp64(void);
uint32_t pcie_get_int_count(uint32_t vector);

volatile uint32_t *bar0_base_addr = nullptr; // Base address of BAR0 for PCIe device
static uint32_t    baseline = 0;             // baseline interrupt count for comparison
static bool        start_next_test = true;

void test_pci_validate_vfio_device(int test_idx, uint32_t offset, uint32_t value)
{
    bool pass = false;

    pci_device_info_t *dev_info = pci_find_by_id(PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);
    if (!dev_info)
        return;

    printf(UI_COLOR_CYAN "[TEST %d]" UI_STYLE_RESET " Initiating PCI validation sequence for PCI device: 0x%04X:0x%04X\r\n", test_idx, PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    LOG_PCI_INFO("[TEST %d] Starting PCI validation sequence", test_idx);
    LOG_PCI_DEBUG("VFIO device identified at %02x:%02x.%x", dev_info->bus, dev_info->dev, dev_info->func);

    uint32_t cmd = pci_read_config_dword(dev_info, PCI_CONFIG_COMMAND);
    uint16_t initial_cmd = (uint16_t)(cmd & 0xFFFF);
    uint16_t initial_status = (uint16_t)((cmd >> 16) & 0xFFFF);

    LOG_PCI_INFO("Initial Config Command: 0x%04X Status: 0x%04X", initial_cmd, initial_status);

    pci_config_device_resources(dev_info);

    cmd = pci_read_config_word(dev_info, PCI_CONFIG_COMMAND);
    uint32_t flipped_cmd = cmd ^ PCI_COMMAND_INTX_DISABLE;

    pci_write_config_word(dev_info, PCI_CONFIG_COMMAND, flipped_cmd);

    uint32_t verify_cmd = pci_read_config_word(dev_info, PCI_CONFIG_COMMAND);

    if ((verify_cmd & PCI_COMMAND_INTX_DISABLE) != (flipped_cmd & PCI_COMMAND_INTX_DISABLE)) {
        LOG_PCI_ERROR("Configuration space write verification mismatch!");
        pci_write_config_word(dev_info, PCI_CONFIG_COMMAND, cmd);
        goto exit;
    }

    pci_write_config_word(dev_info, PCI_CONFIG_COMMAND, cmd);
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
    if (pass == false)
        start_next_test = false;
    LOG_PCI_INFO("[TEST %d] VFIO Device validation %s", test_idx, pass ? "PASS" : "FAIL");
}

static bool test_pci_int_vector(void *bar0, uint32_t vector_id, pci_int_mode_t int_type)
{
    bool pass = false;

    baseline = pcie_get_int_count(vector_id);
    LOG_PCI_INFO("Capturing vector %u a baseline interrupt count: %u", vector_id, baseline);

    // set write to memory base address
    bar0_base_addr = (volatile uint32_t *)((uintptr_t)bar0 + 0);

    LOG_PCI_INFO("Triggering RTL PCI interrupt via BAR0 write '%s' to PCIe device", STR(TRIGGER_IRQ_MAGIC));
#ifndef ENABLE_RTOS
    (void)int_type;
    *bar0_base_addr = TRIGGER_IRQ_MAGIC; // trigger irq
    return pass;
#else
    // RTOS environment
    xTaskNotifyStateClear(nullptr);
    uint32_t discard_bits = 0;
    xTaskNotifyWait(0xFFFFFFFF, 0x00000000, &discard_bits, 0);

    alignas(8) static trigger_irq_pkt_t trigger_irq = {
        .magic = TRIGGER_IRQ_MAGIC,
        .vector_id = 0xFFFF,           // invalid vector ID (must use constant value as initializer)
        .int_type = PCI_INT_MODE_NONE, // invalid interrupt type (must use constant value as initializer)
        .pad = 0
    };
    trigger_irq.vector_id = vector_id; // set vector ID to trigger
    trigger_irq.int_type = int_type;   // set interrupt type
    write64(bar0_base_addr, &trigger_irq);

    LOG_PCI_INFO("Waiting for interrupt propagation...");
    uint32_t   int_vector_bmp = 0;
    BaseType_t notified = xTaskNotifyWait(0x00000000, 0xFFFFFFFF, &int_vector_bmp, 40);
    if ((notified == pdTRUE) && (int_vector_bmp & BIT(vector_id))) { // check is vector
        uint32_t post = pcie_get_int_count(vector_id);
        LOG_PCI_INFO("PCI interrupt verification done. Interrupt vector %u count baseline: %u post: %u", vector_id, baseline, post);
        if (post == baseline + 1) {
            pass = true;
            LOG_PCI_INFO("PCI interrupt captured by the RTOS");
        }
    } else if (notified == pdFALSE) {
        LOG_PCI_ERROR("Timeout waiting for PCI interrupt");
    }
    return pass;
#endif
}

// PCI interrupt test (2.1)
void test_pci_int(int test_idx)
{
    bool pass = false;

    if (!start_next_test)
        return;
    pci_device_info_t *dev_info = pci_find_by_id(PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    if (!dev_info)
        return;

    printf(UI_COLOR_CYAN "[TEST %d]" UI_STYLE_RESET " Initiating PCI interrupt validation for PCI device: 0x%04X:0x%04X\r\n", test_idx, PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    void *bar0 = pci_ioremap_bar(dev_info, 0);
    if (!bar0) {
        start_next_test = false;
        printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET
                               "[WARN] BAR 0 mapping omitted or unconfigured. Exiting\r\n");
        return;
    } else {
        LOG_PCI_INFO("[TEST %d] Starting PCI interrupt validation sequence", test_idx);
    }

    uint32_t       vector_id = 0;                 // interrupt vector id
    pci_int_mode_t int_type = dev_info->int_mode; // interrupt type

    pass = test_pci_int_vector(bar0, vector_id, int_type);
#ifdef ENABLE_RTOS
    if (pass == false)
        start_next_test = false;
    LOG_PCI_INFO("[TEST %d] VFIO Device PCI interrupt validation %s", test_idx, pass ? "PASS" : "FAIL");
#else
    (void)pass;
#endif
}

void test_pci_int_control(int test_idx)
{
    bool pass = false;
    bool ret = false;

    if (!start_next_test)
        return;
    pci_device_info_t *dev_info = pci_find_by_id(PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    if (!dev_info)
        return;

    printf(UI_COLOR_CYAN "[TEST %d]" UI_STYLE_RESET " Initiating PCI interrupt control for PCI device: 0x%04X:0x%04X\r\n", test_idx, PCI_VENDOR_ID_VFIO, PCI_DEVICE_ID_VFIO);

    void *bar0 = pci_ioremap_bar(dev_info, 0);
    if (!bar0) {
        start_next_test = false;
        printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET
                               "[WARN] BAR 0 mapping omitted or unconfigured. Exiting\r\n");
        return;
    } else {
        LOG_PCI_INFO("[TEST %d] Starting PCI interrupt control sequence", test_idx);
    }

    uint32_t       vector_id = 1;                 // interrupt vector id
    pci_int_mode_t int_type = dev_info->int_mode; // interrupt type

    LOG_PCI_INFO("[TEST %d] Disabling interrupt vector %u", test_idx, vector_id);
    pci_int_id_t int_id = {.type = INT_ID_VEC_ID, .is.vec_id = vector_id};
    pci_disable_interrupt(dev_info, int_id);
    ret = test_pci_int_vector(bar0, vector_id, int_type);

    if (!ret) {
        LOG_PCI_INFO("[TEST %d] Interrupt vector %u disabled", test_idx, vector_id);
        LOG_PCI_INFO("[TEST %d] Enabling interrupt vector %u", test_idx, vector_id);
        pci_enable_interrupt(dev_info, int_id);
        ret = test_pci_int_vector(bar0, vector_id, int_type);
        if (ret) {
            pass = true;
            LOG_PCI_INFO("[TEST %d] Interrupt vector %u enabled", test_idx, vector_id);
        } else {
            LOG_PCI_ERROR("[TEST %d] Interrupt vector %u enable failed", test_idx, vector_id);
        }
    } else {
        LOG_PCI_ERROR("[TEST %d] Interrupt vector %u disable failed", test_idx, vector_id);
    }

    LOG_PCI_INFO("[TEST %d] VFIO Device PCI interrupt control validation %s", test_idx, pass ? "PASS" : "FAIL");
}

void test_pci(void)
{
    log_set_level(DOMAIN_SYS, ENTITY_PCI, LOG_LEVEL_DEBUG); // set log level to debug for PCI test
    pci_dump_device_database();
    uint64_t value = get_timestamp64(); // get a value close to random
    test_pci_validate_vfio_device(1, 0x20, (uint32_t)(value & 0xFFFFFFFF));
#ifdef ENABLE_RTOS
    test_pci_int(2);
    test_pci_int_control(3);
    // revert log level to default after PCI tests
    log_set_level(DOMAIN_SYS, ENTITY_PCI, LOG_LEVEL_INFO);
#endif
}

#ifndef ENABLE_RTOS
// used in event loop
void pci_test_start(void)
{
    uint32_t primask = disable_interrupts();
    event_notify |= EVT_PCI_TEST; // trigger PCI test
    restore_interrupts(primask);
}

// PCI interrupt test epilogue (2.2)
void test_pci_post_int(int test_idx)
{
    bool     pass = false;
    uint32_t vector_id = 0; // interrupt vector id, must match the vector ID used in test_pci_int()
    uint32_t post = pcie_get_int_count(vector_id);
    LOG_PCI_INFO("PCI interrupt verification done. Interrupt vector %u count baseline: %u post: %u", vector_id, baseline, post);
    if (post == baseline + 1) {
        pass = true;
        LOG_PCI_INFO("PCI interrupt captured");
    }
    LOG_PCI_INFO("[TEST %d] VFIO Device PCI interrupt validation %s", test_idx, pass ? "PASS" : "FAIL");
    // revert log level to default after PCI tests
    log_set_level(DOMAIN_SYS, ENTITY_PCI, LOG_LEVEL_INFO);
}
#endif
