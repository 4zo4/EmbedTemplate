
/**
 * @file cli_system.c
 * @brief CLI System Runner
 * Provides a command-line interface for system-level commands.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "block_id.h"
#include "cli.h"
#include "embedded_cli.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h"

// prototypes without include file
void clear_msg(void);
void set_msg(const char *msg);
void print_msg(EmbeddedCli *cli);
void on_back(EmbeddedCli *cli, char *args, void *context);
void sim_start(void);
void sim_suspend(void);
bool sim_pause(void);
void sim_resume(void);

const char *entity_name(uint8_t entity);
const char *domain_name(uint8_t domain);
const char *level_name(uint8_t level);

static uint32_t cliHash(const char *str); // refer to embedded-cli/lib/src/embedded_cli.c

// for name to id matching e.g., "CRITICAL" to LOG_LEVEL_CRITICAL (10)
typedef struct {
    uint32_t hash;
    int      id;
} name_id_t;

// sentinel value to indicate an empty slot in the map
#define MAP_EMPTY 0xFF

#define DOMAIN_MAP_SIZE POW2(MAX_DOMAIN * 2)
#define ENTITY_MAP_SIZE POW2(MAX_ENTITY * 2)
#define LOG_LEVEL_MAP_SIZE POW2(NUM_LOG_LEVELS * 2)

#define DOMAIN_MAP_MASK (DOMAIN_MAP_SIZE - 1)
#define ENTITY_MAP_MASK (ENTITY_MAP_SIZE - 1)
#define LOG_LEVEL_MAP_MASK (LOG_LEVEL_MAP_SIZE - 1)

static_assert((DOMAIN_MAP_SIZE & DOMAIN_MAP_MASK) == 0, "DOMAIN_MAP_SIZE must be power of 2");
static_assert((ENTITY_MAP_SIZE & ENTITY_MAP_MASK) == 0, "ENTITY_MAP_SIZE must be power of 2");
static_assert((LOG_LEVEL_MAP_SIZE & LOG_LEVEL_MAP_MASK) == 0, "LOG_LEVEL_MAP_SIZE must be power of 2");

// -- Globals --

static uint8_t   domain_map[DOMAIN_MAP_SIZE];
static name_id_t domain_tbl[MAX_DOMAIN - 1];
static uint8_t   entity_map[ENTITY_MAP_SIZE];
static name_id_t entity_tbl[MAX_ENTITY];
static uint8_t   level_map[LOG_LEVEL_MAP_SIZE];
static name_id_t level_tbl[NUM_LOG_LEVELS];
static int       level_id[NUM_LOG_LEVELS] = {LOG_LEVEL_NONE, LOG_LEVEL_CRITICAL, LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};

// -- End of globals --

void show_main_sys_menu(EmbeddedCli *cli)
{
    cli_clear_menu_region();
    // clang-format off
    const char *msg =
        "\nSystem Commands:\r\n"
        " set - Set log level\r\n"
        " show - Show status\r\n"
#ifdef ENABLE_RTOS
        " start - Start HW Simulation\r\n"
        " end - End HW Simulation\r\n"
        " pause - Pause HW Simulation\r\n"
        " resume - Resume HW Simulation\r\n"
#endif
        "\nUsage: <name> (with TAB), 'quit' or 'q' to exit, 'back' to previous menu";
    // clang-format on
    embeddedCliPrint(cli, msg);
}

void on_system_menu(EmbeddedCli *cli, char *args, void *context)
{
    (void)args;
    (void)context;
    clear_msg();
    cli_data.write_enable = true;
    cli_data.mode = SYSTEM;
    cli_data.test = MAX_BLOCK_INDEX;
    show_main_sys_menu(cli);
}

void on_start_hw_sim(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    // Start hardware simulation
    sim_start();
#endif
    on_system_menu(cli, args, context); // Refresh menu to show we are in system mode
}

void on_end_hw_sim(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    // Suspend hardware simulation
    sim_suspend();
#endif
    on_system_menu(cli, args, context); // Refresh menu to show we are in system mode
}

void on_pause(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    // Pause hardware simulation
    sim_pause();
#endif
    on_system_menu(cli, args, context); // Refresh menu to show we are in system mode
}

void on_resume(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    // Resume hardware simulation
    sim_resume();
#endif
    on_system_menu(cli, args, context); // Refresh menu to show we are in system mode
}

// show command

void show_sys_stats(EmbeddedCli *cli)
{
    embeddedCliPrint(cli, "System Statistics ...");
}

void show_log_stats(EmbeddedCli *cli)
{
    log_stats_t stats;
    log_get_stats(&stats);
    char msg[64];
    snprintf(msg, sizeof(msg), "Log Statistics: total logs %lu, dropped %u", stats.sum, stats.drop_cnt);
    embeddedCliPrint(cli, msg);
}

void show_task_stats(EmbeddedCli *cli)
{
    embeddedCliPrint(cli, "Task Statistics: ...");
}

void show_version(EmbeddedCli *cli)
{
    embeddedCliPrint(cli, "Version ...");
}

void show_config(EmbeddedCli *cli)
{
    char msg[64] = "Config:\r\n";
    snprintf(msg + 9, sizeof(msg) - 9, " CLI binding count %u", embeddedCliGetBindingsCount(cli));
    embeddedCliPrint(cli, msg);
}

void show_config_long(EmbeddedCli *cli)
{
    // clang-format off
    embeddedCliPrint(cli, "A long configuration to test cli and again ... \r\n"
                          "a long configuration to test cli and again ... \r\n"
                          "a long configuration to test cli and again ... \r\n"
                          "a long configuration to test cli ...");
    // clang-format on
}

void show_config_short(EmbeddedCli *cli)
{
    embeddedCliPrint(cli, "Short config");
}

void show_config_log(EmbeddedCli *cli)
{
    int             n = 12; // initial size of msg
    alignas(8) char msg[256 + 8] = "Log config: ";
    alignas(8) char tmp[64 + 8];
    bool            enabled = false;

    for (int did = 1; did < MAX_DOMAIN; did++) {
        for (int eid = 0; eid < MAX_ENTITY; eid++) {
            int level = log_get_level(did, eid);
            if (level) {
                if (!enabled)
                    enabled = true;
                int m = snprintf(
                    tmp, 64, "\r\n [%s%s%s] %s", domain_name(did), eid ? ":" : "", entity_name(eid), level_name(level)
                );
                if (n + m < 256) {
                    memcpy(msg + n, tmp, ALIGN_UP(m, 8));
                    n += m;
                } else {
                    msg[n] = '\0';
                    embeddedCliPrint(cli, msg);
                    memcpy(msg, tmp + 2, ALIGN_UP(m - 2, 8));
                    n = m - 2;
                }
            }
        }
    }

    if (enabled) {
        msg[n] = '\0';
        embeddedCliPrint(cli, msg);
    } else
        embeddedCliPrint(cli, "No logs enabled");
}

void show_domains(EmbeddedCli *cli)
{
    char msg[128];

    int n = snprintf(msg, sizeof(msg), "Domains:");

    for (int i = 1; i < MAX_DOMAIN; i++) {
        n += snprintf(msg + n, sizeof(msg) - n, " %s", domain_name((uint8_t)i));
    }
    embeddedCliPrint(cli, msg);
}

void show_entities(EmbeddedCli *cli)
{
    char msg[128];

    int n = snprintf(msg, sizeof(msg), "Entities:");

    for (int i = 1; i < MAX_ENTITY; i++) {
        n += snprintf(msg + n, sizeof(msg) - n, " %s", entity_name((uint8_t)i));
    }
    embeddedCliPrint(cli, msg);
}
void show_log_levels(EmbeddedCli *cli)
{
    char msg[128];

    int n = snprintf(msg, sizeof(msg), "Log levels:");

    for (int i = 0; i < NUM_LOG_LEVELS; i++) {
        n += snprintf(msg + n, sizeof(msg) - n, " %s", level_name((uint8_t)level_id[i]));
    }
    embeddedCliPrint(cli, msg);
}

/**
 * @brief position strings for the show command
 *  This is Two-Level Offset-Mapped Tree
 *  The Constraints: strictly limited to 2 levels
 *  Horizontal Scaling: At Level 1 you can add as many sub-cmds to compos_1 as you like
 *  Vertical Scaling: At Level 2 you can add as many sub-sub-cmds as you like
 *  You can't have more then 2 levels without structural rework.
 */
