/**
 * @file cli_system_diag.c
 * @brief CLI System diagnostic and configuration commands
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "embedded_cli.h"
#include "cli_dispatch.h"
#include "cli_show.h"
#include "utils.h" // for BIT

// prototypes without include file
void on_set_log_cmd(EmbeddedCli *cli, char *args, int count);
void set_log_completion(EmbeddedCli *cli, const char *token, uint8_t pos);
bool set_log_validate(const char *token, uint8_t pos);
void on_set_sim_cmd(EmbeddedCli *cli, char *args, int count);

/**
 * @brief Global table for specialized argument completion logic.
 * Index 0 is reserved for 'no-op' (static data default).
 * Match via cmd_comp_t.spec_idx.
 */
#define DEF_SPECS_SIZE (sizeof(set_cmd_specs) / sizeof(comp_spec_t))
static comp_spec_t set_cmd_specs[] = {
    {0, nullptr           }, // Default: No special handling
    {1, set_log_completion}, // Custom: 'set log <domain> <entity> <level>'
};

#define DEF_SPECS_VALID_SIZE (sizeof(set_cmd_specs_valid) / sizeof(args_validate_t))
static args_validate_t set_cmd_specs_valid[] = {
    nullptr,          // [0] Reserved
    set_log_validate, // [1] Index 1: 'set log' validation
    nullptr,          // [2] Index 2: 'set sim' (No validation)
};

static inline void set_cmd_special_handling(void)
{
    specs = set_cmd_specs;
    specs_valid = set_cmd_specs_valid;
    specs_size = DEF_SPECS_SIZE;
    specs_valid_size = DEF_SPECS_VALID_SIZE;
}

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
#ifdef ENABLE_SIM
static const char  *set_cmd_comp_1[] = {"log", "sim", nullptr};    // Level 1: Sub-commands
static const char  *set_cmd_comp_1_1[] = {"", nullptr};            // Level 2: 'log' (handled by spec_idx 1)
static const char  *set_cmd_comp_1_2[] = {"phy", "temp", nullptr}; // Level 2: 'sim' options 'set sim phy' | 'set sim temp'
static const char **set_cmd_compos[] = {
    set_cmd_comp_1_1, // 'set log' path
    set_cmd_comp_1_2, // 'set sim' path
    single,
};
#else
static const char  *set_cmd_comp_1[] = {"log", nullptr}; // Level 1: Sub-commands
static const char  *set_cmd_comp_1_1[] = {"", nullptr};  // Level 2: 'log' (handled by spec_idx 1)
static const char **set_cmd_compos[] = {
    set_cmd_comp_1_1, // 'set log' path
};
#endif

#define SET_LOG (1)
#define SET_SIM (SET_LOG + L2_SLOTS(set_cmd_comp_1_1))

static const char *set_cmd_log_args[] = {"domain", "entity", "level", nullptr};
static const char *set_cmd_sim_phy_args[] = {"ramp", "mass", "cond", "power", "ambient", nullptr};
static const char *set_cmd_sim_temp_args[] = {"latency", "target", "critical", "hyster", "cooldown", nullptr};

// Level 3 Options:
// These act as POSITIONAL HINTS because spec_idx is non-zero.
static const comp_opt_t set_cmd_options[] = {
    {SET_LOG,     set_cmd_log_args     }, // 'set log'
#ifdef ENABLE_SIM
    {SET_SIM,     set_cmd_sim_phy_args }, // 'set sim phy'
    {SET_SIM + 1, set_cmd_sim_temp_args}, // 'set sim temp'
#endif
};

static const cmd_comp_t set_cmd_comp = {
    .name = "set",
    .level1 = set_cmd_comp_1,
    .level2 = set_cmd_compos,
    .level3 = set_cmd_options,
    .opt_size = (uint16_t)(sizeof(set_cmd_options) / sizeof(comp_opt_t)),
    .spec_idx = 1 // Points to set_log_completion in 'specs' table
};

static void (*set_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    nullptr,        // [0] Reserved
    on_set_log_cmd, // [1] sel log <pos args>
#ifdef ENABLE_SIM
    on_set_sim_cmd, // [2] set sim phy <pos args>
    on_set_sim_cmd, // [3] set sim temp <pos args>
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

void set_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    set_cmd_special_handling();
    cmd_arg_completion_dispatch(cli, token, pos, &set_cmd_comp, nullptr);
}

