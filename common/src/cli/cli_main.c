
/**
 * @file cli_main.c
 * @brief CLI core implementation.
 */

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "arch_ops.h"
#include "block_id.h"
#include "cli.h"
#define EMBEDDED_CLI_IMPL
#include "embedded_cli.h"
#include "log.h"
#include "log_marker.h"
#include "term_codes.h"
#include "utils.h"

// prototypes without include file
void show_main_menu(EmbeddedCli *cli);
void show_main_sys_menu(EmbeddedCli *cli);
void show_test_select_menu(EmbeddedCli *cli);
void show_test_menu(EmbeddedCli *cli);
void set_system_commands(EmbeddedCli *cli);
void set_test_commands(EmbeddedCli *cli);
void on_test_menu(EmbeddedCli *cli, char *args, void *context);
void on_enter_tests(EmbeddedCli *cli, char *args, void *context);
void on_system_menu(EmbeddedCli *cli, char *args, void *context);
void cli_log_cmd_init(void);

// Logging Macros for CLI specific System-Level Messages
#define LOG_CLI_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_CLI), __VA_ARGS__)
#define LOG_CLI_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_CLI), __VA_ARGS__)
#define LOG_CLI_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_CLI), __VA_ARGS__)
#define LOG_CLI_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_CLI), __VA_ARGS__)
#define LOG_CLI_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_CLI), __VA_ARGS__)

// -- Globals --

bool keep_running = true;

cli_data_t cli_data = {
    .msg_buf = {0},
    .mode = MAIN,
    .test = MAX_BLOCK_INDEX,
    .write_enable = true,
    .no_error = false,
};

#ifdef ENABLE_TEST
#define CLI_MAX_BINDINGS (MAX_BINDINGS / 2)
#define CLI_MEM_SIZE 4096
#else
#define CLI_MAX_BINDINGS 10
#define CLI_MEM_SIZE 1024
#endif
static alignas(8) uint64_t cli_mem[CLI_MEM_SIZE / sizeof(uint64_t)];
static struct termios orig_termios;

// -- End of globals --

void set_msg(const char *msg)
{
    snprintf(cli_data.msg_buf, sizeof(cli_data.msg_buf), "%s", msg);
}

void print_msg(EmbeddedCli *cli)
{
    if (cli_data.msg_buf[0] != '\0') {
        embeddedCliPrint(cli, "");
        embeddedCliPrint(cli, cli_data.msg_buf);
        cli_data.msg_buf[0] = '\0';
    }
}

void clear_msg(void)
{
    cli_data.msg_buf[0] = '\0';
    cli_data.no_error = true;
}

void on_back(EmbeddedCli *cli, char *args, void *context)
{
    (void)args;
    (void)context;
    cli_data.no_error = true;
    cli_data.write_enable = true;

#ifdef ENABLE_TEST
    if (cli_data.mode == TEST_DEV && cli_data.test != MAX_BLOCK_INDEX) {
        cli_data.test = MAX_BLOCK_INDEX;
        show_test_menu(cli);
    } else if (cli_data.mode == TEST_SYS && cli_data.test != MAX_INFRA_INDEX) {
        cli_data.test = MAX_INFRA_INDEX;
        show_test_menu(cli);
    } else if (cli_data.mode == TEST_DEV || cli_data.mode == TEST_SYS) {
        cli_data.mode = TEST;
        show_test_select_menu(cli);
    } else
#endif
    {
        cli_data.mode = MAIN;
        show_main_menu(cli);
    }
}

void on_quit(EmbeddedCli *cli, char *args, void *context)
{
    (void)cli;
    (void)args;
    (void)context;
    cli_data.write_enable = false; // disable further CLI output as we are exiting
    cli_data.no_error = true;
    keep_running = false;
    printf("\r" TERM_CURSOR_UP TERM_CLEAR_LINE TERM_CURSOR_UP TERM_CLEAR_LINE TERM_CURSOR_UP TERM_CLEAR_LINE
           "Terminated\n");
    fflush(stdout);
    exit(0); // trigger atexit(reset_terminal_mode)
}

void set_main_commands(EmbeddedCli *cli)
{
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "quit",
            .help = "Exit",
            .binding = on_quit, // on quiting
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "q",
            .help = "Alias for quit",
            .binding = on_quit, //  on quiting
        }
    );
#ifdef ENABLE_TEST
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "test",
            .help = "Enter the Test menu",
            .binding = on_test_menu, // on entering test menu
        }
    );
#endif
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "system",
            .help = "Enter the System menu",
            .binding = on_system_menu, // on entering system menu
        }
    );
}

void show_main_menu(EmbeddedCli *cli)
{
    cli_clear_menu_region();
    const char *msg = "\nAvailable Modes:\r\n"
#ifdef ENABLE_TEST
                      " test - Enter Test Menu\r\n"
#endif
                      " system - Enter System Menu\r\n"
                      "\nUsage: <name> (with TAB), 'quit' or 'q' to exit";

    embeddedCliPrint(cli, msg);
    print_msg(cli);
}

