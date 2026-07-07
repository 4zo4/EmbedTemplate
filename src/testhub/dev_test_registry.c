#include "block_id.h"
#include "test_common.h"
#include "test_registry.h"
#include "test_registry_priv.h"
#include "test_gpio.h"
#include "test_pci.h"
#include "test_sysctrl.h"
#include "test_timer.h"
#include "test_uart.h"
#include "utils.h"

// sorted by device/block indices
// clang-format off
alignas(8) test_registry_t dev_test_registry[] = {
    // Index      Name       Test Array     Num Tests           Max Test Count    Enabled
    [GPIO]    = { "GPIO",    GPIO_tests,    &GPIO_num_tests,    GPIO_TEST_NUM,    true },
    [PCI]     = { "PCI",     PCI_tests,     &PCI_num_tests,     PCI_TEST_NUM,     false },
    [SYSCTRL] = { "SYSCTRL", SYSCTRL_tests, &SYSCTRL_num_tests, SYSCTRL_TEST_NUM, true },
    [TIMER]   = { "TIMER",   TIMER_tests,   &TIMER_num_tests,   TIMER_TEST_NUM,   false },
    [UART]    = { "UART",    UART_tests,    &UART_num_tests,    UART_TEST_NUM,    true  },
};
// clang-format on

static_assert(
    sizeof(dev_test_registry) / sizeof(dev_test_registry[0]) == NUM_BLOCKS, "Device test registry out of range"
);

enum {
    MAX_TESTS = MAX_NUM(GPIO_TEST_NUM, PCI_TEST_NUM, SYSCTRL_TEST_NUM, TIMER_TEST_NUM, UART_TEST_NUM)
};
const int dev_max_tests = MAX_TESTS; // block with maximum number of tests