static const char  *compos_1[] = {"stats", "config", "version", "domains", "entities", "levels", nullptr}; // 4 show
static const char  *compo_1_1[] = {"log", "sys", "task", nullptr};                                         // 4 stats
static const char  *compo_1_2[] = {"", "short", "long", "log", nullptr};                                   // 4 config
static const char  *single[] = {nullptr};
static const char **compos[] = {compos_1, compo_1_1, compo_1_2, single, single, single, single};
/*
 * The action_idx table is a Cumulative Offset Map. It translates a 2D coordinate
 * into a 1D index for the shaw_action array.
 *                  Level2
 * Index Level 1	Number of Leafs	Offset (action_idx)	Calculation
 * 0	(Root/Show)	    -	            0               Always 0
 * 1	stats       	3	            0               Starts at 0
 * 2	config	        3	            3	            0 (prev offset) + 3 (stats count)
 * 3	version	        1	            6	            3 (prev offset) + 3
 */
static const uint8_t action_idx[] = {0, 0, 3, 7, 8, 9, 10}; // update as needed
static void (*shaw_action[])(EmbeddedCli *cli) = {
    show_log_stats,  // show stats log
    show_sys_stats,  // show stats sys
    show_task_stats, // show stats task

    show_config,       // show config
    show_config_short, // show config short
    show_config_long,  // show config long
    show_config_log,   // show config

    show_version,    // show version
    show_domains,    // show domains
    show_entities,   // show entities
    show_log_levels, // show log levels
};