void writeChar(EmbeddedCli *cli, char c)
{
    (void)cli;
    if (cli_data.write_enable) {
        putchar(c);
        fflush(stdout);
    }
}

void reset_terminal_mode(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode(void)
{
    struct termios cli_termios;
    // Save original settings
    tcgetattr(STDIN_FILENO, &orig_termios);

    // Register the reset function to run automatically on exit/crash
    atexit(reset_terminal_mode);

    cli_termios = orig_termios;
    cli_termios.c_lflag &= ~(ICANON | ECHO);
    cli_termios.c_cc[VMIN] = 1;
    cli_termios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &cli_termios);
}

void signal_handler(int sig)
{
    printf("\r\n" UI_COLOR_YELLOW "[SIGNAL] Received %d. Entering Halt State." UI_STYLE_RESET "\r\n", sig);
#ifdef BARE_METAL
    // Reset terminal to be nice to the UART console
    // reset_terminal_mode();
    printf("\r\n[HALT] System locked. Waiting for power-on reset (POR)...\r\n");
    while (true) {
        HALT_CPU();
    }
#else
    exit(0);
#endif
}

const char *entity_name(uint8_t entity, bool cap)
{
    switch (entity) {
    case ENTITY_SIM:
        return cap ? "SIM" : "sim";
    case ENTITY_CLI:
        return cap ? "CLI" : "cli";
    case ENTITY_LOG:
        return cap ? "LOG" : "log";
    case ENTITY_GPIO:
        return cap ? "GPIO" : "gpio";
    case ENTITY_SYSCTRL:
        return cap ? "SYSCTRL" : "sysctrl";
    case ENTITY_TIMER:
        return cap ? "TIMER" : "timer";
    case ENTITY_UART:
        return cap ? "UART" : "uart";
    default:
        return "";
    }
}

const char *domain_name(uint8_t domain, bool cap)
{
    switch (domain) {
    case DOMAIN_DEV:
        return cap ? "DEV" : "dev";
    case DOMAIN_SYS:
        return cap ? "SYS" : "sys";
    case DOMAIN_TEST:
        return cap ? "TEST" : "test";
    default:
        return "";
    }
}

const char *level_name(uint8_t level, bool cap)
{
    switch (level) {
    case LOG_LEVEL_NONE:
        return cap ? "NONE" : "none";
    case LOG_LEVEL_CRITICAL:
        return cap ? "CRITICAL" : "critical";
    case LOG_LEVEL_ERROR:
        return cap ? "ERROR" : "error";
    case LOG_LEVEL_WARNING:
        return cap ? "WARN" : "warn";
    case LOG_LEVEL_INFO:
        return cap ? "INFO" : "info";
    case LOG_LEVEL_DEBUG:
        return cap ? "DEBUG" : "debug";
    default:
        return "";
    }
}

// log writer callback
void log_formatter(uint8_t domain, uint8_t entity, uint8_t level, uint64_t ts, const char *log_msg, uint16_t len)
{
    (void)len; // unused

    uint32_t us = (uint32_t)(ts % 1000000), tsec = (uint32_t)(ts / 1000000);
    uint32_t sec = tsec % 60, min = (tsec / 60) % 60, hour = (tsec / 3600) % 24, days = tsec / 86400;
    // clang-format off
    const char *color = (domain == DOMAIN_SYS) ? UI_COLOR_YELLOW :
                        (domain == DOMAIN_DEV) ? UI_COLOR_GREEN :
                                                 UI_COLOR_CYAN;
    // clang-format on
    int target_line = (domain == DOMAIN_TEST) ? LOG_REGION_START : (LOG_REGION_START + LOG_REGION_OFFSET);

    printf(TERM_CURSOR_SAVE);
    printf(TERM_RESET_SCROLL_REGION);
    printf("\033[%d;%dr", LOG_REGION_START, LOG_REGION_END);
    printf("\033[%d;1H", target_line);                 // move cursor to the target line
    printf(TERM_INSERT_LINE);                          // insert a blank line and push others down
    printf(TERM_TRUNCATION_SAFETY_ON TERM_CLEAR_LINE); // no-wrap for long lines + clear line
    printf("\033[2m[%03u:%02u:%02u:%02u.%06u]\033[0m", days, hour, min, sec, us);
    printf(
        "%s[%s%s%s]\033[0m[%s] %s", color, domain_name(domain, 1), entity ? ":" : "", entity_name(entity, 1),
        level_name(level, 1), log_msg
    );
    printf(TERM_TRUNCATION_SAFETY_OFF);
    printf(TERM_SET_SCROLL_REGION(MENU_REGION_START, MENU_REGION_END));
    printf(TERM_CURSOR_RESTORE);
    fflush(stdout);
}

