#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pci.h"
#include "utils.h"

#ifdef ARCH_ARM
#define PCI_ECAM_BASE 0x10000000
#define PCI_MMIO_BASE 0x14000000
#elif defined(ARCH_RISCV)
#define PCI_ECAM_BASE 0x30000000
#define PCI_MMIO_BASE 0x40000000
#elif defined(ARCH_X86)
#define PCI_ECAM_BASE 0xB0000000
#define PCI_MMIO_BASE 0xC0000000
#else
#error "Unsupported CPU architecture"
#endif

#define PCI_SCANNED BIT(15)
#define PCI_MSI_BASE_ADDRESS pcie_msi_base_addr
#define PCI_MSI_CAP_ID 0x05
#define PCI_MSIX_CAP_ID 0x11
#define ECAM_ADDR16(offset) (*(volatile uint16_t *)(PCI_ECAM_BASE + (offset)))
#define ECAM_ADDR32(offset) (*(volatile uint32_t *)(PCI_ECAM_BASE + (offset)))

// The user port have to set platform-specific PCIe MSI base address
alignas(8) volatile uintptr_t pcie_msi_base_addr;

alignas(64) static pci_database_t pci_db;

alignas(8) static uint32_t free_mmio = PCI_MMIO_BASE;

pci_database_t *get_pci_database(void)
{
    return &pci_db;
}

pci_device_info_t *pci_find_by_id(uint16_t vendor_id, uint16_t device_id)
{
    for (int i = 0; i < pci_db.device_count; i++) {
        if (pci_db.device_info[i].vendor_id == vendor_id &&
            pci_db.device_info[i].device_id == device_id) {
            return &pci_db.device_info[i];
        }
    }
    return nullptr;
}

pci_device_info_t *pci_find_by_bdf(uint8_t bus, uint8_t dev, uint8_t func)
{
    for (int i = 0; i < pci_db.device_count; i++) {
        if (pci_db.device_info[i].bus == bus &&
            pci_db.device_info[i].dev == dev &&
            pci_db.device_info[i].func == func) {
            return &pci_db.device_info[i];
        }
    }
    return nullptr;
}

#ifdef ENABLE_PCI

static inline uint32_t pcie_read_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    /*
     * Compute the exact hardware address offset using the standard PCI-SIG formula:
     * Bus shifts by 20 bits (allows 1MB per bus)
     * Device shifts by 15 bits (allows 32KB per device slot)
     * Function shifts by 12 bits (allows 4KB per functional block)
     */
    uint32_t address_offset = ((uint32_t)bus << 20) |
        ((uint32_t)dev << 15) |
        ((uint32_t)func << 12) |
        ((uint32_t)offset & 0xFFC); // Enforce strict 4-byte dword alignment

    return ECAM_ADDR32(address_offset);
}

static inline void pcie_write_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t value)
{
    uint32_t address_offset = ((uint32_t)bus << 20) |
        ((uint32_t)dev << 15) |
        ((uint32_t)func << 12) |
        ((uint32_t)offset & 0xFFC);

    ECAM_ADDR32(address_offset) = value;
}

static inline uint32_t pcie_read_config_word(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    uint32_t address_offset = ((uint32_t)bus << 20) |
        ((uint32_t)dev << 15) |
        ((uint32_t)func << 12) |
        ((uint32_t)offset & 0xFFE);

    return ECAM_ADDR16(address_offset);
}

static inline void pcie_write_config_word(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t value)
{
    uint32_t address_offset = ((uint32_t)bus << 20) |
        ((uint32_t)dev << 15) |
        ((uint32_t)func << 12) |
        ((uint32_t)offset & 0xFFE);

    ECAM_ADDR16(address_offset) = value;
}

