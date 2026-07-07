#include <stdint.h>

#include "log.h"
#include "log_marker.h"
#include "test_common.h"
#include "test_log.h"

#ifdef ENABLE_LOG

int test_log(char *args)
{
    (void)args;

    LOG_TEST_INFO("Test LOG");
    // clang-format off
    for (int i = 0; i < 120; i++)
        LOG_TEST_INFO("Msg %d: %s", i, (i % 2) ? "Short log" :
            "A long log message to force log wraps and again ...\n"
            "a long log message to force log wraps and again ...\n"
            "a long log message to force log wraps and again ...\n"
            "a long log message to force log wraps and again ...\n"
            "a long log message to force log wraps and again ...\n"
            "a long log message to force log wraps and again ...\n"
            "a long log message to force log wraps and again ...\n"
            "a long log message to force log wraps and again ...\n"
            "a long log message to force log wraps");
    // clang-format on
    return 0;
}

const int   LOG_num_tests = LOG_TEST_NUM;
test_desc_t LOG_tests[] = {
    {"LOG_test", test_log, true},
};

static_assert(sizeof(LOG_tests) / sizeof(LOG_tests[0]) == LOG_TEST_NUM, "LOG tests out of range");

#else // !ENABLE_LOG

const int   LOG_num_tests = 0;
test_desc_t LOG_tests[] = {{0}};

#endif // ENABLE_LOG