void process_logs(void)
{
    // If the UI is currently "muted" for a refresh, wait to flush logs
    if (!cli_data.write_enable || !log_is_dirty())
        return;

    log_flash(log_formatter);
}

void cli_clear_menu_region(void)
{
    printf("\033[%d;1H", MENU_REGION_START);
    printf("\033[J"); // Clear everything from MENU_REGION_START to the bottom
    fflush(stdout);
}

void cli_setup_screen(void)
{
    printf(TERM_RESET_SCROLL_REGION);
    printf(TERM_CLEAR_SCREEN);
    printf(TERM_SET_SCROLL_REGION(LOG_REGION_START, LOG_REGION_END));
    printf(TERM_SET_CURSOR_AT_LINE(MENU_REGION_START));
    fflush(stdout);
}

// Initialize CLI, set up commands and screen
int cli_init(void **cli_ctx)
{
    EmbeddedCliConfig *config = embeddedCliDefaultConfig();

    config->maxBindingCount = CLI_MAX_BINDINGS;
    config->enableAutoComplete = false;
    config->cliBuffer = cli_mem;
    config->cliBufferSize = CLI_MEM_SIZE;

    uint16_t requiredCliBufferSize = embeddedCliRequiredSize(config);

    printf("[INFO] CLI buffer allocated " STR(CLI_MEM_SIZE) " required %u bytes", requiredCliBufferSize);
    EmbeddedCli *cli = embeddedCliNew(config);

    if (cli == nullptr) {
        printf("%s: %d: [ERROR] embeddedCliNew: returned null\n", __func__, __LINE__);
        return -1;
    }

    signal(SIGTERM, signal_handler);
    set_conio_terminal_mode();
    cli_setup_screen();
    set_test_commands(cli);
    set_system_commands(cli);
    set_main_commands(cli);
    cli_log_cmd_init();
    log_set_level(DOMAIN_SYS, ENTITY_CLI, LOG_LEVEL_ERROR);

    cli->writeChar = writeChar;
    cli_data.write_enable = false;
    // Clear CLI initial state with fake 'Enter' pressed
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);
    // CLI can start writing
    cli_data.write_enable = true;
    show_main_menu(cli);
    // write back the CLI context
    *cli_ctx = cli;
    return 0;
}

// CLI Test Mode runner
void cli_run_test(EmbeddedCli *cli)
{
#ifdef ENABLE_TEST
    int        prev_block = cli_data.test;
    cli_mode_e prev_mode = (cli_mode_e)cli_data.mode;

    cli_data.no_error = false;
    cli_data.write_enable = false;
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);
    cli_data.write_enable = true;

    // If mode and block haven't changed, the user likely typed an invalid command
    if (prev_mode == cli_data.mode && prev_block == cli_data.test) {

        if (!cli_data.no_error && cli_data.msg_buf[0] == '\0') {
            set_msg("[ERROR] Invalid selection");
        }

        if (cli_data.mode == TEST) {
            show_test_select_menu(cli);
        } else if (cli_data.test == get_max_index()) {
            show_test_menu(cli);
        } else {
            on_enter_tests(cli, NULL, (void *)(uintptr_t)cli_data.test);
        }
    }
#endif
}

bool cli_run(void *cli_ctx)
{
    EmbeddedCli *cli = (EmbeddedCli *)cli_ctx;

    process_logs();
    if (!stdin_ready(20))
        return true;
    char c = (char)getc(stdin);
    if (c == EOF)
        return false;

    cli_data.write_enable = true;
    if (c == '\t') {
        LOG_CLI_DEBUG("[TAB] press");
        embeddedCliReceiveChar(cli, c);
        embeddedCliProcess(cli);
        fflush(stdout);
        LOG_CLI_DEBUG("[TAB] handling done");
    } else if (c == '\r' || c == '\n') {
        LOG_CLI_DEBUG("[ENTER] press");
        if (cli_data.mode == TEST || cli_data.mode == TEST_DEV || cli_data.mode == TEST_SYS) {
            cli_run_test(cli);
        } else {
            embeddedCliReceiveChar(cli, '\r');
            embeddedCliProcess(cli);
        }
        LOG_CLI_DEBUG("[ENTER] handling done");
    } else if (c == 127 || c == '\b') {
        LOG_CLI_DEBUG("[BACKSPACE] press");
        embeddedCliReceiveChar(cli, '\b');
        embeddedCliProcess(cli);
        LOG_CLI_DEBUG("[BACKSPACE] handling done");
    } else {
        embeddedCliReceiveChar(cli, c);
        embeddedCliProcess(cli);
    }
    return true;
}

void cli_exit(void *cli_ctx)
{
    embeddedCliFree((EmbeddedCli *)cli_ctx);
}