void do_help(EmbeddedCli *cli, const char **options, bool usage, const char *prefix)
{
    char msg[128];
    bool use_options = (options != nullptr && options[0] != nullptr);

    int n = snprintf(msg, sizeof(msg), "%s", usage ? "Usage:" : "use:");

    if (prefix && prefix[0] != '\0')
        n += snprintf(msg + n, sizeof(msg) - n, " %s", prefix);

    if (use_options) {
        if (usage)
            n += snprintf(msg + n, sizeof(msg) - n, " <");

        bool first_word = false;
        for (int i = 0; options[i] != nullptr; i++) {
            if (options[i][0] == '\0')
                continue;

            if (n >= (int)sizeof(msg) - 20)
                break;

            const char *sep = "";
            if (first_word) {
                sep = usage ? "|" : " ";
            } else {
                sep = usage ? "" : " ";
            }

            n += snprintf(msg + n, sizeof(msg) - n, "%s%s", sep, options[i]);
            first_word = true;
        }

        if (usage && first_word)
            strncat(msg, ">", sizeof(msg) - strlen(msg) - 1);
    }
    embeddedCliPrint(cli, msg);
}

// handle sub-command completion
bool do_sub_completion(EmbeddedCli *cli, const char **options, const char *token)
{
    const char *match = nullptr;
    size_t      len = strlen(token);
    int         count = 0;

    // find matches
    for (int i = 0; options[i] != nullptr; i++) {
        if (strncmp(options[i], token, len) == 0) {
            if (count == 0)
                match = options[i];
            count++;
        }
    }
    // is a single match
    if (count == 1) {
        embeddedCliCompletion(cli, match);
        return true;
    }
    // multiple matches (Ambiguity)
    if (count > 1) {
        do_help(cli, options, false, nullptr);
        return false;
    }
    // no match
    return false;
}

void on_show_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    uint16_t        input_len;
    const char     *input = embeddedCliGetInputString(cli, &input_len);
    alignas(8) char buf[64 + 8];

    if (input_len > 64)
        input_len = 64;
    memcpy(buf, input, ALIGN_UP(input_len, 8));
    buf[input_len] = '\0';
    embeddedCliTokenizeArgs(buf);

    bool help = (token != nullptr && strchr(token, '?') != nullptr);

    char clean_token[32] = {0};
    if (help && token) {
        size_t len = strlen(token);
        if (len > 1 && token[len - 1] == '?') {
            strncpy(clean_token, token, len - 1);
            clean_token[len - 1] = '\0';
        }
    } else if (token) {
        strncpy(clean_token, token, 31);
    }

    if (pos == 1) {
        if (help)
            do_help(cli, compos_1, false, "show");
        else
            do_sub_completion(cli, compos_1, token);
    } else if (pos == 2) {
        const char *arg = embeddedCliGetToken(buf, 2);

        for (int i = 0; compos_1[i] != nullptr; i++) {
            const char *match = arg ? arg : clean_token;

            if (strncmp(match, compos_1[i], strlen(match)) == 0) {
                const char **sub = compos[i + 1];
                if (help) {
                    char msg[48];
                    snprintf(msg, sizeof(msg), "show %s", compos_1[i]);
                    do_help(cli, sub, false, msg);
                } else {
                    do_sub_completion(cli, sub, token);
                }
                return;
            }
        }
    }
}