static void do_pci_scan(uint8_t bus)
{
    pci_device_info_t *pci_devices = pci_db.device_info;
    pci_bridge_info_t *pci_bridges = pci_db.bridge_info;

    for (uint8_t dev = 0; dev < MAX_PCI_DEVICES; dev++) {
        uint32_t id_reg_f0 = pcie_read_config_dword(bus, dev, 0, 0x00);
        uint16_t vendor_f0 = id_reg_f0 & 0xFFFFU;

        if (vendor_f0 == 0xFFFFU || vendor_f0 == 0x0000U)
            continue;

        // Read the Header Type register at Function 0 (Offset 0x0C)
        uint32_t hdr_reg_f0 = pcie_read_config_dword(bus, dev, 0, 0x0C);
        uint8_t  hdr_type_f0 = (uint8_t)((hdr_reg_f0 >> 16) & 0xFFU);
        uint8_t  num_functions = (hdr_type_f0 & 0x80U) ? MAX_FUNCTIONS : 1;

        for (uint8_t func = 0; func < num_functions; func++) {
            uint32_t id_reg = pcie_read_config_dword(bus, dev, func, PCI_CONFIG_VENDOR_ID);
            uint16_t vendor = id_reg & 0xFFFF;

            if (vendor == 0xFFFFU || vendor == 0x0000U)
                continue;

            if (pci_db.device_count >= MAX_PCI_DEVICES) {
                printf("\r[PCI Scan] Maximum device limit reached, skipping further enumeration.\n");
                return;
            }
            pci_device_info_t *pci_dev = &pci_devices[pci_db.device_count];
            uint32_t           hdr_reg = pcie_read_config_dword(bus, dev, func, PCI_CONFIG_HEADER_TYPE);
            uint8_t            hdr_type = (hdr_reg >> 16) & 0x7F;

            if (hdr_type == PCI_HEADER_TYPE_ENDPOINT) {
                pci_dev->bus = bus;
                pci_dev->dev = dev;
                pci_dev->func = func;
                pci_dev->vendor_id = vendor;
                pci_dev->device_id = (id_reg >> 16) & 0xFFFF;
                pci_dev->header_type = hdr_type;
                int bar_idx = 0;

                while (bar_idx < MAX_BARS) {
                    uint8_t  bar_offset = PCI_CONFIG_BAR0 + (bar_idx * 4);
                    uint32_t original_bar = pcie_read_config_dword(bus, dev, func, bar_offset);
                    if (original_bar == 0) {
                        while (bar_idx < MAX_BARS) {
                            pci_dev->bar_size[bar_idx] = 0;
                            pci_dev->bar_address[bar_idx] = original_bar & 0xFFFFFFF0UL;
                            bar_idx++;
                        }
                        break;
                    }
                    pcie_write_config_dword(bus, dev, func, bar_offset, 0xFFFFFFFFUL);
                    uint32_t read_back = pcie_read_config_dword(bus, dev, func, bar_offset);

                    if (read_back == 0 || read_back == 0xFFFFFFFFUL || (read_back & 0x01) != 0) {
                        pcie_write_config_dword(bus, dev, func, bar_offset, original_bar);
                        pci_dev->bar_size[bar_idx] = 0;
                        bar_idx++;
                        continue;
                    }

                    bool is_64bit = ((read_back & PCI_MEM_TYPE_MASK) == PCI_MEM_TYPE_64);

                    if (is_64bit && (bar_idx < (MAX_BARS - 1))) {
                        uint8_t upper_offset = bar_offset + 4;

                        uint32_t orig_upper = pcie_read_config_dword(bus, dev, func, upper_offset);
                        pcie_write_config_dword(bus, dev, func, upper_offset, 0xFFFFFFFFUL);
                        uint32_t upper_read_back = pcie_read_config_dword(bus, dev, func, upper_offset);

                        pcie_write_config_dword(bus, dev, func, bar_offset, original_bar);
                        pcie_write_config_dword(bus, dev, func, upper_offset, orig_upper);

                        uint64_t full_addr = ((uint64_t)orig_upper << 32) | (original_bar & 0xFFFFFFF0UL);
                        pci_dev->bar_address[bar_idx] = full_addr;

                        uint32_t lower_mask = read_back & 0xFFFFFFF0UL;
                        uint32_t upper_mask = upper_read_back;
                        uint64_t total_mask = ((uint64_t)upper_mask << 32) | lower_mask;

                        if (total_mask != 0) {
                            uint64_t size_64 = (~total_mask) + 1;
                            pci_dev->bar_size[bar_idx] = (size_64 > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)size_64;
                        } else {
                            pci_dev->bar_size[bar_idx] = 0;
                        }

                        pci_dev->bar_size[bar_idx + 1] = 0;
                        bar_idx += 2;
                    } else {
                        pci_dev->bar_address[bar_idx] = original_bar & 0xFFFFFFF0UL;
                        uint32_t lower_mask = read_back & 0xFFFFFFF0UL;
                        if (lower_mask != 0 && lower_mask != 0xFFFFFFF0UL) {
                            pci_dev->bar_size[bar_idx] = (~lower_mask) + 1;
                        } else {
                            pci_dev->bar_size[bar_idx] = 0;
                        }

                        pcie_write_config_dword(bus, dev, func, bar_offset, original_bar);
                        bar_idx++;
                    }
                }
                pci_db.device_count++;
            } else if (hdr_type == PCI_HEADER_TYPE_BRIDGE) {
                if (pci_db.bridge_count >= MAX_PCI_BRIDGES) {
                    printf("\r[PCI Scan] Maximum bridges limit reached, skipping further enumeration.\n");
                    return;
                }
                uint32_t bus_reg = pcie_read_config_dword(bus, dev, func, PCI_CONFIG_PRIMARY_BUS);
                uint8_t  secondary_bus = (bus_reg >> 8) & 0xFF;

                pci_bridge_info_t *pci_bridge = &pci_bridges[pci_db.bridge_count];

                pci_bridge->primary_bus = (uint8_t)(bus_reg & 0xFF);
                pci_bridge->secondary_bus = (uint8_t)((bus_reg >> 8) & 0xFF);
                pci_bridge->subordinate_bus = (uint8_t)((bus_reg >> 16) & 0xFF);

                pci_bridge->bus = bus;
                pci_bridge->dev = dev;
                pci_bridge->func = func;
                pci_bridge->vendor_id = vendor;
                pci_bridge->device_id = (id_reg >> 16) & 0xFFFF;
                pci_bridge->header_type = hdr_type;

                uint32_t mem_reg = pcie_read_config_dword(bus, dev, func, 0x20);
                pci_bridge->mem_base = (mem_reg & 0xFFF0) << 16;
                pci_bridge->mem_limit = (mem_reg & 0xFFF00000) | 0x000FFFFF;
                pci_bridge->mem_free = pci_bridge->mem_base;
                pci_db.bridge_count++;
                // Recursively traverse downstream
                if (secondary_bus > bus) {
                    do_pci_scan(secondary_bus);
                }
            }
        }
    }
}

