/**
 * @file cli_system.c
 * @brief CLI System Runner
 * Provides a command-line interface for system-level commands.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block_id.h"
#include "cli.h"
#include "embedded_cli.h"
#include "log.h"
#include "log_marker.h"
#include "pack.h"
#include "utils.h"

// prototypes without include file
void clear_msg(void);
void set_msg(const char *msg);
void print_msg(EmbeddedCli *cli);
void sim_start(void);
void sim_suspend(void);
bool sim_pause(void);
void sim_resume(void);
void cmd_in_test_mode(EmbeddedCli *cli);
int  set_sim_cfg(int len, stream_t *cfg);
int  get_sim_cfg(int len, stream_t *cfg, int what);

const char *entity_name(uint8_t entity, bool cap);
const char *domain_name(uint8_t domain, bool cap);
const char *level_name(uint8_t level, bool cap);

static uint32_t cliHash(const char *str); // refer to embedded-cli/lib/src/embedded_cli.c

typedef bool (*args_validate_t)(const char *token, uint8_t pos);
typedef void (*args_comp_t)(EmbeddedCli *cli, const char *token, uint8_t pos);
typedef struct comp_cast_s {
    uint8_t   idx; // action index
    uintptr_t ptr;
} comp_cast_t;

typedef struct comp_opt_s {
    uint8_t      idx; // action index
    const char **opt; // option names
} comp_opt_t;

typedef struct comp_spec_s {
    uint8_t idx; // specs index
    void (*args_comp)(EmbeddedCli *cli, const char *token, uint8_t pos);
} comp_spec_t;

typedef struct cmd_comp_s {
    const char       *name;     // command name
    const char      **level1;   // Level 1 sub-commands
    const char     ***level2;   // Level 2 sub-command's args
    const comp_opt_t *level3;   // Level 3 options
    uint16_t          opt_size; // option table size
    uint16_t          spec_idx; // index into specs
} cmd_comp_t;

typedef struct cmd_comp_desc_s {
    cmd_comp_t     comp;
    const uint8_t *act_idx;  // command's action index table
    const uint32_t opt_mask; // options enabled mask

    void (**action)(EmbeddedCli *cli, char *args, int count);
} cmd_comp_desc_t;

// for name to id matching e.g., "CRITICAL" to LOG_LEVEL_CRITICAL (10)
typedef struct {
    uint32_t hash;
    int      id;
} name_id_t;

// helper counting macros
#define COUNT_L2(str_array) (sizeof(str_array) / sizeof(char *) - 1)
#define L2_SLOTS(str_array) (COUNT_L2(str_array) == 0 ? 1 : COUNT_L2(str_array))

// sentinel value to indicate an empty slot in the map
#define MAP_EMPTY 0xFF

#define DOMAIN_MAP_SIZE POW2(NUM_DOMAINS * 2)
#define ENTITY_MAP_SIZE POW2(NUM_ENTITIES * 2)
#define LOG_LEVEL_MAP_SIZE POW2(NUM_LOG_LEVELS * 2)

#define DOMAIN_MAP_MASK (DOMAIN_MAP_SIZE - 1)
#define ENTITY_MAP_MASK (ENTITY_MAP_SIZE - 1)
#define LOG_LEVEL_MAP_MASK (LOG_LEVEL_MAP_SIZE - 1)

static_assert((DOMAIN_MAP_SIZE & DOMAIN_MAP_MASK) == 0, "DOMAIN_MAP_SIZE must be power of 2");
static_assert((ENTITY_MAP_SIZE & ENTITY_MAP_MASK) == 0, "ENTITY_MAP_SIZE must be power of 2");
static_assert((LOG_LEVEL_MAP_SIZE & LOG_LEVEL_MAP_MASK) == 0, "LOG_LEVEL_MAP_SIZE must be power of 2");

// -- Globals --

static uint8_t   domain_map[DOMAIN_MAP_SIZE];
static name_id_t domain_tbl[NUM_DOMAINS - 1]; // exclude DOMAIN_NONE
static uint8_t   entity_map[ENTITY_MAP_SIZE];
static name_id_t entity_tbl[NUM_ENTITIES];
static uint8_t   level_map[LOG_LEVEL_MAP_SIZE];
static name_id_t level_tbl[NUM_LOG_LEVELS];
static const int level_id[NUM_LOG_LEVELS] = {LOG_LEVEL_NONE, LOG_LEVEL_CRITICAL, LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};

#define MAP_ALL_ENTITIES ((1U << MAX_ENTITY) - 1)
#define MAP_ALL_DOMAINS ((1U << MAX_DOMAIN) - 1)

#ifdef ENABLE_RTOS
static const uint16_t ent_map_en = MAP_ALL_ENTITIES;
#else
static const uint16_t ent_map_en = (MAP_ALL_ENTITIES & ~(1U << ENTITY_SIM));
#endif

#ifdef ENABLE_TEST
static const uint16_t dom_map_en = MAP_ALL_DOMAINS;
#else
static const uint16_t dom_map_en = (MAP_ALL_DOMAINS & ~(1U << DOMAIN_TEST));
#endif

const char *error = "[ERROR] Too many arguments";
const char *invalid = "[ERROR] Invalid selection";

// -- End of globals --

void show_main_sys_menu(EmbeddedCli *cli)
{
    cli_clear_menu_region();
    embeddedCliSetAppContext(0x0);
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
    cli_data.mode = SYSTEM;
    cli_data.test = MAX_BLOCK_INDEX;
    cli_data.flags.write_enable = true;
    show_main_sys_menu(cli);
}

void on_start_hw_sim(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    cmd_in_test_mode(cli);
    // Start hardware simulation
    sim_start();
#endif
}

void on_end_hw_sim(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    cmd_in_test_mode(cli);
    // Suspend hardware simulation
    sim_suspend();
#endif
}

void on_pause(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    cmd_in_test_mode(cli);
    // Pause hardware simulation
    sim_pause();
#endif
}

void on_resume(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_RTOS
    cmd_in_test_mode(cli);
    // Resume hardware simulation
    sim_resume();
#endif
}

// show command

void show_stats_sys(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "System Statistics ...");
}

void show_stats_log(EmbeddedCli *cli, char *args, int count)
{
    log_stats_t stats;
    log_get_stats(&stats);
    char msg[64];
    snprintf(msg, sizeof(msg), "Log Statistics: total logs %lu, dropped %u", stats.sum, stats.drop_cnt);
    embeddedCliPrint(cli, msg);
}

void show_stats_task(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "Task Statistics: ...");
}

void show_version(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "Version ...");
}

void show_config(EmbeddedCli *cli, char *args, int count)
{
    char msg[64] = "Config:\r\n";
    snprintf(msg + 9, sizeof(msg) - 9, " CLI binding count %u", embeddedCliGetBindingsCount(cli));
    embeddedCliPrint(cli, msg);
}

void show_config_sim(EmbeddedCli *cli, char *args, int count)
{
    const char *opt = embeddedCliGetToken(args, 3);
    int         what = 0; // 'current'

    if (strcmp(opt, "default") == 0)
        what = 1;
    else if (strcmp(opt, "ranges") == 0)
        what = 2;

    alignas(8) char msg[256 + 8] = "Simulation config:";

    if (what < 2) {
        stream_t pkt[2];
        get_sim_cfg(2, pkt, what);

        // clang-format off
        snprintf(msg + 18, sizeof(msg) - 18,
            "\r\n Thermal Mass: %u J/°C, Conductance: %u mW/°C, Heater Power: %u mW"
            "\r\n Ambient Temp: %d°C, Ramp Time: %u sec, Sensor latency: %u ms"
            "\r\n Target Temp: %u°C, Critical Temp: %u°C"
            "\r\n Cooldown Temp: %u°C, Hysteresis: %u°C",
            pkt[0].u16[1], pkt[0].u16[2], pkt[0].u16[3],
            (int16_t)pkt[1].u16[3], pkt[0].u8[1], pkt[1].u8[1],
            pkt[1].u8[2], pkt[1].u8[3],
            pkt[1].u8[4], pkt[1].u8[5]);
        // clang-format on
        embeddedCliPrint(cli, msg);
    } else {
        alignas(8) char tmp[128 + 8];
        stream_t        p_min[2], p_max[2];
        get_sim_cfg(2, p_min, 2);
        get_sim_cfg(2, p_max, 3);

        int n = snprintf(msg, 256, "Simulation Arg Ranges:");

        // clang-format off
        struct { const char *name; int min; int max; const char *unit; } rows[] = {
            {"Ramp Time", p_min[0].u8[1], p_max[0].u8[1], "sec"},
            {"Thermal Mass", p_min[0].u16[1], p_max[0].u16[1], "J/°C"},
            {"Conductance", p_min[0].u16[2], p_max[0].u16[2], "mW/°C"},
            {"Heater Power", p_min[0].u16[3], p_max[0].u16[3], "mW"},
            {"Ambient Temp", (int16_t)p_min[1].u16[3], (int16_t)p_max[1].u16[3], "°C"},
            {"Sensor latency", p_min[1].u8[1], p_max[1].u8[1], "ms"},
            {"Target Temp", p_min[1].u8[2], p_max[1].u8[2], "°C"},
            {"Critical Temp",p_min[1].u8[3], p_max[1].u8[3], "°C"},
            {"Cooldown Temp", p_min[1].u8[4], p_max[1].u8[4], "°C"},
            {"Hysteresis", p_min[1].u8[5], p_max[1].u8[5], "°C"}
        };
        // clang-format on

        for (int i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            int m = snprintf(tmp, 128, "\r\n %s: %d - %d %s", rows[i].name, rows[i].min, rows[i].max, rows[i].unit);

            if (n + m < 256) {
                memcpy(msg + n, tmp, ALIGN_UP(m, 8));
                n += m;
            } else {
                msg[n] = '\0';
                embeddedCliPrint(cli, msg);
                memcpy(msg, tmp + 2, ALIGN_UP(m - 2, 8)); // skip leading \r\n
                n = m - 2;
            }
        }
        msg[n] = '\0';
        embeddedCliPrint(cli, msg);
    }
}

void show_config_short(EmbeddedCli *cli, char *args, int count)
{
    embeddedCliPrint(cli, "Short config"); // placeholder
}

void show_config_log(EmbeddedCli *cli, char *args, int count)
{
    int             n = 12; // initial size of msg
    alignas(8) char msg[256 + 8] = "Log config: ";
    alignas(8) char tmp[64 + 8];
    bool            enabled = false;

    for (int did = 1; did < NUM_DOMAINS; did++) {
        if (!(dom_map_en & BIT(did)))
            continue;
        for (int eid = 0; eid < NUM_ENTITIES; eid++) {
            if (!(ent_map_en & BIT(eid)))
                continue;
            int level = log_get_level(did, eid);
            if (level) {
                if (!enabled)
                    enabled = true;
                // clang-format off
                int m = snprintf(tmp, 64, "\r\n [%s%s%s] %s", domain_name(did, 1), eid ? ":" : "",
                                 entity_name(eid, 1), level_name(level, 1));
                // clang-format on
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

void show_domains(EmbeddedCli *cli, char *args, int count)
{
    char msg[128] = "Domains:";
    int  n = 8;

    for (int did = 1; did < NUM_DOMAINS; did++) {
        if (!(dom_map_en & BIT(did)))
            continue;
        n += snprintf(msg + n, sizeof(msg) - n, " %s", domain_name((uint8_t)did, 1));
    }
    embeddedCliPrint(cli, msg);
}

void show_entities(EmbeddedCli *cli, char *args, int count)
{
    char msg[128] = "Entities:";
    int  n = 9;

    for (int eid = 1; eid < NUM_ENTITIES; eid++) {
        if (!(ent_map_en & BIT(eid)))
            continue;
        n += snprintf(msg + n, sizeof(msg) - n, " %s", entity_name((uint8_t)eid, 1));
    }
    embeddedCliPrint(cli, msg);
}

void show_log_levels(EmbeddedCli *cli, char *args, int count)
{
    char msg[128] = "Log levels:";
    int  n = 11;

    for (int i = 0; i < NUM_LOG_LEVELS; i++) {
        n += snprintf(msg + n, sizeof(msg) - n, " %s", level_name((uint8_t)level_id[i], 1));
    }
    embeddedCliPrint(cli, msg);
}

static uintptr_t find_by_index(const void *table, uint16_t size, uint16_t idx)
{
    const comp_cast_t *cast_tbl = (const comp_cast_t *)table;

    for (uint16_t i = 0; i < size; i++) {
        if (cast_tbl[i].idx == idx)
            return cast_tbl[i].ptr;
    }
    return 0;
}

void do_log_help(EmbeddedCli *cli, const char **matches, int count, uint8_t pos)
{
    int         n = 5; // initial size of msg
    char        msg[128] = "use: ";
    const char *name;

    if (matches) {
        for (int i = 0; i < count; i++) {
            n += snprintf(msg + n, sizeof(msg) - n, "%s", matches[i]);
            msg[n++] = '|';
        }
        goto done;
    }

    if (pos == 2 || pos == 3)
        n += snprintf(msg + n, sizeof(msg) - n, "all|");

    if (pos == 2) {
        for (int i = 1; i < NUM_DOMAINS; i++) {
            if (!(dom_map_en & BIT(i)))
                continue;
            n += snprintf(msg + n, sizeof(msg) - n, "%s", domain_name((uint8_t)i, 0));
            msg[n++] = '|';
        }
    } else if (pos == 3) {
        for (int i = 0; i < NUM_ENTITIES; i++) {
            if (!(ent_map_en & BIT(i)))
                continue;
            n += snprintf(msg + n, sizeof(msg) - n, "%s", entity_name((uint8_t)i, 0));
            msg[n++] = '|';
        }
    } else if (pos == 4) {
        for (int i = 0; i < NUM_LOG_LEVELS; i++) {
            n += snprintf(msg + n, sizeof(msg) - n, "%s", level_name((uint8_t)level_id[i], 0));
            msg[n++] = '|';
        }
    } else {
        return;
    }
done:
    msg[n - 1] = '\0';
    embeddedCliPrint(cli, msg);
}

bool set_log_validate(const char *token, uint8_t pos)
{
    if (pos == 2 || pos == 3) {
        if (strcmp(token, "all") == 0)
            return true;
    }

    if (pos == 2) {
        for (int did = 1; did < NUM_DOMAINS; did++) {
            if ((dom_map_en & BIT(did)) && strcmp(token, domain_name(did, 0)) == 0)
                return true;
        }
    } else if (pos == 3) {
        for (int eid = 0; eid < NUM_ENTITIES; eid++) {
            if ((ent_map_en & BIT(eid)) && strcmp(token, entity_name(eid, 0)) == 0)
                return true;
        }
    } else if (pos == 4) {
        for (int i = 0; i < NUM_LOG_LEVELS; i++) {
            if (strcmp(token, level_name((uint8_t)level_id[i], 0)) == 0)
                return true;
        }
    }
    return false;
}

void set_log_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    const char *match = nullptr;
    const char *matches[8];
    size_t      len = strlen(token);
    int         count = 0;

    if (len == 0) {
        do_log_help(cli, nullptr, 0, pos);
        return;
    }

    bool help = (strchr(token, '?') != nullptr);
    if (help) {
        do_log_help(cli, nullptr, 0, pos);
        return;
    }

    if (pos == 2 || pos == 3) {
        if (strncmp(token, "all", len) == 0) {
            embeddedCliCompletion(cli, "all");
            return;
        }
    }

    if (pos == 2) {
        for (int did = 1; did < NUM_DOMAINS; did++) {
            if (!(dom_map_en & BIT(did)))
                continue;
            if (count == 8)
                break;
            match = domain_name((uint8_t)did, 0);
            if (strncmp(token, match, len) == 0) {
                matches[count] = match;
                count++;
            }
        }
    } else if (pos == 3) {
        for (int eid = 0; eid < NUM_ENTITIES; eid++) {
            if (!(ent_map_en & BIT(eid)))
                continue;
            if (count == 8)
                break;
            match = entity_name(eid, 0);
            if (strncmp(token, match, len) == 0) {
                matches[count] = match;
                count++;
            }
        }
    } else if (pos == 4) {
        for (int i = 0; i < NUM_LOG_LEVELS; i++) {
            if (count == 8)
                break;
            match = level_name((uint8_t)level_id[i], 0);
            if (strncmp(token, match, len) == 0) {
                matches[count] = match;
                count++;
            }
        }
    }

    if (count == 1)
        embeddedCliCompletion(cli, matches[0]);
    else if (count > 1)
        do_log_help(cli, matches, count, pos);
}

// -- Globals --

/**
 * @brief Global table for specialized argument completion logic.
 * Index 0 is reserved for 'no-op' (static data default).
 * Match via cmd_comp_t.spec_idx.
 */
