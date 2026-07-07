
#include <stdio.h>

#include "pci.h"

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

#define PCI_MSI_BASE_ADDRESS pcie_msi_base_addr
#define PCI_MSI_CAP_ID 0x05

// The user port have to set platform-specific PCIe MSI base address
alignas(8) volatile uintptr_t pcie_msi_base_addr;

static pci_device_database_t pci_db = {.device_count = 0};
static uint32_t              free_mmio = PCI_MMIO_BASE;

pci_device_database_t *get_pci_device_database(void)
{
    return &pci_db;
}

#ifdef ENABLE_PCI

void pcie_scan(uint8_t bus)
{
    pci_device_info_t *pci_devices = pci_db.device_info;

    for (uint8_t dev = 0; dev < MAX_PCI_DEVICES; dev++) {
        for (uint8_t func = 0; func < MAX_FUNCTIONS; func++) {
            uint32_t id_reg = pcie_read_config_dword(bus, dev, func, PCI_CONFIG_VENDOR_ID);
            uint16_t vendor = id_reg & 0xFFFF;

            if (vendor == 0xFFFF || vendor == 0x0000) {
                if (func == 0)
                    break;
                continue;
            }

            if (pci_db.device_count >= MAX_PCI_DEVICES) {
                printf("\r[PCI Scan] Maximum device limit reached, skipping further enumeration.\n");
                return;
            }
            pci_device_info_t *pci_dev = &pci_devices[pci_db.device_count++];
            uint32_t           hdr_reg = pcie_read_config_dword(bus, dev, func, PCI_CONFIG_HEADER_TYPE);
            uint8_t            hdr_type = (hdr_reg >> 16) & 0x7F;

            pci_dev->bus = bus;
            pci_dev->dev = dev;
            pci_dev->func = func;
            pci_dev->vendor_id = vendor;
            pci_dev->device_id = (id_reg >> 16) & 0xFFFF;
            pci_dev->header_type = hdr_type;

            if (hdr_type == PCI_HEADER_TYPE_ENDPOINT) {
                for (int bar_idx = 0; bar_idx < MAX_BARS; bar_idx++) {
                    uint32_t bar_offset = PCI_CONFIG_BAR0 + (bar_idx * 4);
                    uint32_t original_bar = pcie_read_config_dword(bus, dev, func, bar_offset);
                    pcie_write_config_dword(bus, dev, func, bar_offset, 0xFFFFFFFF);
                    uint32_t read_back = pcie_read_config_dword(bus, dev, func, bar_offset);
                    pcie_write_config_dword(bus, dev, func, bar_offset, original_bar);

                    if ((read_back & 0x1) == 0 && read_back != 0) {
                        uint32_t mask = read_back & 0xFFFFFFF0;
                        pci_dev->bar_size[bar_idx] = (~mask) + 1;
                    } else {
                        pci_dev->bar_size[bar_idx] = 0; // Unused or I/O space
                    }
                }
            } else if (hdr_type == PCI_HEADER_TYPE_BRIDGE) {
                uint32_t bus_reg = pcie_read_config_dword(bus, dev, func, PCI_CONFIG_PRIMARY_BUS);
                uint8_t  secondary_bus = (bus_reg >> 8) & 0xFF;

                // Recursively traverse downstream
                if (secondary_bus > bus) {
                    pcie_scan(secondary_bus);
                }
            }
        }
    }
}

void pcie_alloc(void)
{
    pci_device_info_t *pci_devices = pci_db.device_info;

    for (uint32_t i = 0; i < pci_db.device_count; i++) {
        pci_device_info_t *dev = &pci_devices[i];

        if (dev->header_type == PCI_HEADER_TYPE_ENDPOINT) {
            for (int bar_idx = 0; bar_idx < MAX_BARS; bar_idx++) {
                if (dev->bar_size[bar_idx] == 0)
                    continue;

                uint32_t alignment = dev->bar_size[bar_idx];
                free_mmio = (free_mmio + (alignment - 1)) & ~(alignment - 1);

                uint32_t bar_offset = PCI_CONFIG_BAR0 + (bar_idx * 4);
                pcie_write_config_dword(dev->bus, dev->dev, dev->func, bar_offset, free_mmio);
                dev->bar_address[bar_idx] = free_mmio;

                printf("\r[PCI Alloc] PCI Device %04x:%04x BAR%d at 0x%08lx Size: %lu KB\n", dev->vendor_id, dev->device_id, bar_idx, free_mmio, dev->bar_size[bar_idx] / 1024);
                free_mmio += dev->bar_size[bar_idx]; // advance free MMIO pointer for next allocation
            }

            pcie_alloc_irq_vectors(dev->bus, dev->dev, dev->func, dev);

            uint32_t cmd = pcie_read_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND);
            pcie_write_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND, cmd | 0x06);
        } else if (dev->header_type == PCI_HEADER_TYPE_BRIDGE) {
            /*
               For a complete system, read endpoints downstream of this bridge,
               calculate total required window footprint bounds, and program
               the Bridge Memory Base and Limit Registers (Offset 0x20).
            */
            uint32_t bridge_mem_base = free_mmio & 0xFFF00000;
            pcie_write_config_dword(dev->bus, dev->dev, dev->func, 0x20, (bridge_mem_base >> 16));
            uint32_t cmd = pcie_read_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND);
            pcie_write_config_dword(dev->bus, dev->dev, dev->func, PCI_CONFIG_COMMAND, cmd | 0x06);
        }
    }
}

