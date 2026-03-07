/**
 * @file cli_test.c
 * @brief CLI SoC Test Runner
 * Provides a command-line interface for executing SoC tests organized into
 * blocks. Users can navigate through test blocks, select and run individual
 * tests, and view results.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "embedded_cli.h"
#include "block_id.h"
#include "cli.h"
#include "term_codes.h"
#include "test_common.h"
#include "log_marker.h"
#include "test_registry.h"
#include "utils.h"

// prototypes without include file
void clear_msg(void);
void set_msg(const char *msg);
void print_msg(EmbeddedCli *cli);

// -- Globals --

const int max_index_sys = MAX_INFRA_INDEX;
const int max_index_dev = MAX_BLOCK_INDEX;

// For showing numeric indexes
static const char *const index_str[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
    // Extend as needed
};

// -- End of globals --

void show_test_menu(EmbeddedCli *cli)
{
    cli_clear_menu_region();

    embeddedCliPrint(cli, (cli_data.mode == TEST_SYS) ? "\nSystem Infra Blocks:" : "\nDevices:");

    for (int i = 0; i < cli_data.test; i++) {
        char line[128];
        snprintf(
            line, sizeof(line), "  %d. %s %s", i + 1, cli_data.registry[i].name,
            cli_data.registry[i].enabled ? "" : "[DISABLED]"
        );
        embeddedCliPrint(cli, line);
    }
    embeddedCliPrint(cli, "\nUsage: <name> (with TAB) or <number>, 'quit' or 'q', 'back' to previous menu");
    print_msg(cli);
}

void on_execute_test(EmbeddedCli *cli, char *args, void *context)
{
    cli_data.no_error = true;
    const int max_index = get_max_index();

    if (cli_data.test >= max_index) {
        set_msg("[ERROR] Block index out of range");
        return;
    }

    // context is used here to pass the specific test_desc_t pointer
    test_desc_t *test = (test_desc_t *)context;

    bool test_belongs_to_block = false;
    if (cli_data.test < max_index) {
        for (int i = 0; i < cli_data.registry[cli_data.test].count; i++) {
            if (&cli_data.registry[cli_data.test].tests[i] == test) {
                test_belongs_to_block = true;
                break;
            }
        }
    }

    cli_data.write_enable = true;
    putchar('\n'); // ensure test output starts on its own line

    if (!test_belongs_to_block) {
        set_msg("[ERROR] This test is not available in the current block");
        return;
    }

    if (!test->enabled) {
        set_msg("Test disabled");
        return;
    }

    if (test->func == nullptr) {
        set_msg("[ERROR] Test not configured");
        return;
    }
    embeddedCliPrint(cli, "Running...");
    int result = test->func(args);
    snprintf(cli_data.msg_buf, sizeof(cli_data.msg_buf), "Test Finished. Result: %s", (result == 0) ? "PASS" : "FAIL");
}

void on_enter_tests(EmbeddedCli *cli, char *args, void *context)
{
    (void)args;
    const uintptr_t index = (uintptr_t)context;
    const int       max_index = get_max_index();

    if (index >= max_index) {
        set_msg("[ERROR] Invalid selection");
        return;
    }

    const test_registry_t *block = &cli_data.registry[index];

    cli_data.write_enable = true;
    if (!block->enabled) {
        set_msg("[ERROR] Block disabled");
        return;
    }

    cli_data.no_error = true;
    cli_data.test = (int)index; // change state

    // Clear the screen and print the clean menu
    cli_clear_menu_region();

    char header[64];
    snprintf(header, sizeof(header), "\n=== %s Tests ===", block->name);
    embeddedCliPrint(cli, header);

    for (int i = 0; i < block->count; i++) {
        char line[128];
        snprintf(line, sizeof(line), "  %d. %s", i + 1, block->tests[i].name);
        embeddedCliPrint(cli, line);
    }
    embeddedCliPrint(cli, "\nUsage: <name> (with TAB) or <number>, 'quit' or 'q', 'back' to previous menu");
    print_msg(cli);
}

// Dispatcher for numeric selections
void on_number_selected(EmbeddedCli *cli, char *args, void *context)
{
    const uintptr_t index = (uintptr_t)context;
    const int       max_index = get_max_index();

    // Check if we are at the top level of the sub-menu (Block Selection)
    // Assuming MAX_BLOCK_INDEX and MAX_INFRA_INDEX are used as the "at-menu" sentinel
    if (cli_data.test == max_index) {
        if (index < max_index) {
            on_enter_tests(cli, args, (void *)index);
        } else {
            set_msg("[ERROR] Invalid selection");
        }
    } else {
        // We are inside a block (Executing a test)
        const test_registry_t *block = &cli_data.registry[cli_data.test];
        if (index < (uintptr_t)block->count) {
            on_execute_test(cli, args, (void *)&block->tests[index]);
        } else {
            set_msg("[ERROR] Invalid selection");
        }
    }
}

void on_dev_test_menu(EmbeddedCli *cli, char *args, void *context)
{
    (void)args;
    (void)context;
    clear_msg();
    cli_data.write_enable = true;
    cli_data.mode = TEST_DEV;
    cli_data.test = MAX_BLOCK_INDEX;
    cli_data.registry = dev_test_registry;
    show_test_menu(cli);
}

void on_sys_test_menu(EmbeddedCli *cli, char *args, void *context)
{
    (void)args;
    (void)context;
    clear_msg();
    cli_data.write_enable = true;
    cli_data.mode = TEST_SYS;
    cli_data.test = MAX_INFRA_INDEX;
    cli_data.registry = sys_test_registry;
    show_test_menu(cli);
}

static void set_test_registry_bindings(EmbeddedCli *cli, const test_registry_t *reg, int count)
{
    for (int i = 0; i < count; i++) {
        embeddedCliAddBinding(
            cli, (CliCommandBinding){.name = reg[i].name, .context = (void *)(uintptr_t)i, .binding = on_enter_tests}
        );
        for (int j = 0; j < reg[i].count; j++) {
            embeddedCliAddBinding(
                cli, (CliCommandBinding){.name = reg[i].tests[j].name, .context = (void *)&reg[i].tests[j], .binding = on_execute_test}
            );
        }
    }
}

void show_test_select_menu(EmbeddedCli *cli)
{
    cli_clear_menu_region();
    // clang-format off
    const char *msg =
        "\nTest Groups:\r\n"
        " dev - Enter Device Test Menu\r\n"
        " sys - Enter System Test Menu\r\n"
        "\nUsage: <name> (with TAB), 'quit' or 'q', 'back' to previous menu";
    // clang-format on
    embeddedCliPrint(cli, msg);
    print_msg(cli);
}

void on_test_menu(EmbeddedCli *cli, char *args, void *context)
{
    (void)args;
    (void)context;
    cli_data.write_enable = true;
    cli_data.mode = TEST;
    cli_data.test = -1;
    show_test_select_menu(cli);
}

void set_test_commands(EmbeddedCli *cli)
{
    embeddedCliAddBinding(cli, (CliCommandBinding){"test", "Test Menu", true, NULL, on_test_menu});
    embeddedCliAddBinding(cli, (CliCommandBinding){"dev", "Enter Device Tests", true, NULL, on_dev_test_menu});
    embeddedCliAddBinding(cli, (CliCommandBinding){"sys", "Enter System Tests", true, NULL, on_sys_test_menu});

    set_test_registry_bindings(cli, dev_test_registry, MAX_BLOCK_INDEX);
    set_test_registry_bindings(cli, sys_test_registry, MAX_INFRA_INDEX);
    // clang-format off
    const size_t count = MAX2(MAX2(MAX_INFRA_INDEX, sys_max_tests),
                              MAX2(MAX_BLOCK_INDEX, dev_max_tests));
    // clang-format on
    assert(
        count <= sizeof(index_str) / sizeof(index_str[0]) &&
        "[ERROR] Insufficient index strings. Please extend index_str array."
    );

    // register each index as a stringified number
    for (unsigned int i = 0; i < count; i++) {
        embeddedCliAddBinding(
            cli,
            (CliCommandBinding){
                .name = index_str[i],
                .help = "Index selection",
                .tokenizeArgs = true,
                .context = (void *)(uintptr_t)i, // pass the raw index
                .binding = on_number_selected,   // use the dispatcher
            }
        );
    }
}