void on_show_command(EmbeddedCli *cli, char *args, void *context)
{
    bool help = false;
    int  count = (args == nullptr) ? 0 : embeddedCliGetTokenCount(args);

    if (count == 0) {
        do_help(cli, compos_1, true, "show");
        return;
    }

    const char *last = embeddedCliGetToken(args, count);
    if (last && strcmp(last, "?") == 0)
        help = true;

    if (help && count == 1) {
        do_help(cli, compos_1, false, "show");
        return;
    }

    const char *arg1 = embeddedCliGetToken(args, 1);
    const char *invalid = "[ERROR] Invalid selection";

    int idx = -1;
    for (int i = 0; compos_1[i] != nullptr; i++) {
        if (strcmp(arg1, compos_1[i]) == 0) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        embeddedCliPrint(cli, invalid);
        return;
    }

    char         msg[48];
    const char **sub_options = compos[idx + 1];
    int          act_idx = action_idx[idx + 1];
    int          max_args = (sub_options == nullptr || sub_options[0] == nullptr) ? 1 : 2;

    if (!help && count > max_args) {
        embeddedCliPrint(cli, "[ERROR] Too many arguments");
        snprintf(msg, sizeof(msg), "show %s", arg1);
        do_help(cli, sub_options, true, msg);
        return;
    }

    if (max_args == 1) {
        if (help) {
            snprintf(msg, sizeof(msg), "show %s", compos_1[idx]);
            do_help(cli, nullptr, false, msg);
        } else {
            if (shaw_action[act_idx]) {
                shaw_action[act_idx](cli);
            }
        }
        return;
    }

    const char *arg2 = (count >= 2) ? embeddedCliGetToken(args, 2) : nullptr;

    if (arg2 == nullptr || (help && strcmp(arg2, "?") == 0)) {
        bool use_base = (sub_options != nullptr && sub_options[0] != nullptr && sub_options[0][0] == '\0');

        if (arg2 == nullptr && use_base) {
            if (shaw_action[act_idx])
                shaw_action[act_idx](cli);
        } else {
            snprintf(msg, sizeof(msg), "show %s", arg1);
            do_help(cli, sub_options, !help, msg);
        }
        return;
    }

    idx = -1;
    for (int i = 0; sub_options[i] != nullptr; i++) {
        if (strcmp(arg2, sub_options[i]) == 0) {
            idx = i;
            break;
        }
    }

    if (idx != -1) {
        const char *arg3 = (count >= 3) ? embeddedCliGetToken(args, 3) : nullptr;
        if (arg3 && strcmp(arg3, "?") == 0) {
            snprintf(msg, sizeof(msg), "show %s %s", arg1, arg2);
            do_help(cli, nullptr, false, msg);
            return;
        }
        act_idx = act_idx + idx;
        if (shaw_action[act_idx])
            shaw_action[act_idx](cli);
    } else {
        embeddedCliPrint(cli, invalid);
    }
}