#define SPECS_SIZE (sizeof(specs) / sizeof(comp_spec_t))
static const comp_spec_t specs[] = {
    {0, nullptr           }, // Default: No special handling
    {1, set_log_completion}, // Custom: 'set log <domain> <entity> <level>'
};

#define SPECS_VALID_SIZE (sizeof(specs_valid) / sizeof(args_validate_t))
static const args_validate_t specs_valid[] = {
    nullptr,          // [0] Reserved
    set_log_validate, // [1] Index 1: 'set log' validation
    nullptr,          // [2] Index 2: 'set sim' (No validation)
};

static const char *single[] = {nullptr};

/**
 * @brief Auto-completion tree for the 'set' command.
 * Special handling profiles for the 'set' command.
 * 1. Fully Dynamic ('set log'):
 *    - Hybrid path: The command 'log' is a terminal verb (Path Depth 1).
 *    - Dynamic Completion: Uses 'set_log_completion' (spec_idx 1) to fetch
 *      live system strings (Domains/Entities) for each positional argument.
 *    - Custom Help: Overrides standard help to show filtered lists of
 *      currently available domains/entities.
 * 2. Semi-Static ('set sim'):
 *    - Nested path: Requires sub-verbs 'phy' or 'temp' (Path Depth 2).
 *    - Positional Hints: Uses the 'level3' table only for static naming
 *      hints (e.g., <ramp>, <mass>) to guide numeric input.
 *    - Standard Completion: TAB simply provides the next sub-verb.
 */