void pci_scan(uint8_t bus)
{
    if (pci_db.flags & PCI_SCANNED)
        return;
    do_pci_scan(bus);
    pci_db.flags |= PCI_SCANNED;
}

void pci_alloc(void)
{
    pci_device_info_t *pci_devices = pci_db.device_info;
    pci_bridge_info_t *pci_bridges = pci_db.bridge_info;

    // Enable MMIO windows for bridges
    for (uint32_t i = 0; i < pci_db.bridge_count; i++) {
        pci_bridge_info_t *bridge = &pci_bridges[i];
        uint32_t           cmd = pcie_read_config_dword(bridge->bus, bridge->dev, bridge->func, PCI_CONFIG_COMMAND);
        pcie_write_config_dword(bridge->bus, bridge->dev, bridge->func, PCI_CONFIG_COMMAND, cmd | 0x06);
    }
    // Allocate BARs for endpoint devices
    for (uint32_t i = 0; i < pci_db.device_count; i++) {
        pci_device_info_t *dev = &pci_devices[i];

        if (dev->bus == 0 && dev->dev == 0 && dev->func == 0)
            continue;

        if (dev->header_type == PCI_HEADER_TYPE_ENDPOINT) {
            pci_bridge_info_t *parent_bridge = nullptr;
            if (dev->bus > 0) {
                for (uint16_t bdx = 0; bdx < pci_db.bridge_count; bdx++) {
                    if (pci_bridges[bdx].secondary_bus == dev->bus) {
                        parent_bridge = &pci_bridges[bdx];
                        break;
                    }
                }
            }

            for (int bar_idx = 0; bar_idx < MAX_BARS; bar_idx++) {
                if (dev->bar_size[bar_idx] == 0)
                    continue;

                uint32_t alignment = dev->bar_size[bar_idx];
                uint32_t mem_addr = 0;
                if (parent_bridge != nullptr) {
                    parent_bridge->mem_free = (parent_bridge->mem_free + (alignment - 1)) & ~(alignment - 1);
                    mem_addr = parent_bridge->mem_free;
                    parent_bridge->mem_free += dev->bar_size[bar_idx];
                } else {
                    free_mmio = (free_mmio + (alignment - 1)) & ~(alignment - 1);
                    mem_addr = free_mmio;
                    free_mmio += dev->bar_size[bar_idx];
                }

                uint32_t bar_offset = PCI_CONFIG_BAR0 + (bar_idx * 4);
                pcie_write_config_dword(dev->bus, dev->dev, dev->func, bar_offset, mem_addr);
                dev->bar_address[bar_idx] = mem_addr;

                printf("\r[PCI Alloc] PCI Device %04x:%04x BAR%d at 0x%08lx Size: %lu KB\n", dev->vendor_id, dev->device_id, bar_idx, mem_addr, dev->bar_size[bar_idx] >> 10);
                free_mmio += dev->bar_size[bar_idx]; // advance free MMIO pointer for next allocation
            }

            uint32_t cmd = pcie_read_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND);
            pcie_write_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND, cmd | 0x06);
            pci_alloc_irq_vectors(dev); // Allocate interrupt vectors for the device
        }
    }
}

