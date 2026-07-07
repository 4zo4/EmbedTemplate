#include <stdint.h>

#include "test_common.h"
#include "test_pci.h"

#ifdef ENABLE_PCI

const int PCI_num_tests = PCI_TEST_NUM;
int       test_pci_init(char *args)
{
    // Initialization code for PCI tests
    (void)args;
    return 0; // Return 0 on success
}

test_desc_t PCI_tests[] = {
    {"PCI_Init", test_pci_init, true},
};

#else // !ENABLE_PCI

const int   PCI_num_tests = 0;
test_desc_t PCI_tests[] = {{0}};

#endif // ENABLE_PCI
