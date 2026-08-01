#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "log_marker.h"
#include "pci.h"
#include "term_codes.h"
#include "utils.h"

int pci_enable_device(pci_device_info_t *dev)
{
    uint32_t cmd = pci_read_config_dword(dev, PCI_CONFIG_COMMAND);
    cmd |= (PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
    pci_write_config_dword(dev, PCI_CONFIG_COMMAND, cmd);

    return 0;
}

void pci_set_master(pci_device_info_t *dev)
{
    uint32_t cmd = pci_read_config_dword(dev, PCI_CONFIG_COMMAND);
    cmd |= PCI_COMMAND_MASTER;
    pci_write_config_dword(dev, PCI_CONFIG_COMMAND, cmd);
}

void *pci_ioremap_bar(pci_device_info_t *dev, int bar)
{
    if (dev->bar_size[bar] == 0) {
        return nullptr;
    }

    uint8_t  bar_offset = PCI_CONFIG_BAR0 + (bar * 4);
    uint32_t bar_val = pci_read_config_dword(dev, bar_offset);

    if (bar_val == 0 || bar_val == 0xFFFFFFFFUL || (bar_val & 0x01) != 0)
        return nullptr;

    // uint64_t phy_addr = dev->bar_address[bar];
    uint64_t phy_addr = bar_val;

    bool is_64bit = ((bar_val & PCI_MEM_TYPE_MASK) == PCI_MEM_TYPE_64);

    if (is_64bit && (bar < (MAX_BARS - 1))) {
        uint8_t upper_offset = bar_offset + 4;
        bar_val = pci_read_config_dword(dev, upper_offset);
        phy_addr |= ((uint64_t)bar_val << 32);
    }

    phy_addr &= 0xFFFFFFFFFFFFFFF0ULL;

    return (void *)(uintptr_t)phy_addr;
}

const char *pci_get_header_type_string(uint8_t header_type)
{
    switch (header_type) {
    case 0x00:
        return "00h (Endpoint)";
    case 0x01:
        return "01h (Bridge)";
    case 0x02:
        return "02h (CardBus)";
    default:
        return "Unknown";
    }
}

void pci_dump_device_info(const pci_device_info_t *dev_info)
{
    if (!dev_info)
        return;

    const char *hdr_str = pci_get_header_type_string(dev_info->header_type);
    // clang-format off
    printf("PCI device: %04X:%04X (%02X:%02X.%X) Hdr: %s\r\n",
            dev_info->vendor_id, dev_info->device_id, dev_info->bus, dev_info->dev, dev_info->func, hdr_str);
    // clang-format on
    for (int i = 0; i < MAX_BARS; i++) {
        if (dev_info->bar_size[i] == 0 && dev_info->bar_address[i] == 0)
            continue;

        uint64_t    size = dev_info->bar_size[i];
        uint32_t    size_int = 0;
        uint32_t    size_frac = 0;
        const char *unit = "Bytes";

        if (size >= 1024 * 1024) {
            size_int = (uint32_t)(size / (1024 * 1024));
            size_frac = (uint32_t)(((size % (1024 * 1024)) * 100) / (1024 * 1024));
            unit = "MB";
        } else if (size >= 1024) {
            size_int = (uint32_t)(size / 1024);
            size_frac = (uint32_t)(((size % 1024) * 100) / 1024);
            unit = "KB";
        } else {
            size_int = (uint32_t)size;
            size_frac = 0;
            unit = "Bytes";
        }
        // clang-format off
        if (size >= 1024) {
            printf("  [BAR %d] Base: 0x%08llX Size: %lu.%02lu %s (0x%llX)\r\n",
                    i, (unsigned long long)dev_info->bar_address[i], size_int,
                    size_frac, unit, (unsigned long long)dev_info->bar_size[i]);
        } else {
            printf("  [BAR %d] Base: 0x%08llX Size: %lu %s (0x%llX)\r\n",
                    i, (unsigned long long)dev_info->bar_address[i], size_int,
                    unit, (unsigned long long)dev_info->bar_size[i]);
        }
        // clang-format on
    }
}

void pci_dump_bridge_info(const pci_bridge_info_t *bridge_info)
{
    if (!bridge_info)
        return;

    const char *hdr_str = pci_get_header_type_string(bridge_info->header_type);

    // clang-format off
    printf("PCI bridge: %04X:%04X (%02X:%02X.%X) Hdr: %s\r\n",
            bridge_info->vendor_id, bridge_info->device_id, bridge_info->bus,
            bridge_info->dev, bridge_info->func, hdr_str);
    printf("  Topology: Primary Bus %02X | Secondary Bus %02X | Subordinate Bus %02X\r\n",
            bridge_info->primary_bus, bridge_info->secondary_bus, bridge_info->subordinate_bus);
    // clang-format on

    if (bridge_info->mem_base != 0 && bridge_info->mem_limit >= bridge_info->mem_base) {
        uint32_t mem_window = bridge_info->mem_limit - bridge_info->mem_base + 1;
        // clang-format off
        printf("  MMIO Window: 0x%08lX - 0x%08lX Size: %lu KB\r\n",
                bridge_info->mem_base, bridge_info->mem_limit, mem_window / 1024);
        // clang-format on
    } else {
        printf("  MMIO Window: Disabled / Unconfigured\r\n");
    }
}

void pci_dump_device_database(void)
{
    pci_database_t *pci_db = get_pci_database();
    if (!pci_db)
        return;

    uint16_t device_count = pci_db->device_count;

    // clang-format off
    printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET UI_COLOR_GREEN " %u "
            UI_STYLE_RESET "device%s found:\r\n", device_count, device_count == 1 ? "" : "s");
    // clang-format on

    for (uint16_t i = 0; i < device_count; i++) {
        pci_dump_device_info(&pci_db->device_info[i]);
    }

    uint16_t bridge_count = pci_db->bridge_count;

    // clang-format off
    printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET UI_COLOR_GREEN " %u "
            UI_STYLE_RESET "bridge%s found:\r\n", bridge_count, bridge_count == 1 ? "" : "s");
    // clang-format on

    for (uint16_t i = 0; i < bridge_count; i++) {
        pci_dump_bridge_info(&pci_db->bridge_info[i]);
    }
}

