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

void pci_scan(uint8_t bus)
{
    pci_device_info_t *pci_devices = pci_db.device_info;
    pci_bridge_info_t *pci_bridges = pci_db.bridge_info;

    if (pci_db.flags & PCI_SCANNED)
        return;

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
                pci_db.flags |= PCI_SCANNED;
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
                    pci_db.flags |= PCI_SCANNED;
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
                pci_bridge->mem_limit = (mem_reg & 0xFFF00000);
                pci_db.bridge_count++;
                // Recursively traverse downstream
                if (secondary_bus > bus) {
                    pci_scan(secondary_bus);
                }
            }
        }
    }
}

void pci_alloc(void)
{
    pci_device_info_t *pci_devices = pci_db.device_info;

    for (uint32_t i = 0; i < pci_db.device_count; i++) {
        pci_device_info_t *dev = &pci_devices[i];

        if (dev->bus == 0 && dev->dev == 0 && dev->func == 0)
            continue;

        if (dev->header_type == PCI_HEADER_TYPE_ENDPOINT) {
            for (int bar_idx = 0; bar_idx < MAX_BARS; bar_idx++) {
                if (dev->bar_size[bar_idx] == 0)
                    continue;

                uint32_t alignment = dev->bar_size[bar_idx];
                free_mmio = (free_mmio + (alignment - 1)) & ~(alignment - 1);

                uint32_t bar_offset = PCI_CONFIG_BAR0 + (bar_idx * 4);
                pcie_write_config_dword(dev->bus, dev->dev, dev->func, bar_offset, free_mmio);
                dev->bar_address[bar_idx] = free_mmio;

                printf("\r[PCI Alloc] PCI Device %04x:%04x BAR%d at 0x%08lx Size: %lu KB\n", dev->vendor_id, dev->device_id, bar_idx, free_mmio, dev->bar_size[bar_idx] >> 10);
                free_mmio += dev->bar_size[bar_idx]; // advance free MMIO pointer for next allocation
            }

            pci_alloc_irq_vectors(dev);
            uint32_t cmd = pcie_read_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND);
            pcie_write_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND, cmd | 0x06);
        } else if (dev->header_type == PCI_HEADER_TYPE_BRIDGE) {
            uint32_t bridge_mem_base = free_mmio & 0xFFF00000;
            pcie_write_config_dword(dev->bus, dev->dev, dev->func, 0x20, (bridge_mem_base >> 16));
            uint32_t cmd = pcie_read_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND);
            pcie_write_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND, cmd | 0x06);
        }
    }
}

static uint8_t pcie_find_capability(uint8_t bus, uint8_t dev, uint8_t func, uint8_t match_cap_id)
{
    uint32_t status_cmd_reg = pcie_read_config_dword(bus, dev, func, 0x04);
    uint16_t status_reg = (uint16_t)(status_cmd_reg >> 16);

    if (!(status_reg & BIT(4)))
        return 0;

    uint32_t cap_ptr = pcie_read_config_dword(bus, dev, func, 0x34) & 0xFF;
    int      timeout = 16;

    while (cap_ptr != 0 && --timeout > 0) {
        uint32_t aligned_ptr = cap_ptr & 0xFC;
        uint32_t shift_bits = (cap_ptr & 0x03) * 8; // Calculate the byte-shift offset (0, 8, 16, or 24 bits)

        uint32_t raw_reg = pcie_read_config_dword(bus, dev, func, aligned_ptr);
        uint32_t cap_hdr = raw_reg >> shift_bits;
        uint8_t  cap_id = cap_hdr & 0xFF;
        if (cap_id == match_cap_id)
            return (uint8_t)cap_ptr; // Found the requested capability, return capability offset

        cap_ptr = (cap_hdr >> 8) & 0xFF; // Move to next capability node offset index
    }

    return 0; // Capability not found
}

// Forward declaration of the platform-specific MSI vector registration function
int pcie_register_msi_vector(pci_device_info_t *dev_info, uint32_t *vector_idx);

void pci_alloc_irq_vectors(pci_device_info_t *dev_info)
{
    uint8_t msi_cap_offset = pcie_find_capability(dev_info->bus, dev_info->dev, dev_info->func, PCI_MSI_CAP_ID);
    if (!msi_cap_offset)
        return;

    uint32_t vector_idx = 0; // set placeholder irq id
    if (pcie_register_msi_vector(dev_info, &vector_idx) != 0)
        return;

    pci_write_config_dword(dev_info, msi_cap_offset + 0x04, PCI_MSI_BASE_ADDRESS);
    pci_write_config_dword(dev_info, msi_cap_offset + 0x08, 0x00000000);

    uint16_t irq_id = (vector_idx & 0xFFFFU);
    pci_write_config_word(dev_info, msi_cap_offset + 0x0C, irq_id);
    uint16_t msg_ctrl = pci_read_config_word(dev_info, msi_cap_offset + 2);
    msg_ctrl |= BIT(0); // set the MSI Enable flag inside the Control WORD
    pci_write_config_word(dev_info, msi_cap_offset + 2, msg_ctrl);

    printf("\r[PCI Irq Alloc] PCI Device %02x:%02x.%x MSI Capability Activated on Vector %lu\n", dev_info->bus, dev_info->dev, dev_info->func, vector_idx);
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