void on_set_command(EmbeddedCli *cli, char *args, void *context)
{
    cmd_dispatch(cli, args, (args == nullptr) ? 0 : embeddedCliGetTokenCount(args), &set_cmd_desc);
}

/**
 * @brief Auto-completion tree for the 'show' command.
 * Structure: Two-Level Offset-Mapped Tree.
 * - Horizontal: Add sub-commands to level1.
 * - Vertical: Level 2 provides arguments for Level 1 sub-commands.
 * - Note: Level 3 (options) and Specs are indexed via action_idx calculation.
 */
static const char *show_cmd_comp_1[] = {"stats", "config", "version", "domains", "entities", "levels", nullptr};
#ifdef ENABLE_PROF
static const char *show_cmd_comp_1_1[] = {"log", "sys", "task", "prof", nullptr}; // 'show stats ...'
#else
static const char *show_cmd_comp_1_1[] = {"log", "sys", "task", nullptr}; // 'show stats ...'
#endif
#ifdef ENABLE_SIM
static const char *show_cmd_comp_1_2[] = {"", "short", "sim", "log", nullptr}; // 'show config ...'
#else
static const char *show_cmd_comp_1_2[] = {"", "short", "log", nullptr}; // 'show config ...'
#endif
static const char **show_cmd_compos[] = {
    show_cmd_comp_1_1, // Sub-args for 'stats'
    show_cmd_comp_1_2, // Sub-args for 'config'
    single,            // No sub-args for 'version'
    single,            // No sub-args for 'domains'
    single,            // No sub-args for 'entities'
    single,            // No sub-args for 'levels'
};

#define SHOW_STATS (0)
#define SHOW_CONFIG (SHOW_STATS + L2_SLOTS(show_cmd_comp_1_1))
#define SHOW_VERSION (SHOW_CONFIG + L2_SLOTS(show_cmd_comp_1_2))
#define SHOW_DOMAINS (SHOW_VERSION + L2_SLOTS(single))
#define SHOW_ENTITIES (SHOW_DOMAINS + L2_SLOTS(single))
#define SHOW_LEVELS (SHOW_ENTITIES + L2_SLOTS(single))

/**
 * @brief Level 3 static options mapping.
 * Matches 1D action index to a list of optional terminal arguments.
 */
static const char *options_stats_log[] = {"clear", nullptr};
#ifdef ENABLE_SIM
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
static const opt_mask_t show_cmd_opt_mask = {
    .enable = SHOW_OPTS,
    .must = SHOW_OPTS & ~BIT(SHOW_STATS), // 'show stats log' doesn't require options
};

/**
 * @brief Execution handlers mapped to the 1D Action Index.
 */
static void (*show_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    show_stats_log,  // [0] show stats log [clear]
    show_stats_sys,  // [1] show stats sys
    show_stats_task, // [2] show stats task
#ifdef ENABLE_PROF
    show_stats_prof, // [3] show stats prof
#endif
    show_config,       // [3] show config (hybrid: leaf + path)
    show_config_short, // [4] show config short
#ifdef ENABLE_SIM
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
        .level1 = show_cmd_comp_1,
        .level2 = show_cmd_compos,
        .level3 = show_cmd_options,
        .opt_size = (uint16_t)(sizeof(show_cmd_options) / sizeof(comp_opt_t)),
        .spec_idx = 0, // No special arg completion logic
    },
    // clang-format on
    .act_idx = show_cmd_action_idx,
    .opt_mask = show_cmd_opt_mask,
    .action = show_cmd_action,
};

void show_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    cmd_arg_completion_dispatch(cli, token, pos, &show_cmd_desc.comp, show_cmd_desc.act_idx);
}

void on_show_command(EmbeddedCli *cli, char *args, void *context)
{
    cmd_dispatch(cli, args, (args == nullptr) ? 0 : embeddedCliGetTokenCount(args), &show_cmd_desc);
}

void set_diagnostic_and_config_commands(EmbeddedCli *cli)
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
}