#define PCI_BRIDGE_MEM_BASE 0x20 // Offset for PCI Bridge Memory Window
#define PCI_BRIDGE_MEM_LIMIT 0x22

void pci_config_device_resources(pci_device_info_t *dev)
{
    if (!dev)
        return;

    int log_level = log_get_level(DOMAIN_SYS, ENTITY_PCI);

    // clang-format off
    printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET " Programming BARs for "
            UI_COLOR_GREEN "%04X:%04X (%02X:%02X.%X)" UI_STYLE_RESET "\r\n",
            dev->vendor_id, dev->device_id, dev->bus, dev->dev, dev->func);
    // clang-format on

    if (dev->bus > 0) {
        pci_database_t    *pci_db = get_pci_database();
        pci_bridge_info_t *parent_bridge = nullptr;

        if (pci_db) {
            for (uint16_t i = 0; i < pci_db->bridge_count; i++) {
                pci_bridge_info_t *bridge = &pci_db->bridge_info[i];
                if (bridge->header_type == PCI_HEADER_TYPE_BRIDGE) {
                    uint32_t buses = pcib_read_config_dword(bridge, PCI_CONFIG_PRIMARY_BUS);
                    uint8_t  secondary_bus = (uint8_t)((buses >> 8) & 0xFF);

                    if (secondary_bus == dev->bus) {
                        parent_bridge = bridge;
                        break;
                    }
                }
            }
        }

        if (parent_bridge) {
            uint32_t base = 0xFFFFFFFF;
            uint32_t limit = 0;
            for (int bar_idx = 0; bar_idx < MAX_BARS; bar_idx++) {
                if (dev->bar_size[bar_idx] > 0 &&
                    dev->bar_address[bar_idx] != 0) {
                    uint32_t start = (uint32_t)dev->bar_address[bar_idx];
                    uint32_t end = start + (uint32_t)dev->bar_size[bar_idx] - 1;

                    if (start < base) {
                        base = start;
                    }
                    if (end > limit) {
                        limit = end;
                    }
                }
            }

            if (limit) {
                uint16_t mem_base = (uint16_t)((base & 0xFFF00000) >> 16);
                uint16_t mem_limit = (uint16_t)((limit & 0xFFF00000) >> 16);

                pcib_write_config_word(parent_bridge, PCI_BRIDGE_MEM_BASE, mem_base);
                pcib_write_config_word(parent_bridge, PCI_BRIDGE_MEM_LIMIT, mem_limit);
                uint16_t mem_base_read = pcib_read_config_word(parent_bridge, PCI_BRIDGE_MEM_BASE);
                if (mem_base_read != mem_base)
                    printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET
                                           "[ERROR] Invalid upstream PCI Bridge MMIO Window\r\n");

                uint32_t phy_base = base & 0xFFF00000;
                uint32_t phy_limit = limit | 0x000FFFFF;
                uint32_t size = phy_limit - phy_base + 1;

                // clang-format off
                printf("Setting upstream PCI Bridge (%02X:%02X.%X) MMIO Window: 0x%08lX - 0x%08lX Size: %lu KB\r\n",
                        parent_bridge->bus, parent_bridge->dev, parent_bridge->func, (unsigned long long)phy_base,
                        (unsigned long long)phy_limit, (unsigned long long)size / 1024);
            } else {
                pcib_write_config_dword(parent_bridge, PCI_BRIDGE_MEM_BASE, 0x0000FFFF);
                printf("Setting upstream PCI Bridge (%02X:%02X.%X) memory window disabled\r\n",
                        parent_bridge->bus, parent_bridge->dev, parent_bridge->func);
                // clang-format on
            }
        } else {
            // clang-format off
            printf(UI_COLOR_YELLOW "[SYS:PCI]" UI_STYLE_RESET
                    "[WARN] No matching upstream PCI Bridge found for Bus %d\r\n", dev->bus);
            // clang-format on
        }
    }

    uint32_t old_cmd = pci_read_config_dword(dev, PCI_CONFIG_COMMAND);
    pci_write_config_dword(dev, PCI_CONFIG_COMMAND, old_cmd & ~0x3);

    for (int bar_idx = 0; bar_idx < MAX_BARS; bar_idx++) {
        if (dev->bar_size[bar_idx] == 0)
            continue;

        uint8_t  bar_offset = PCI_CONFIG_BAR0 + (bar_idx * 4);
        uint32_t value = (uint32_t)dev->bar_address[bar_idx];

        if (log_level >= LOG_LEVEL_DEBUG) {
            // clang-format off
            printf("Writing Base Address 0x%08lX to BAR%d (Offset 0x%02X)\r\n",
                    (unsigned long long)value, bar_idx, bar_offset);
            // clang-format on
        }

        pci_write_config_dword(dev, bar_offset, value);
    }

    pci_enable_device(dev);
    pci_set_master(dev);
}