static uint8_t pci_find_capability(pci_device_info_t *pci_dev, uint8_t match_cap_id)
{
    uint32_t status_cmd_reg = pci_read_config_dword(pci_dev, 0x04);
    uint16_t status_reg = (uint16_t)(status_cmd_reg >> 16);

    if (!(status_reg & BIT(4)))
        return 0;

    uint32_t cap_ptr = pci_read_config_dword(pci_dev, 0x34) & 0xFF;
    int      timeout = 16;

    while (cap_ptr != 0 && --timeout > 0) {
        uint32_t aligned_ptr = cap_ptr & 0xFC;
        uint32_t shift_bits = (cap_ptr & 0x03) * 8; // Calculate the byte-shift offset (0, 8, 16, or 24 bits)

        uint32_t raw_reg = pci_read_config_dword(pci_dev, aligned_ptr);
        uint32_t cap_hdr = raw_reg >> shift_bits;
        uint8_t  cap_id = cap_hdr & 0xFF;
        if (cap_id == match_cap_id)
            return (uint8_t)cap_ptr; // Found the requested capability, return capability offset

        cap_ptr = (cap_hdr >> 8) & 0xFF; // Move to next capability node offset index
    }

    return 0; // Capability not found
}

int pci_register_int_vector_null(pci_device_info_t *dev_info, uint32_t *vector_idx)
{
    (void)dev_info;
    (void)vector_idx;
    return -1; // No interrupt vectors available for this platform
}

/*
 * Platform-specific interrupt vector registration function pointer
 * This function pointer can be set to the appropriate platform-specific implementation during initialization.
 * If not set, it defaults to pci_register_int_vector_null, which returns no vectors available.
 */
int (*pci_register_int_vector)(pci_device_info_t *dev_info, uint32_t *vector_idx) = pci_register_int_vector_null;

void setup_msi_interrupts(pci_device_info_t *dev_info, uint8_t msi_cap_off)
{
    uint32_t vector_idx = 0; // set placeholder irq id
    if (pci_register_int_vector(dev_info, &vector_idx) != 0)
        return;

    pci_write_config_dword(dev_info, msi_cap_off + 0x04, PCI_MSI_BASE_ADDRESS);
    uint16_t msg_ctrl = pci_read_config_word(dev_info, msi_cap_off + 2);
    uint8_t  mmc_power = (msg_ctrl >> 1) & 0x07;
    uint16_t max_vectors = (1U << mmc_power);
    bool     is_64bit = (msg_ctrl & BIT(7)) != 0;

    uint32_t base_vector_idx = vector_idx;
    uint32_t vector_count = 1;

    for (; vector_count < max_vectors; vector_count++) {
        if (pci_register_int_vector(dev_info, &vector_idx) != 0)
            break; // No more vectors available
        if (vector_idx != base_vector_idx + vector_count) {
            printf("\r[PCI Error] Non-contiguous MSI vector %lu for device %02x:%02x.%x\n", vector_idx, dev_info->bus, dev_info->dev, dev_info->func);
            break;
        }
    }

    uint8_t mme_power = 0;
    while ((1U << (mme_power + 1)) <= vector_count) {
        mme_power++;
    }
    vector_count = 1U << mme_power; // Adjust vector count to the nearest power of two
    uint16_t irq_id = (base_vector_idx & 0xFFFFU);
    if (is_64bit) {
        pci_write_config_word(dev_info, msi_cap_off + 0x0C, irq_id);
        pci_write_config_dword(dev_info, msi_cap_off + 0x08, 0x00000000);
    } else {
        pci_write_config_word(dev_info, msi_cap_off + 0x08, irq_id);
    }

    msg_ctrl &= ~(0x07 << 4);
    msg_ctrl |= (mme_power & 0x07) << 4;
    msg_ctrl |= BIT(0); // set the MSI Enable flag inside the Control WORD
    pci_write_config_word(dev_info, msi_cap_off + 2, msg_ctrl);

    if (vector_count > 1) {
        printf("\r[PCI Irq Alloc] PCI Device %02x:%02x.%x MSI Capability Activated on Vectors %lu - %lu\n", dev_info->bus, dev_info->dev, dev_info->func, base_vector_idx, base_vector_idx + vector_count - 1);
    } else {
        printf("\r[PCI Irq Alloc] PCI Device %02x:%02x.%x MSI Capability Activated on Vector %lu\n", dev_info->bus, dev_info->dev, dev_info->func, base_vector_idx);
    }
}

