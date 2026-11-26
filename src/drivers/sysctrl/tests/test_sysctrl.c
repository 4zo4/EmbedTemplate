#include <stdint.h>

#include "log.h"
#include "log_marker.h"
#include "test_common.h"
#include "test_sysctrl.h"

#ifdef ENABLE_SYSCTRL

int test_sysctrl_init(char *args)
{
    // Initialization code for SYSCTRL tests
    (void)args;
    return 0; // Return 0 on success
}

int test_sysctrl_functionality(char *args)
{
    // Code to test SYSCTRL functionality
    (void)args;
    return 0; // Return 0 on success
}

int test_sysctrl_mmio(char *args)
{
    (void)args;
    volatile uint32_t *mmio = (volatile uint32_t *)0x90000000;
    mmio[0] = 0x12345678;
    mmio[1] = 0xCAFEBABE;
    uint32_t r0 = mmio[0];
    uint32_t r1 = mmio[1];
    LOG_SYSCTRL_TEST_INFO("MMIO readback: 0x%08x, 0x%08x", r0, r1);
    return 0; // Return 0 on success
}

const int   SYSCTRL_num_tests = SYSCTRL_TEST_NUM;
test_desc_t SYSCTRL_tests[] = {
    {"SYSCTRL_Init", test_sysctrl_init,          true },
    {"SYSCTRL_Func", test_sysctrl_functionality, false},
    {"SYSCTRL_MMIO", test_sysctrl_mmio,          true }
};

static_assert(sizeof(SYSCTRL_tests) / sizeof(SYSCTRL_tests[0]) == SYSCTRL_TEST_NUM, "SYSCTRL tests out of range");

#else // !ENABLE_SYSCTRL

const int   SYSCTRL_num_tests = 0;
test_desc_t SYSCTRL_tests[] = {{0}};

#endif // ENABLE_SYSCTRL