uint32_t pcie_read_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
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
        ((uint32_t)offset & 0xFC); // Enforce strict 4-byte dword alignment

    return *((volatile uint32_t *)(PCI_ECAM_BASE + address_offset));
}

void pcie_write_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address_offset = ((uint32_t)bus << 20) |
        ((uint32_t)dev << 15) |
        ((uint32_t)func << 12) |
        ((uint32_t)offset & 0xFC);

    *((volatile uint32_t *)(PCI_ECAM_BASE + address_offset)) = value;
}

uint8_t pcie_find_capability(uint8_t bus, uint8_t dev, uint8_t func, uint8_t match_cap_id)
{
    uint32_t cap_ptr = pcie_read_config_dword(bus, dev, func, 0x34) & 0xFF;
    while (cap_ptr != 0) {
        uint32_t cap_hdr = pcie_read_config_dword(bus, dev, func, cap_ptr);
        uint8_t  cap_id = cap_hdr & 0xFF;
        if (cap_id == match_cap_id) {
            return cap_ptr; // Found the requested capability
        }
        cap_ptr = (cap_hdr >> 8) & 0xFF; // Move to next capability
    }
    return 0; // Capability not found
}

// Forward declaration of the platform-specific MSI vector registration function
int pcie_register_msi_vector(pci_device_info_t *dev_info, uint32_t *vector_idx);

void pcie_alloc_irq_vectors(uint8_t bus, uint8_t dev, uint8_t func, pci_device_info_t *dev_info)
{
    uint8_t msi_cap_offset = pcie_find_capability(bus, dev, func, PCI_MSI_CAP_ID);
    if (!msi_cap_offset)
        return;

    uint32_t vector_idx = 0;

    if (pcie_register_msi_vector(dev_info, &vector_idx) != 0) {
        printf("\r[PCI Irq Alloc]] Error: Platform failed to allocate MSI vector for device %02x:%02x.%x\n", bus, dev, func);
        return;
    }

    pcie_write_config_dword(bus, dev, func, msi_cap_offset + 0x04, PCI_MSI_BASE_ADDRESS);

    uint32_t data_reg = pcie_read_config_dword(bus, dev, func, msi_cap_offset + 0x08);
    data_reg = (data_reg & 0xFFFF0000UL) | (vector_idx & 0xFFFFU);
    pcie_write_config_dword(bus, dev, func, msi_cap_offset + 0x08, data_reg);

    uint32_t msg_ctrl_reg = pcie_read_config_dword(bus, dev, func, msi_cap_offset + 0x00);
    msg_ctrl_reg |= (1UL << 16); // Bit 16 is the MSI Enable flag inside the Control DWORD
    pcie_write_config_dword(bus, dev, func, msi_cap_offset + 0x00, msg_ctrl_reg);

    printf("\r[PCI Irq Alloc] PCI Device %02x:%02x.%x MSI Capability Activated on Vector %lu\n", bus, dev, func, vector_idx);
}

#else  // !ENABLE_PCI
void pcie_scan(uint8_t bus)
{
    (void)bus;
}
uint32_t pcie_read_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    return 0;
}
void pcie_write_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    (void)value; // No operation
}
uint8_t pcie_find_capability(uint8_t bus, uint8_t dev, uint8_t func, uint8_t match_cap_id)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)match_cap_id;
    return 0; // Capability not found
}
void pcie_alloc(void)
{
}
#endif // ENABLE_PCI