typedef struct {
    uint32_t msg_addr_lo;
    uint32_t msg_addr_hi;
    uint32_t msg_data;
    uint32_t vector_ctrl;
} __attribute__((packed)) msix_entry_t;

void setup_msix_interrupts(pci_device_info_t *dev_info, uint8_t msix_cap_off)
{
    uint32_t vector_idx = 0; // set placeholder irq id
    if (pci_register_int_vector(dev_info, &vector_idx) != 0)
        return;

    uint16_t msg_ctrl = pci_read_config_word(dev_info, msix_cap_off + 2);
    uint16_t tbl_size = (msg_ctrl & 0x07FF) + 1; // Table Size is 11 bits, zero-based
    uint32_t table_reg = pci_read_config_dword(dev_info, msix_cap_off + 0x04);
    uint32_t table_bir = table_reg & 0x07;       // Lower 3 bits flag target BAR index
    uint32_t table_off = table_reg & 0xFFFFFFF8; // Upper 29 bits dinternal offset

    uint64_t bar_base_addr = dev_info->bar_address[table_bir];
    if (bar_base_addr == 0) {
        printf("\r[PCI Error] MSI-X BAR%lu not mapped/allocated!\n", table_bir);
        return;
    }

    uint32_t cmd = pci_read_config_dword(dev_info, PCI_CONFIG_COMMAND);
    pci_write_config_dword(dev_info, PCI_CONFIG_COMMAND, cmd | 0x02); // Enable Memory Space Decoding
    volatile msix_entry_t *msix_table = (volatile msix_entry_t *)(uintptr_t)(bar_base_addr + table_off);

    for (uint16_t tbl_idx = 0; tbl_idx < tbl_size; tbl_idx++) {
        msix_table[tbl_idx].msg_addr_lo = (uint32_t)PCI_MSI_BASE_ADDRESS;
        msix_table[tbl_idx].msg_addr_hi = 0x00000000;
        msix_table[tbl_idx].msg_data = (vector_idx & 0xFFFFU);
        msix_table[tbl_idx].vector_ctrl = 0x00000000; // Unmask Vector

        if (pci_register_int_vector(dev_info, &vector_idx) != 0) {
            break; // No more vectors available
        }
    }

    msg_ctrl |= BIT(15); // set the MSI-X Enable flag inside the Control WORD
    pci_write_config_word(dev_info, msix_cap_off + 2, msg_ctrl);

    printf("\r[PCI Irq Alloc] PCI Device %02x:%02x.%x MSI-X Capability Activated on Vector %lu\n", dev_info->bus, dev_info->dev, dev_info->func, vector_idx);
}

void setup_legacy_intx(pci_device_info_t *dev_info)
{
    uint32_t vector_idx = 0; // set placeholder irq id
    if (pci_register_int_vector(dev_info, &vector_idx) != 0)
        return;

    uint32_t int_reg = pci_read_config_dword(dev_info, 0x3C);
    uint8_t  int_pin = (uint8_t)((int_reg >> 8) & 0xFF);
    if (int_pin == 0)
        return; // No legacy INTx pin assigned

    int_reg = (int_reg & 0xFFFFFF00) | (vector_idx & 0xFF);
    pci_write_config_dword(dev_info, 0x3C, int_reg);

    printf("\r[PCI Irq Alloc] PCI Device %02x:%02x.%x Legacy INTx Activated on Vector %lu\n", dev_info->bus, dev_info->dev, dev_info->func, vector_idx);
}

