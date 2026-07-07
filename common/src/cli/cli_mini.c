
/**
 * @file cli_mini.c
 * @brief Mini CLI implementation.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "arch_ops.h"
#include "block_id.h"
#include "log.h"
#include "log_marker.h"
#include "term_codes.h"
#include "utils.h"

// prototypes without include file
bool stdin_ready(int timeout_ms);

// guard macro substitutions for standard i/o functions
#undef getchar
#undef putchar

#ifdef BARE_METAL
#ifndef VMIN
#define VMIN 16
#endif
#ifndef VTIME
#define VTIME 17
#endif
#endif

#define MAX_LINE 12
#define NULL_TOKEN 0xFF
#define PROMPT '>'
#define CHAR_SPACE 32
#define CHAR_TILDE 126
#define CHAR_BACKSPACE 127

// -- Globals --

bool volatile keep_running = true;
static struct termios orig_termios;
static char           rxBuf[MAX_LINE];
static uint8_t        rxCount;
static uint32_t       rxBitmap[4];

// -- End of globals --

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

const char *entity_name(uint8_t entity, bool cap)
{
    switch (entity) {
    case ENTITY_NONE:
        return cap ? "" : "none";
    case ENTITY_SIM:
        return cap ? "SIM" : "sim";
    case ENTITY_CLI:
        return cap ? "CLI" : "cli";
    case ENTITY_LOG:
        return cap ? "LOG" : "log";
    case ENTITY_GPIO:
        return cap ? "GPIO" : "gpio";
    case ENTITY_PCI:
        return cap ? "PCI" : "pci";
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

static void process_logs(void)
{
    if (!log_is_dirty())
        return;

    log_flash(log_formatter);
}

static void show_menu(void)
{
    cli_clear_menu_region();
    printf("\r\n" UI_COLOR_YELLOW "Runner" UI_STYLE_RESET "...\r\n");
    printf("Usage: <cmd>, 'q' to exit, '?' for help\r\n");
    putchar(PROMPT);
    putchar(' ');
}

static int cli_clear_last_char(void)
{
    if (rxCount) {
        rxCount--;
        rxBitmap[rxCount / 32] &= ~BIT(rxCount & 31);
        putchar('\b');
        putchar(' ');
        putchar('\b');
        fflush(stdout);
        return 0;
    }
    return -1;
}

static int cli_receive_char(const char c)
{
    if (rxCount < MAX_LINE) {
        if (c == CHAR_SPACE)
            rxBitmap[rxCount / 32] |= BIT(rxCount & 31);
        rxBuf[rxCount++] = c;
        putchar(c);
        return 0;
    }
    return -1;
}

static uint32_t cli_tokenize(char *str, int len, uint32_t *bitmap)
{
    uint32_t tkns = 0xFFFFFFFF; // set max of 4 tokens to NULL_TOKEN
    int      num_tkns = 0;
    int      last_space = -1;
    uint8_t *tkn = (uint8_t *)&tkns;

    int token = 0;

    for (int i = 0; i < 4; i++) {
        uint32_t mask = bitmap[i];
        while (mask != 0) {
            int pos = LOWEST_BIT(mask);
            int idx = (i * 32) + pos;

            if (idx >= len) {
                token = len;
                break;
            }

            if (idx > token) {
                str[idx] = '\0';
                tkn[num_tkns++] = token;
            }

            token = idx + 1;
            last_space = idx;
            mask &= (mask - 1);
            if (num_tkns >= 4)
                break;
        }
        if (num_tkns >= 4)
            break;
    }

    if (num_tkns < 4 && token < len)
        tkn[num_tkns] = token;

    return tkns;
}

const char *cmds[] = {
    "set",
    "show"
};
const char *args[] = {
    "logs",
    "stats"
};

static void on_help(int cmd)
{
    switch (cmd) {
    case 0:
        printf("Usage: set [logs|stats]\r\n");
        break;
    case 1:
        printf("Usage: show [logs|stats]\r\n");
        break;
    default:
        printf("Usage: <set|show>\r\n");
        break;
    }
}

static void on_quit(void)
{
    keep_running = false;
    printf("\r" TERM_CURSOR_UP TERM_CLEAR_LINE TERM_CURSOR_UP TERM_CLEAR_LINE TERM_CURSOR_UP TERM_CLEAR_LINE
           "Terminated\r\n");
    fflush(stdout);
    exit(0); // trigger atexit(reset_terminal_mode)
}

static void show_stats_log(void)
{
    log_stats_t stats;
    log_get_stats(&stats);
    char msg[64];
    snprintf(msg, sizeof(msg), "Log Statistics: total logs %lu, dropped %u", stats.sum, stats.drop_cnt);
    printf("%s\r\n", msg);
}

static void cli_process_str(char *str, int len, uint32_t *bitmap)
{
    if (len == 1) {
        switch (str[0]) {
        case 'q':
            on_quit();
            return;
        case '?':
        default:
            on_help(NULL_TOKEN);
            return;
        }
    }

    uint32_t tkns = cli_tokenize(str, len, bitmap);
    uint8_t *tkn = (uint8_t *)&tkns;

    if (tkn[0] == NULL_TOKEN)
        return;
    switch (str[tkn[0]]) {
    case 'q':
        on_quit();
        return;
    case '?':
        on_help(NULL_TOKEN);
        return;
    }
    int cmd = NULL_TOKEN;

    for (int i = 0; i < 2; i++) {
        if (strcmp(str + tkn[0], cmds[i]) == 0) {
            cmd = i;
            break;
        }
    }

    if (str[tkn[1]] == '?') {
        on_help(cmd);
        return;
    }

    switch (cmd) {
    case 0:
        if (tkn[1] < len) {
            if (strcmp(str + tkn[1], args[0]) == 0)
                printf("run set logs...\r\n");
            if (strcmp(str + tkn[1], args[1]) == 0)
                printf("run set stats...\r\n");
        }
        break;
    case 1:
        if (tkn[1] < len) {
            if (strcmp(str + tkn[1], args[0]) == 0)
                show_stats_log();
            if (strcmp(str + tkn[1], args[1]) == 0)
                show_stats_log();
        }
        break;
    }
}

static int cli_process(char c)
{
    if (rxCount) {
        rxBuf[rxCount] = '\0';
        putchar('\r');
        putchar('\n');
        cli_process_str(rxBuf, rxCount, rxBitmap);
        rxCount = 0;
        rxBitmap[0] = 0;
        rxBitmap[1] = 0;
        rxBitmap[2] = 0;
        rxBitmap[3] = 0;
        putchar(PROMPT);
        putchar(' ');
        return 0;
    }
    return -1;
}

// Initialize CLI, set up screen
int cli_init(void **cli_ctx)
{
    set_conio_terminal_mode();
    cli_setup_screen();
    show_menu();
    *cli_ctx = nullptr;
    return 0;
}

bool cli_run(void *cli_ctx)
{
    process_logs();

    if (!stdin_ready(20))
        return true;

    char c = (char)getchar();
    if (c == EOF)
        return false;

    if (c == '\r' || c == '\n') {
        cli_process(c);
    } else if (c == CHAR_BACKSPACE || c == '\b') {
        cli_clear_last_char();
    } else if (c >= CHAR_SPACE && c <= CHAR_TILDE) {
        cli_receive_char(c);
    }

    return keep_running;
}

void cli_exit(void *cli_ctx)
{
    NOP();
}