#ifdef ENABLE_RTOS
static const char  *set_cmd_compo_1[] = {"log", "sim", nullptr};    // Level 1: Sub-commands
static const char  *set_cmd_compo_1_1[] = {"", nullptr};            // Level 2: 'log' (handled by spec_idx 1)
static const char  *set_cmd_compo_1_2[] = {"phy", "temp", nullptr}; // Level 2: 'sim' options 'set sim phy' | 'set sim temp'
static const char **set_cmd_compos[] = {
    set_cmd_compo_1_1, // 'set log' path
    set_cmd_compo_1_2, // 'set sim' path
    single,
};
#else
static const char  *set_cmd_compo_1[] = {"log", nullptr}; // Level 1: Sub-commands
static const char  *set_cmd_compo_1_1[] = {"", nullptr};  // Level 2: 'log' (handled by spec_idx 1)
static const char **set_cmd_compos[] = {
    set_cmd_compo_1_1, // 'set log' path
};
#endif

#define SET_LOG (1)
#define SET_SIM (SET_LOG + L2_SLOTS(set_cmd_compo_1_1))

static const char *set_cmd_log_args[] = {"domain", "entity", "level", nullptr};
static const char *set_cmd_sim_phy_args[] = {"ramp", "mass", "cond", "power", "ambient", nullptr};
static const char *set_cmd_sim_temp_args[] = {"latency", "target", "critical", "hyster", "cooldown", nullptr};

