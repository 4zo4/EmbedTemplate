
#pragma once
#include <stdint.h>

#define MAX_PCI_DEVICES 32
#define MAX_BARS 6
#define MAX_FUNCTIONS 8

#define PCI_CONFIG_VENDOR_ID 0x00
#define PCI_CONFIG_COMMAND 0x04
#define PCI_CONFIG_HEADER_TYPE 0x0C
#define PCI_CONFIG_BAR0 0x10
#define PCI_CONFIG_PRIMARY_BUS 0x18

typedef enum {
    PCI_HEADER_TYPE_ENDPOINT = 0,
    PCI_HEADER_TYPE_BRIDGE = 1
} pci_header_t;

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint8_t  header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar_size[MAX_BARS]; /* Endpoints have up to 6 BARs; Bridges have 2 */
    uint32_t bar_address[MAX_BARS];
} pci_device_info_t;

typedef struct {
    pci_device_info_t device_info[MAX_PCI_DEVICES];
    uint32_t          device_count;
} pci_device_database_t;

void pcie_alloc(void);
void pcie_scan(uint8_t bus);
void pcie_write_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
void pcie_alloc_irq_vectors(uint8_t bus, uint8_t dev, uint8_t func, pci_device_info_t *dev_info);

uint8_t  pcie_find_capability(uint8_t bus, uint8_t dev, uint8_t func, uint8_t cap_id);
uint32_t pcie_read_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

pci_device_database_t *get_pci_device_database(void);
