#include "log_marker.h"
#include "test_common.h"
#include "test_registry.h"
#include "test_registry_priv.h"
#include "test_cli.h"
#include "test_log.h"
#include "utils.h"

const test_registry_t sys_test_registry[] = {
    [CLI] = {"CLI", CLI_tests, CLI_TEST_NUM, true},
    [LOG] = {"LOG", LOG_tests, LOG_TEST_NUM, true},
};

static_assert(
    sizeof(sys_test_registry) / sizeof(sys_test_registry[0]) == NUM_SYS_BLOCKS, "System test registry out of range"
);

enum {
    MAX_TESTS = MAX_NUM(CLI_TEST_NUM, LOG_TEST_NUM)
};
const int sys_max_tests = MAX_TESTS;