// Level 3 Options:
// These act as POSITIONAL HINTS because spec_idx is non-zero.
static const comp_opt_t set_cmd_options[] = {
    {SET_LOG,     set_cmd_log_args     }, // 'set log'
#ifdef ENABLE_RTOS
    {SET_SIM,     set_cmd_sim_phy_args }, // 'set sim phy'
    {SET_SIM + 1, set_cmd_sim_temp_args}, // 'set sim temp'
#endif
};

static const cmd_comp_t set_cmd_comp = {
    .name = "set",
    .level1 = set_cmd_compo_1,
    .level2 = set_cmd_compos,
    .level3 = set_cmd_options,
    .opt_size = (uint16_t)(sizeof(set_cmd_options) / sizeof(comp_opt_t)),
    .spec_idx = 1 // Points to set_log_completion in 'specs' table
};

void on_set_log_command(EmbeddedCli *cli, char *args, int count);
void on_set_sim_command(EmbeddedCli *cli, char *args, int count);

static void (*set_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    nullptr,            // [0] Reserved
    on_set_log_command, // [1] sel log <pos args>
#ifdef ENABLE_RTOS
    on_set_sim_command, // [2] set sim [options]
#endif
};

/**
 * Action index table:
 * [0] = reserved
 * [1] = log (action index 1)
 * [2] = sim (action index 2)
 */
static const uint8_t set_cmd_action_idx[] = {0, SET_LOG, SET_SIM};

static const cmd_comp_desc_t set_cmd_desc = {
    .comp = set_cmd_comp,
    .act_idx = set_cmd_action_idx,
    .action = set_cmd_action,
};

/**
 * @brief Auto-completion tree for the 'show' command.
 * Structure: Two-Level Offset-Mapped Tree.
 * - Horizontal: Add sub-commands to level1.
 * - Vertical: Level 2 provides arguments for Level 1 sub-commands.
 * - Note: Level 3 (options) and Specs are indexed via action_idx calculation.
 */
