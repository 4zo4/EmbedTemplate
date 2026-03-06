#include <stdint.h>

#include "log.h"
#include "log_marker.h"
#include "test_common.h"
#include "test_cli.h"

int test_cli(char *args)
{
    (void)args;

    LOG_CLI_TEST_INFO("Test CLI");

    return 0; // Return 0 on success
}

test_desc_t CLI_tests[] = {
    {"CLI_test", test_cli, true},
};

static_assert(sizeof(CLI_tests) / sizeof(CLI_tests[0]) == CLI_TEST_NUM, "CLI tests out of range");
