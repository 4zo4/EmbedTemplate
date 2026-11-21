/**
 * @file cli_dispatch.c
 * @brief Generic Multi-Level CLI Command Dispatcher and Autocompletion Engine
 *
 * Implements a declarative, tree-based routing and TAB-completion engine for
 * nested sub-commands, positional arguments, and dynamic context-aware help (`?`).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "embedded_cli.h"
#include "cli_dispatch.h"
// #define DEBUG // Uncomment to enable breadcrumb tracking
#include "utils.h" // for BIT

void cmd_in_test_mode(EmbeddedCli *cli);

const char *error = "[ERROR] Too many arguments";
const char *invalid = "[ERROR] Invalid selection";
const char *single[1] = {nullptr};

comp_spec_t     *specs;
args_validate_t *specs_valid;
uint16_t         specs_size;
uint16_t         specs_valid_size;

static int find_index_by(const char **list, const char *str)
{
    if (!list || !str)
        return -1;
    for (int i = 0; list[i] != nullptr; i++) {
        if (strcmp(list[i], str) == 0)
            return i;
    }
    return -1;
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

static void do_help(EmbeddedCli *cli, const char **options, int usage, const char *prefix)
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

void cmd_arg_completion_dispatch(EmbeddedCli *cli, const char *token, uint8_t pos, const cmd_comp_t *comp, const uint8_t *action_idx)
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

        int l1_idx = find_index_by(comp->level1, arg1);

        if (l1_idx != -1) {
            const char **l2_subcmd = comp->level2[l1_idx];
            int          l2_idx = find_index_by(l2_subcmd, arg2);

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

void cmd_dispatch(EmbeddedCli *cli, char *args, int count, const cmd_comp_desc_t *desc)
{
    cmd_in_test_mode(cli);
    char msg[128];

    const char *last = (count > 0) ? embeddedCliGetToken(args, count) : nullptr;
    bool        help = (last && strcmp(last, "?") == 0);

    if (count == 0 || (help && count == 1)) {
        do_help(cli, desc->comp.level1, !help, desc->comp.name);
        return;
    }

    DEF_BREADCRUMB(breadcrumb, 8);
    const char *arg1 = embeddedCliGetToken(args, 1);
    int         l1_idx = find_index_by(desc->comp.level1, arg1);
    if (l1_idx == -1)
        return (void)embeddedCliPrint(cli, invalid);

    int          act_idx = desc->act_idx ? desc->act_idx[l1_idx + 1] : (l1_idx + 1);
    const char **l2_subcmd = desc->comp.level2[l1_idx];
    bool         has_l2 = (l2_subcmd && l2_subcmd[0] && l2_subcmd[0][0] != '\0');

    const char *arg2 = (count >= 2) ? embeddedCliGetToken(args, 2) : nullptr;
    int         l2_idx = -1;
    bool        is_hybrid = (l2_subcmd && l2_subcmd[0] && l2_subcmd[0][0] == '\0');

    if (arg2 && !(help && count == 2))
        l2_idx = find_index_by(l2_subcmd, arg2);

    if (l2_idx == -1 && is_hybrid && !arg2)
        l2_idx = 0;

    ADD_BREADCRUMB(breadcrumb, 0);
    if (!arg2 || (help && count == 2)) {
        if (!has_l2 && desc->comp.spec_idx == 0 && !help)
            goto action_run;

        ADD_BREADCRUMB(breadcrumb, 1);
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
    if (desc->comp.spec_idx != 0 || (desc->opt_mask.enable & BIT(act_idx)))
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

        ADD_BREADCRUMB(breadcrumb, 2);
        goto action_run;
    }

    const char *arg3 = (count >= 3) ? embeddedCliGetToken(args, 3) : nullptr;

    if (help || (l3_opts && !arg3 && (desc->opt_mask.must & BIT(act_idx)))) {
        int n = snprintf(msg, sizeof(msg), "%s %s", desc->comp.name, arg1);
        if (arg2 && l2_idx != -1)
            snprintf(msg + n, sizeof(msg) - n, " %s", arg2);
        do_help(cli, l3_opts, l3_opts != nullptr ? 2 : 0, msg); // Usage: [a|b|c]
        return;
    }

    if (arg3 && !l3_opts)
        return (void)embeddedCliPrint(cli, error);

    if (arg3 && l3_opts && find_index_by(l3_opts, arg3) == -1)
        return (void)embeddedCliPrint(cli, invalid);

    // Final check for unexpected 4th token in standard commands
    if (count > (l3_opts ? 3 : 2) && !help)
        return (void)embeddedCliPrint(cli, error);

action_run:
    ADD_BREADCRUMB(breadcrumb, 3);
    if (desc->action[act_idx])
        desc->action[act_idx](cli, args, count);
}