static const char *show_cmd_compo_1[] = {"stats", "config", "version", "domains", "entities", "levels", nullptr};
static const char *show_cmd_compo_1_1[] = {"log", "sys", "task", nullptr}; // 'show stats ...'
#ifdef ENABLE_RTOS
static const char *show_cmd_compo_1_2[] = {"", "short", "sim", "log", nullptr}; // 'show config ...'
#else
static const char *show_cmd_compo_1_2[] = {"", "short", "log", nullptr}; // 'show config ...'
#endif
static const char **show_cmd_compos[] = {
    show_cmd_compo_1_1, // Sub-args for 'stats'
    show_cmd_compo_1_2, // Sub-args for 'config'
    single,             // No sub-args for 'version'
    single,             // No sub-args for 'domains'
    single,             // No sub-args for 'entities'
    single,             // No sub-args for 'levels'
};

#define SHOW_STATS (0)
#define SHOW_CONFIG (SHOW_STATS + L2_SLOTS(show_cmd_compo_1_1))
#define SHOW_VERSION (SHOW_CONFIG + L2_SLOTS(show_cmd_compo_1_2))
#define SHOW_DOMAINS (SHOW_VERSION + L2_SLOTS(single))
#define SHOW_ENTITIES (SHOW_DOMAINS + L2_SLOTS(single))
#define SHOW_LEVELS (SHOW_ENTITIES + L2_SLOTS(single))

/**
 * @brief Level 3 static options mapping.
 * Matches 1D action index to a list of optional terminal arguments.
 */
static const char *options_stats_log[] = {"clear", nullptr};
#ifdef ENABLE_RTOS
static const char *options_config_sim[] = {"current", "default", "ranges", nullptr};
static comp_opt_t  show_cmd_options[] = {
    {SHOW_STATS,      options_stats_log }, // Maps to 'show stats log [clear]'
    {SHOW_CONFIG + 2, options_config_sim}, // Maps to 'show config sim [current|default|ranges]'
};
#else
static const comp_opt_t show_cmd_options[] = {
    {SHOW_STATS, options_stats_log}, // Maps to 'show stats log [clear]'
};
#endif

/**
 * @brief Cumulative Offset Map for 'show' command hierarchy.
 * Translates (Level 1 Index, Level 2 Index) -> 1D Linear Action Index.
 *
 * L1 Index | Sub-Cmd  | L2 Count | Action Offset | Range
 * ---------|----------|----------|---------------|-------
 * 1        | stats    | 3        | 0             | 0-2
 * 2        | config   | 4        | 3             | 3-6
 * 3        | version  | 1        | 7             | 7
 * 4        | domains  | 1        | 8             | 8
 * 5        | entities | 1        | 9             | 9
 * 6        | levels   | 1        | 10            | 10
 */
static const uint8_t show_cmd_action_idx[] = {0, SHOW_STATS, SHOW_CONFIG, SHOW_VERSION, SHOW_DOMAINS, SHOW_ENTITIES, SHOW_LEVELS};
#ifdef ENABLE_RTOS
#define SHOW_OPTS (BIT(SHOW_STATS) | BIT(SHOW_CONFIG) | BIT(SHOW_CONFIG + 2))
#else
#define SHOW_OPTS (BIT(SHOW_STATS) | BIT(SHOW_CONFIG))
#endif
// Bitmask identifying which action indices support Level 3 options (show_cmd_options)
static const uint32_t show_cmd_allow_opt = SHOW_OPTS;

/**
 * @brief Execution handlers mapped to the 1D Action Index.
 */
static void (*show_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    show_stats_log,  // [0] show stats log [clear]
    show_stats_sys,  // [1] show stats sys
    show_stats_task, // [2] show stats task

    show_config,       // [3] show config (hybrid: leaf + path)
    show_config_short, // [4] show config short
#ifdef ENABLE_RTOS
    show_config_sim, // [5] show config sim [current|default|ranges]
#endif
    show_config_log, // [6] show config log

    show_version,    // [7] show version
    show_domains,    // [8] show domains
    show_entities,   // [9] show entities
    show_log_levels, // [10] show log levels
};

static const cmd_comp_desc_t show_cmd_desc = {
    // clang-format off
    .comp = {
        .name = "show",
        .level1 = show_cmd_compo_1,
        .level2 = show_cmd_compos,
        .level3 = show_cmd_options,
        .opt_size = (uint16_t)(sizeof(show_cmd_options) / sizeof(comp_opt_t)),
        .spec_idx = 0, // No special arg completion logic
    },
    // clang-format on
    .act_idx = show_cmd_action_idx,
    .opt_mask = show_cmd_allow_opt,
    .action = show_cmd_action,
};

// -- End of globals --

void do_help(EmbeddedCli *cli, const char **options, int usage, const char *prefix)
{
    char        msg[128];
    const char *head = (usage == 1 || usage == 3) ? "Usage:" : "use:";
    int         n = snprintf(msg, sizeof(msg), "%s %s", head, prefix ? prefix : "");

    if (options && options[0]) {
        // usage 1: <a|b|c> (Required selection)
        // usage 2: [a|b|c] (Optional selection)
        // usage 3: <a> <b> <c> (Fixed positional args)
        if (usage != 3 && n > 0 && msg[n - 1] != ' ') {
            msg[n++] = ' ';
        }

        if (usage == 1)
            msg[n++] = '<';
        else if (usage == 2)
            msg[n++] = '[';

        bool first_word = true;
        for (int i = 0; options[i] != nullptr; i++) {
            if (options[i][0] == '\0')
                continue;

            if (n >= (int)sizeof(msg) - 12)
                break;

            if (usage == 3) {
                n += snprintf(msg + n, sizeof(msg) - n, " <%s>", options[i]);
            } else {
                n += snprintf(msg + n, sizeof(msg) - n, "%s%s", first_word ? "" : "|", options[i]);
                first_word = false;
            }
        }

        if (usage == 1)
            msg[n++] = '>';
        else if (usage == 2)
            msg[n++] = ']';
    }
    msg[n] = '\0';
    embeddedCliPrint(cli, msg);
}

