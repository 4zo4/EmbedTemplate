#include <stdint.h>

#include "embedded_cli.h"
#include "log.h"
#include "log_marker.h"
#include "test_common.h"
#include "test_cli.h"

typedef struct cli_test_list_s {
    const char **cmd_list;
    uint16_t     num;
    uint16_t     next;
    bool         run;
} cli_test_list_t;

// prototypes without include file
EmbeddedCli *get_cli_ctx(void);

const char *cmd_list[] = {
    "set log all all info",
    "show ?",
    "show config",
    "show config log",
#ifdef ENABLE_RTOS
    "show config sim ?",
    "show config sim ranges",
#endif
    "set log all all none",
    "set log sys cli error",
#ifdef ENABLE_RTOS
    "set sim phy 1 2 3 4 5 ?",
    "set sim phy 1 2 3 4 5 6", // should error
#endif
    "set log zzz ?",           // should error
    "set log test cli info",
};

cli_test_list_t cli_test = {
    // clang-format off
    .cmd_list = cmd_list,
    .num = (int)(sizeof(cmd_list) / sizeof(cmd_list[0])),
    .next = 0,
    .run = false,
    // clang-format on
};

static void test_cli_inject_command(EmbeddedCli *cli, const char *cmd)
{
    for (int i = 0; cmd[i] != '\0'; i++) {
        embeddedCliReceiveChar(cli, cmd[i]);
        embeddedCliProcess(cli);
    }
}

/**
 * @brief run CLI tests
 */
void run_cli_tests(EmbeddedCli *cli)
{
    if (cli_test.run) {
        while (cli_test.next < cli_test.num) {
            test_cli_inject_command(cli, cli_test.cmd_list[cli_test.next++]);
            embeddedCliReceiveChar(cli, '\r');
            embeddedCliProcess(cli);
        }
        cli_test.run = false;
        cli_test.next = 0;
        LOG_CLI_TEST_INFO("Test CLI done");
    }
}

int test_cli(char *args)
{
    (void)args;

    LOG_CLI_TEST_INFO("Test CLI");

    cli_test.run = true;

    return 0;
}

test_desc_t CLI_tests[] = {
    {"CLI_test", test_cli, true},
    {"CLI_test2", nullptr, true},
    {"CLI_test3", nullptr, true},
    {"CLI_test4", nullptr, true},
    {"CLI_test5", nullptr, true},
    {"CLI_test6", nullptr, true},
    {"CLI_test7", nullptr, true},
    {"CLI_test8", nullptr, true},
    {"CLI_test9", nullptr, true},
    {"CLI_test10", nullptr, true},
    {"CLI_test11", nullptr, true},
    {"CLI_test12", nullptr, true},
    {"CLI_test13", nullptr, true},
    {"CLI_test14", nullptr, true},
    {"CLI_test15", nullptr, true},
    {"CLI_test16", nullptr, true},
    {"CLI_test17", nullptr, true},
    {"CLI_test18", nullptr, true},
};

static_assert(sizeof(CLI_tests) / sizeof(CLI_tests[0]) == CLI_TEST_NUM, "CLI tests out of range");
