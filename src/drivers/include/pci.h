
#pragma once

#define MAX_PCI_DEVICES 32
#define MAX_PCI_BRIDGES 2
#define MAX_FUNCTIONS 8
#define MAX_BARS 6

#define PCI_CONFIG_VENDOR_ID 0x00
#define PCI_CONFIG_COMMAND 0x04
#define PCI_CONFIG_HEADER_TYPE 0x0C
#define PCI_CONFIG_BAR0 0x10
#define PCI_CONFIG_PRIMARY_BUS 0x18

#define PCI_COMMAND_IO 0x01     // Enable response for I/O space
#define PCI_COMMAND_MEMORY 0x02 // Enable response for Memory space
#define PCI_COMMAND_MASTER 0x04 // Enable Bus Mastering (DMA)

#define PCI_MEM_TYPE_MASK 0x06 // Bits 1-2: Memory type sizing
#define PCI_MEM_TYPE_64 0x04   // Type indicator for 64-bit BARs

#define LOG_PCI_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_PCI), __VA_ARGS__)
#define LOG_PCI_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_PCI), __VA_ARGS__)

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
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint8_t  header_type;
    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t primary_bus;     // Bus number upstream of the bridge (closer to CPU)
    uint8_t secondary_bus;   // Bus number directly managed downstream by this bridge
    uint8_t subordinate_bus; // Highest bus number reachable behind this bridge

    uint32_t mem_base;  // Starting physical address of mapped MMIO window
    uint32_t mem_limit; // Ending physical address of mapped MMIO window
} pci_bridge_info_t;

typedef struct {
    uint16_t          flags;
    uint16_t          device_count;
    uint16_t          bridge_count;
    pci_device_info_t device_info[MAX_PCI_DEVICES];
    pci_bridge_info_t bridge_info[MAX_PCI_BRIDGES];
} pci_database_t;

void pci_alloc(void);
void pci_scan(uint8_t bus);
void pci_alloc_irq_vectors(pci_device_info_t *dev_info);
int  pci_write_config_word(pci_device_info_t *pci_dev, uint16_t addr, uint16_t value);
int  pci_write_config_dword(pci_device_info_t *pci_dev, uint16_t addr, uint32_t value);
int  pcib_write_config_word(pci_bridge_info_t *pci_bridge, uint16_t addr, uint16_t value);
int  pcib_write_config_dword(pci_bridge_info_t *pci_bridge, uint16_t addr, uint32_t value);

uint16_t pci_read_config_word(pci_device_info_t *pci_dev, uint16_t addr);
uint32_t pci_read_config_dword(pci_device_info_t *pci_dev, uint16_t addr);
uint16_t pcib_read_config_word(pci_bridge_info_t *pci_bridge, uint16_t addr);
uint32_t pcib_read_config_dword(pci_bridge_info_t *pci_bridge, uint16_t addr);

pci_database_t    *get_pci_database(void);
pci_device_info_t *pci_find_by_id(uint16_t vendor_id, uint16_t device_id);
pci_device_info_t *pci_find_by_bdf(uint8_t bus, uint8_t dev, uint8_t func);

void *pci_ioremap_bar(pci_device_info_t *pci_dev, int bar);

int  pci_enable_device(pci_device_info_t *dev);
void pci_set_master(pci_device_info_t *dev);

void pci_config_device_resources(pci_device_info_t *dev);
void pci_dump_device_database(void);