// handle sub-command completion
bool do_sub_completion(EmbeddedCli *cli, const char **options, const char *token)
{
    const char *match = nullptr;
    const char *matches[8];
    size_t      len = strlen(token);
    int         count = 0;

    // find matches
    for (int i = 0; options[i] != nullptr; i++) {
        if (strncmp(options[i], token, len) == 0) {
            if (count < 8) {
                match = options[i];
                matches[count] = match;
                count++;
            }
        }
    }
    // is a single match
    if (count == 1) {
        embeddedCliCompletion(cli, match);
        return true;
    }
    // multiple matches (Ambiguity)
    if (count > 1) {
        matches[count] = nullptr;
        do_help(cli, matches, false, nullptr);
        return false;
    }
    // no match
    return false;
}

static int find_index_by_str(const char **list, const char *str)
{
    if (!list || !str)
        return -1;
    for (int i = 0; list[i] != nullptr; i++) {
        if (strcmp(list[i], str) == 0)
            return i;
    }
    return -1;
}

static void do_cmd_arg_completion_dispatch(EmbeddedCli *cli, const char *token, uint8_t pos, const cmd_comp_t *comp, const uint8_t *action_idx)
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

    if (pos == 1) {
        if (help)
            do_help(cli, comp->level1, false, comp->name);
        else
            do_sub_completion(cli, comp->level1, token);
        return;
    } else if (pos == 2) {
        const char *arg = embeddedCliGetToken(buf, 2);

        for (int i = 0; comp->level1[i] != nullptr; i++) {
            if (arg && strncmp(arg, comp->level1[i], strlen(arg)) == 0) {
                bool         is_full_match = (strlen(arg) == strlen(comp->level1[i]));
                const char **l2_subcmd = comp->level2[i];

                bool has_options = (l2_subcmd && l2_subcmd[0] && (l2_subcmd[0][0] != '\0' || l2_subcmd[1] != nullptr));

                if (!has_options || (is_full_match && !has_options)) {
                    goto is_special_handling;
                }

                if (help) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "%s %s", comp->name, comp->level1[i]);
                    do_help(cli, l2_subcmd, false, msg);
                } else if (!do_sub_completion(cli, l2_subcmd, token)) {
                    goto is_special_handling;
                }
                return;
            }
        }
        return;
    } else if (pos == 3) {
        const char *arg1 = embeddedCliGetToken(buf, 2);
        const char *arg2 = embeddedCliGetToken(buf, 3);

        if (!arg1 || !arg2)
            goto is_special_handling;

        int l1_idx = find_index_by_str(comp->level1, arg1);

        if (l1_idx != -1) {
            const char **l2_subcmd = comp->level2[l1_idx];
            int          l2_idx = find_index_by_str(l2_subcmd, arg2);

            bool is_hybrid = (l2_subcmd && l2_subcmd[0] && l2_subcmd[0][0] == '\0');
            if (l2_idx == -1 && is_hybrid && comp->spec_idx != 0)
                goto is_special_handling;

            if (l2_idx != -1 && action_idx != nullptr) {
                int          act_idx = action_idx[l1_idx + 1] + l2_idx;
                const char **l3_opts = (const char **)find_by_index(comp->level3, comp->opt_size, act_idx);

                if (l3_opts) {
                    if (help) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "%s %s %s", comp->name, arg1, arg2);
                        do_help(cli, l3_opts, false, msg);
                        return;
                    }
                    if (do_sub_completion(cli, l3_opts, token)) {
                        return;
                    }
                    goto is_special_handling;
                }
            }
        }

        if (comp->spec_idx != 0)
            goto is_special_handling;
        return;
    }
is_special_handling:
    args_comp_t args_comp = (args_comp_t)find_by_index(specs, SPECS_SIZE, comp->spec_idx);
    if (args_comp)
        args_comp(cli, token, pos);
}

void set_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    do_cmd_arg_completion_dispatch(cli, token, pos, &set_cmd_comp, nullptr);
}

void show_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    do_cmd_arg_completion_dispatch(cli, token, pos, &show_cmd_desc.comp, show_cmd_desc.act_idx);
}