static uint32_t cliHash(const char *str)
{
    uint32_t hash = 5381;
    uint32_t c;

    while ((c = (uint8_t)*str++)) {
        if (c >= 'a' && c <= 'z') {
            c -= 32; // convert to all capital
        }
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static void do_insert(uint8_t *map, uint32_t map_mask, name_id_t *table, int idx)
{
    uint32_t hash = table[idx].hash;
    uint32_t slot = hash & map_mask;

    while (map[slot] != MAP_EMPTY) {
        if (table[map[slot]].hash == hash) {
            break;
        }
        slot = (slot + 1) & map_mask;
    }
    map[slot] = (uint8_t)idx;
}

bool insert_name(const char *name, int id, int type, int n)
{
    uint32_t hash = cliHash(name);

    switch (type) {
    case TYPE_DOMAIN: {
        domain_tbl[n].hash = hash;
        domain_tbl[n].id = id;
        do_insert(domain_map, DOMAIN_MAP_MASK, domain_tbl, n);
        return true;
    }
    case TYPE_ENTITY: {
        entity_tbl[n].hash = hash;
        entity_tbl[n].id = id;
        do_insert(entity_map, ENTITY_MAP_MASK, entity_tbl, n);
        return true;
    }
    case TYPE_LOG_LEVEL: {
        level_tbl[n].hash = hash;
        level_tbl[n].id = id;
        do_insert(level_map, LOG_LEVEL_MAP_MASK, level_tbl, n);
        return true;
    }
    }
    return false;
}

static int find_name_id(const char *name, int type)
{
    name_id_t *table;
    uint8_t   *map;
    uint32_t   mask;
    uint32_t   hash = cliHash(name);

    if (type == TYPE_DOMAIN) {
        map = domain_map;
        table = domain_tbl;
        mask = DOMAIN_MAP_MASK;
    } else if (type == TYPE_ENTITY) {
        map = entity_map;
        table = entity_tbl;
        mask = ENTITY_MAP_MASK;
    } else if (type == TYPE_LOG_LEVEL) {
        map = level_map;
        table = level_tbl;
        mask = LOG_LEVEL_MAP_MASK;
    } else {
        return -1;
    }

    uint32_t slot = hash & mask;
    uint32_t start = slot;

    while (map[slot] != MAP_EMPTY) {
        uint8_t idx = map[slot];
        if (table[idx].hash == hash)
            return table[idx].id;

        slot = (slot + 1) & mask;
        if (slot == start)
            break;
    }

    return -1;
}

void cli_log_cmd_init(void)
{
    const char *name = nullptr;

    memset(domain_map, MAP_EMPTY, sizeof(domain_map));
    memset(entity_map, MAP_EMPTY, sizeof(entity_map));
    memset(level_map, MAP_EMPTY, sizeof(level_map));

    for (int did = 1; did < MAX_DOMAIN; did++) {
        name = domain_name((uint8_t)did);
        insert_name(name, did, TYPE_DOMAIN, did - 1);
    }

    insert_name("NONE", ENTITY_NONE, TYPE_ENTITY, 0);
    for (int eid = 1; eid < MAX_ENTITY; eid++) {
        name = entity_name((uint8_t)eid);
        insert_name(name, eid, TYPE_ENTITY, eid);
    }

    for (int i = 0; i < NUM_LOG_LEVELS; i++) {
        name = level_name((uint8_t)level_id[i]);
        insert_name(name, level_id[i], TYPE_LOG_LEVEL, i);
    }
}

void on_set_log_command(EmbeddedCli *cli, char *args, void *context)
{
    if (embeddedCliGetTokenCount(args) != 4) {
        embeddedCliPrint(cli, "Usage: set log <domain> <entity> <level>");
        return;
    }
    const char *log = embeddedCliGetToken(args, 1);
    if (strcmp(log, "log") != 0) {
        embeddedCliPrint(cli, "[ERROR] Invalid selection");
        return;
    }
    const char *dom = embeddedCliGetToken(args, 2);
    const char *ent = embeddedCliGetToken(args, 3);
    const char *lvl = embeddedCliGetToken(args, 4);

    int level = find_name_id(lvl, TYPE_LOG_LEVEL);
    if (level == -1) {
        embeddedCliPrint(cli, "[ERROR] Invalid log level");
        return;
    }

    bool all_domains = (strcmp(dom, "all") == 0);
    bool all_entities = (strcmp(ent, "all") == 0);

    int did = all_domains ? 0 : find_name_id(dom, TYPE_DOMAIN);
    if (!all_domains && did == -1) {
        embeddedCliPrint(cli, "[ERROR] Unknown log domain");
        return;
    }
    int eid = all_entities ? 0 : find_name_id(ent, TYPE_ENTITY);
    if (!all_entities && eid == -1) {
        embeddedCliPrint(cli, "[ERROR] Unknown log entity");
        return;
    }

    for (int d = 1; d < MAX_DOMAIN; d++) {
        if (!all_domains && d != did)
            continue;
        for (int e = 0; e < MAX_ENTITY; e++) {
            if (!all_entities && e != eid)
                continue;
            log_set_level(d, e, level);
        }
    }
    embeddedCliPrint(cli, "Log level set");
}

void set_system_commands(EmbeddedCli *cli)
{
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "back",
            .help = "Back to previous menu",
            .binding = on_back, // on going back
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "show",
            .help = "Show status",
            .tokenizeArgs = true,
            .binding = on_show_command, // on show command
        }
    );
    embeddedCliAddCompletion("show", on_show_completion);
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "set",
            .help = "Set log level",
            .tokenizeArgs = true,
            .binding = on_set_log_command, // on set command
        }
    );
#ifdef ENABLE_RTOS
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "start",
            .help = "Start HW Simulation",
            .binding = on_start_hw_sim, // on starting HW simulation
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "end",
            .help = "End HW Simulation",
            .binding = on_end_hw_sim, // on ending HW simulation
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "pause",
            .help = "Pause HW Simulation",
            .binding = on_pause, // on pausing
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "resume",
            .help = "Resume HW Simulation",
            .binding = on_resume, // on resuming
        }
    );
#endif
}