void pci_alloc_irq_vectors(pci_device_info_t *dev_info)
{
    dev_info->int_mode = PCI_INT_MODE_NONE;
    dev_info->vector_count = 0;

    uint8_t msix_off = pci_find_capability(dev_info, PCI_MSIX_CAP_ID);
    uint8_t msi_off = pci_find_capability(dev_info, PCI_MSI_CAP_ID);

    uint32_t int_reg = pci_read_config_dword(dev_info, 0x3C);
    uint8_t  int_pin = (uint8_t)((int_reg >> 8) & 0xFF);

    if (int_pin != 0)
        dev_info->int_mode = PCI_INT_MODE_INTX;
    if (msi_off)
        dev_info->int_mode = PCI_INT_MODE_MSI;

    if (msix_off)
        dev_info->int_mode = PCI_INT_MODE_MSIX;

    if (msix_off) {
        setup_msix_interrupts(dev_info, msix_off);
        return;
    }

    if (msi_off) {
        setup_msi_interrupts(dev_info, msi_off);
        return;
    }

    if (int_pin != 0) {
        setup_legacy_intx(dev_info);
    }
}

uint16_t pci_read_config_word(pci_device_info_t *pci_dev, uint16_t addr)
{
    return pcie_read_config_word(pci_dev->bus, pci_dev->dev, pci_dev->func, addr);
}

uint32_t pci_read_config_dword(pci_device_info_t *pci_dev, uint16_t addr)
{
    return pcie_read_config_dword(pci_dev->bus, pci_dev->dev, pci_dev->func, addr);
}

int pci_write_config_word(pci_device_info_t *pci_dev, uint16_t addr, uint16_t value)
{
    pcie_write_config_word(pci_dev->bus, pci_dev->dev, pci_dev->func, addr, value);
    return 0;
}

int pci_write_config_dword(pci_device_info_t *pci_dev, uint16_t addr, uint32_t value)
{
    pcie_write_config_dword(pci_dev->bus, pci_dev->dev, pci_dev->func, addr, value);
    return 0;
}

uint16_t pcib_read_config_word(pci_bridge_info_t *pci_bridge, uint16_t addr)
{
    return pcie_read_config_word(pci_bridge->bus, pci_bridge->dev, pci_bridge->func, addr);
}

int pcib_write_config_word(pci_bridge_info_t *pci_bridge, uint16_t addr, uint16_t value)
{
    pcie_write_config_word(pci_bridge->bus, pci_bridge->dev, pci_bridge->func, addr, value);
    return 0;
}

int pcib_write_config_dword(pci_bridge_info_t *pci_bridge, uint16_t addr, uint32_t value)
{
    pcie_write_config_dword(pci_bridge->bus, pci_bridge->dev, pci_bridge->func, addr, value);
    return 0;
}

uint32_t pcib_read_config_dword(pci_bridge_info_t *pci_bridge, uint16_t addr)
{
    return pcie_read_config_dword(pci_bridge->bus, pci_bridge->dev, pci_bridge->func, addr);
}

#else  // !ENABLE_PCI
void pci_scan(uint8_t bus)
{
    (void)bus;
}
uint16_t pci_read_config_word(pci_device_info_t *pci_dev, uint16_t addr)
{
    (void)pci_dev;
    (void)addr;
    return 0;
}
uint32_t pci_read_config_dword(pci_device_info_t *pci_dev, int addr)
{
    (void)pci_dev;
    (void)addr;
    return 0;
}
int pci_write_config_word(pci_device_info_t *pci_dev, uint16_t addr, uint16_t value)
{
    (void)pci_dev;
    (void)addr;
    (void)value;
    return 0;
}
int pci_write_config_dword(pci_device_info_t *pci_dev, uint16_t addr, uint32_t value)
{
    (void)pci_dev;
    (void)addr;
    (void)value;
    return 0;
}
uint32_t pcib_read_config_dword(pci_bridge_info_t *pci_bridge, uint16_t addr)
{
    (void)pci_bridge;
    (void)addr;
    return 0;
}
int pcib_write_config_dword(pci_bridge_info_t *pci_bridge, uint16_t addr, uint32_t value)
{
    (void)pci_bridge;
    (void)addr;
    (void)value;
    return 0;
}
void pci_alloc(void)
{
}
#endif // ENABLE_PCI