static void cmd_dispatch(EmbeddedCli *cli, char *args, int count, const cmd_comp_desc_t *desc)
{
    cmd_in_test_mode(cli);
    char msg[128];

    const char *last = (count > 0) ? embeddedCliGetToken(args, count) : nullptr;
    bool        help = (last && strcmp(last, "?") == 0);

    if (count == 0 || (help && count == 1)) {
        do_help(cli, desc->comp.level1, !help, desc->comp.name);
        return;
    }

    const char *arg1 = embeddedCliGetToken(args, 1);
    int         l1_idx = find_index_by_str(desc->comp.level1, arg1);
    if (l1_idx == -1)
        return (void)embeddedCliPrint(cli, invalid);

    int          act_idx = desc->act_idx ? desc->act_idx[l1_idx + 1] : (l1_idx + 1);
    const char **l2_subcmd = desc->comp.level2[l1_idx];
    bool         has_l2 = (l2_subcmd && l2_subcmd[0] && l2_subcmd[0][0] != '\0');

    const char *arg2 = (count >= 2) ? embeddedCliGetToken(args, 2) : nullptr;
    int         l2_idx = -1;
    bool        is_hybrid = (l2_subcmd && l2_subcmd[0] && l2_subcmd[0][0] == '\0');

    if (arg2 && !(help && count == 2))
        l2_idx = find_index_by_str(l2_subcmd, arg2);

    if (l2_idx == -1 && is_hybrid && !arg2)
        l2_idx = 0;

    if (!arg2 || (help && count == 2)) {
        if (!has_l2 && desc->comp.spec_idx == 0 && !help)
            goto action_run;

        if (!arg2 && is_hybrid && desc->comp.spec_idx == 0)
            goto action_run;

        if (desc->comp.spec_idx == 0 || !is_hybrid) {
            snprintf(msg, sizeof(msg), "%s %s", desc->comp.name, arg1);
            do_help(cli, l2_subcmd, 1, msg);
            return;
        }
    }

    if (l2_idx == -1 && !help && desc->comp.spec_idx == 0)
        return (void)embeddedCliPrint(cli, invalid);

    act_idx += (l2_idx != -1 ? l2_idx : 0);

    const char **l3_opts = nullptr;
    if (desc->comp.spec_idx != 0 || (desc->opt_mask & BIT(act_idx)))
        l3_opts = (const char **)find_by_index(desc->comp.level3, desc->comp.opt_size, act_idx);

    if (desc->comp.spec_idx != 0) {
        int path_tokens = (arg2 && l2_idx != -1 && !is_hybrid) ? 2 : 1;
        int num_tkns = help ? (count - 1) : count;
        int present = (num_tkns > path_tokens) ? (num_tkns - path_tokens) : 0;

        if (!l3_opts && present > 0)
            return (void)embeddedCliPrint(cli, invalid);
        if (l3_opts) {
            int max_allowed = 0;
            while (l3_opts[max_allowed])
                max_allowed++;
            if (present > max_allowed) {
                embeddedCliPrint(cli, error);
                return;
            }
        }
        // clang-format off
        const args_validate_t is_arg_valid = (act_idx < SPECS_VALID_SIZE) ?
                                              specs_valid[act_idx] : nullptr;
        // clang-format on
        if (is_arg_valid && present > 0) {
            for (int i = path_tokens + 1; i <= num_tkns; i++) {
                const char *tkn = embeddedCliGetToken(args, i);
                if (!is_arg_valid(tkn, i))
                    return (void)embeddedCliPrint(cli, invalid);
            }
        }

        if (help || (l3_opts && present == 0)) {
            const int limit = (is_hybrid && present > 0 && !is_arg_valid) ? path_tokens : num_tkns;
            int       n = snprintf(msg, sizeof(msg), "%s", desc->comp.name);

            for (int i = 1; i <= limit; i++) {
                n += snprintf(msg + n, sizeof(msg) - n, " %s", embeddedCliGetToken(args, i));
            }

            const char **opts = l3_opts;

            if (opts && present > 0) {
                for (int i = 0; (i < present) && opts[0] != nullptr; i++) {
                    opts++;
                }
            }

            do_help(cli, opts, 3, msg);
            return;
        }

        goto action_run;
    }

    const char *arg3 = (count >= 3) ? embeddedCliGetToken(args, 3) : nullptr;

    if (help || (l3_opts && !arg3)) {
        int n = snprintf(msg, sizeof(msg), "%s %s", desc->comp.name, arg1);
        if (arg2 && l2_idx != -1)
            snprintf(msg + n, sizeof(msg) - n, " %s", arg2);
        do_help(cli, l3_opts, l3_opts != nullptr ? 2 : 0, msg); // Usage: [a|b|c]
        return;
    }

    if (arg3 && !l3_opts)
        return (void)embeddedCliPrint(cli, error);

    if (arg3 && l3_opts && find_index_by_str(l3_opts, arg3) == -1)
        return (void)embeddedCliPrint(cli, invalid);

    // Final check for unexpected 4th token in standard commands
    if (count > (l3_opts ? 3 : 2) && !help)
        return (void)embeddedCliPrint(cli, error);

action_run:
    if (desc->action[act_idx])
        desc->action[act_idx](cli, args, count);
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
        if (table[map[slot]].hash == hash)
            break;
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

static void sim_cfg_print_oor(EmbeddedCli *cli, int ret)
{
    static const char *sim_cfg_names[] = {
        "", "Ramp Time", "Thermal Mass", "Conductance", "Heater Power", "", "", "",                             // bits 0-7 (PHY)
        "", "Sensor latency", "Target Temp", "Critical Temp", "Cooldown Temp", "Hysteresis", "Ambient Temp", "" // bits 8-15 (TEMP)
    };

    char      msg[192] = "[SKIP] Out of Range: ";
    int       n = 21;
    uint16_t  oor_mask = ret;
    const int last_bit = HIGHEST_BIT(oor_mask);

    for (int i = 1; i <= last_bit; i++) {
        if (!(oor_mask & (1U << i)) || sim_cfg_names[i][0] == '\0')
            continue;
        int m = snprintf(msg + n, sizeof(msg) - n, "%s, ", sim_cfg_names[i]);
        if (m > 0)
            n += m;
        if (n >= (int)sizeof(msg) - 1)
            break;
    }

    if (n > 23) {
        msg[n - 2] = '\0';
        embeddedCliPrint(cli, msg);
    }
}

void on_set_sim_command(EmbeddedCli *cli, char *args, int count)
{
    const char *arg = embeddedCliGetToken(args, 2); // Guaranteed to be "phy" or "temp"
    bool        temp = (strcmp(arg, "temp") == 0);
    int         ret = 0;
    uint16_t    oor_mask = 0;
    uint16_t    cfg_mask = 0;
    uint8_t     hdr = 0;
    uint16_t    val;
    stream_t    pkt[2] = {0};

    if (temp) {
        // tokens 3 to 7 map to the 5 temp args
        for (int i = 0; i < 5 && (i + 3 <= count); i++) {
            arg = embeddedCliGetToken(args, i + 3);
            val = (uint16_t)atoi(arg);
            if (val < 256) {
                pkt[1].u8[i + 1] = (uint8_t)val;
                hdr |= BIT(i + 1);
            } else {
                oor_mask |= BIT2(i + 1);
            }
        }
        cfg_mask = (uint16_t)hdr << 8;
        pkt[0].u8[0] = BIT(0); // Continue bit
        pkt[1].u8[0] = hdr;
        ret = set_sim_cfg(2, pkt) | oor_mask;
    } else {
        arg = embeddedCliGetToken(args, 3);
        val = (uint16_t)atoi(arg);
        if (val < 256) {
            pkt[0].u8[1] = (uint8_t)val;
            hdr |= BIT(1);
        } else {
            oor_mask |= BIT(1);
        }

        for (int i = 1; i < 5 && (i + 3 <= count); i++) {
            arg = embeddedCliGetToken(args, i + 3);
            val = (uint16_t)atoi(arg);
            if (val) {
                pkt[0].u16[i] = val;
                hdr |= BIT(i + 1);
            }
        }

        cfg_mask = (uint16_t)hdr;
        pkt[0].u8[0] = hdr;

        // Special handling for the 5th PHY arg (ambient) if count == 7
        if (count == 7) {
            arg = embeddedCliGetToken(args, 7);
            val = (uint16_t)atoi(arg);
            pkt[1].u16[3] = val;
            pkt[0].u8[0] |= BIT(0); // mark continue
            pkt[1].u8[0] = BIT(6);  // mark ambient field present
            cfg_mask |= BIT2(6);
            ret = set_sim_cfg(2, pkt);
        } else {
            ret = set_sim_cfg(1, pkt);
        }
    }

    if (ret > 0)
        sim_cfg_print_oor(cli, ret);
    if ((cfg_mask & ~(uint16_t)ret) != 0)
        embeddedCliPrint(cli, "Sim config set");
}

void cli_log_cmd_init(void)
{
    const char *name = nullptr;

    memset(domain_map, MAP_EMPTY, sizeof(domain_map));
    memset(entity_map, MAP_EMPTY, sizeof(entity_map));
    memset(level_map, MAP_EMPTY, sizeof(level_map));

    for (int did = 1; did < NUM_DOMAINS; did++) {
        if (!(dom_map_en & BIT(did)))
            continue;
        name = domain_name((uint8_t)did, 1);
        insert_name(name, did, TYPE_DOMAIN, did - 1);
    }

    for (int eid = 0; eid < NUM_ENTITIES; eid++) {
        if (!(ent_map_en & BIT(eid)))
            continue;
        name = entity_name((uint8_t)eid, 1);
        insert_name(name, eid, TYPE_ENTITY, eid);
    }

    for (int i = 0; i < NUM_LOG_LEVELS; i++) {
        name = level_name((uint8_t)level_id[i], 1);
        insert_name(name, level_id[i], TYPE_LOG_LEVEL, i);
    }
}

void on_set_log_command(EmbeddedCli *cli, char *args, int count)
{
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

    for (int d = 1; d < NUM_DOMAINS; d++) {
        if (!all_domains && d != did)
            continue;
        for (int e = 0; e < NUM_ENTITIES; e++) {
            if (!all_entities && e != eid)
                continue;
            log_set_level(d, e, level);
        }
    }
    embeddedCliPrint(cli, "Log level set");
}

void on_set_command(EmbeddedCli *cli, char *args, void *context)
{
    cmd_dispatch(cli, args, (args == nullptr) ? 0 : embeddedCliGetTokenCount(args), &set_cmd_desc);
}

void on_show_command(EmbeddedCli *cli, char *args, void *context)
{
    cmd_dispatch(cli, args, (args == nullptr) ? 0 : embeddedCliGetTokenCount(args), &show_cmd_desc);
}

void set_system_commands(EmbeddedCli *cli)
{
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "show",
            .flags = BINDING_FLAG_TOKENIZE_ARGS | BINDING_FLAG_HAS_HELP | BINDING_FLAG_WIDE,
            .binding = on_show_command, // on show command
        }
    );
    embeddedCliAddCompletion("show", show_cmd_completion);
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "set",
            .flags = BINDING_FLAG_TOKENIZE_ARGS | BINDING_FLAG_HAS_HELP | BINDING_FLAG_WIDE,
            .binding = on_set_command, // on set command
        }
    );
    embeddedCliAddCompletion("set", set_cmd_completion);
#ifdef ENABLE_RTOS
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "start",
            .flags = BINDING_FLAG_WIDE,
            .binding = on_start_hw_sim, // on starting HW simulation
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "end",
            .flags = BINDING_FLAG_WIDE,
            .binding = on_end_hw_sim, // on ending HW simulation
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "pause",
            .flags = BINDING_FLAG_WIDE,
            .binding = on_pause, // on pausing
        }
    );
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "resume",
            .flags = BINDING_FLAG_WIDE,
            .binding = on_resume, // on resuming
        }
    );
#endif
}
