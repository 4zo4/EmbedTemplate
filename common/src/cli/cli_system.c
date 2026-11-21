/**
 * @file cli_system.c
 * @brief CLI System Runner
 * Provides a command-line interface for system-level commands.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block_id.h" // for MAX_BLOCK_INDEX
#include "cli.h"
#include "embedded_cli.h"
#include "cli_dispatch.h"
#include "utils.h" // for BIT

// prototypes without include file
void clear_msg(void);
void set_msg(const char *msg);
void print_msg(EmbeddedCli *cli);
void ble_start(void);
void ble_suspend(void);
void pci_start(void);
void pci_suspend(void);
void pci_test_start(void);
void sim_start(void);
void sim_suspend(void);
bool sim_pause(void);
void sim_resume(void);
void cmd_in_test_mode(EmbeddedCli *cli);
void set_diagnostic_and_config_commands(EmbeddedCli *cli);

#ifdef ENABLE_SIM
#define NUM_SIM_APP_CONTEXT 4
#define MAP_SIM_APP_CONTEXT 0xF
static uint8_t sim_ctx[8]; // simulation application context binding ids
#endif

void show_main_sys_menu(EmbeddedCli *cli)
{
    cli_clear_menu_region();
    embeddedCliSetAppContext(0x0);
    // clang-format off
    const char *msg =
        "\rSystem Commands:\r\n"
        " set - Set log level\r\n"
        " show - Show status\r\n"
#ifdef ENABLE_BLE
        " ble - BLE Host\r\n"
#endif
#ifdef ENABLE_SIM
        " sim - HW Simulation Control\r\n"
#endif
#ifdef ENABLE_PCI
        " pci - PCI Control\r\n"
#endif
        "Usage: <name> (with TAB), 'quit' or 'q' to exit, 'back' to previous menu";
    // clang-format on
    embeddedCliPrint(cli, msg);
}

void show_sim_menu(EmbeddedCli *cli)
{
#ifdef ENABLE_SIM
    cli_clear_menu_region();
    embeddedCliAddAppContext(sim_ctx, NUM_SIM_APP_CONTEXT);
    embeddedCliSetAppContext(MAP_SIM_APP_CONTEXT);
    // clang-format off
    const char *msg =
        "\rSimulation Control Commands:\r\n"
        " start - Start HW Simulation\r\n"
        " stop - Stop HW Simulation\r\n"
        " pause - Pause HW Simulation\r\n"
        " resume - Resume HW Simulation\r\n"
        "Usage: <name> (with TAB), 'quit' or 'q' to exit, 'back' to previous menu";
    // clang-format on
    embeddedCliPrint(cli, msg);
#endif
}

void on_system_menu(EmbeddedCli *cli, char *args, void *context)
{
    clear_msg();
    cli_data.mode = SYSTEM;
    cli_data.test = MAX_BLOCK_INDEX;
    cli_data.flags.write_enable = true;
    show_main_sys_menu(cli);
}

void on_sim_menu(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_SIM
    clear_msg();
    cli_data.mode = SIMULATION;
    cli_data.test = MAX_BLOCK_INDEX;
    cli_data.flags.write_enable = true;
    show_sim_menu(cli);
#endif
}

static void on_sim_start(EmbeddedCli *cli, char *args, int count);
void        on_start_hw_sim(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_SIM
    cmd_in_test_mode(cli);
    // Start hardware simulation
    on_sim_start(cli, args, 0);
#endif
}

static void on_sim_stop(EmbeddedCli *cli, char *args, int count);
void        on_stop_hw_sim(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_SIM
    cmd_in_test_mode(cli);
    // Suspend hardware simulation
    on_sim_stop(cli, args, 0);
#endif
}

void on_pause(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_SIM
    cmd_in_test_mode(cli);
    // Pause hardware simulation
    sim_pause();
#endif
}

void on_resume(EmbeddedCli *cli, char *args, void *context)
{
#ifdef ENABLE_SIM
    cmd_in_test_mode(cli);
    // Resume hardware simulation
    sim_resume();
#endif
}

#ifdef ENABLE_BLE
static bool ble_running = false;

static void on_ble_start(EmbeddedCli *cli, char *args, int count)
{
    ble_start();
    ble_running = true;
}

static void on_ble_stop(EmbeddedCli *cli, char *args, int count)
{
    ble_suspend();
    ble_running = false;
}

static const char  *ble_cmd_compo_init[] = {"start", nullptr};
static const char  *ble_cmd_compo_run[] = {"stop", nullptr};
static const char **ble_cmd_compos[] = {single, single};

static const uint8_t ble_cmd_idx_init[] = {0, 1};
static const uint8_t ble_cmd_idx_run[] = {0, 2};

static void (*ble_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    nullptr,      // [0] Reserved
    on_ble_start, // [1] 'ble start'
    on_ble_stop,  // [2] 'ble stop'
};

static const cmd_comp_desc_t ble_cmd_desc_init = {
    .comp = {
             .name = "ble",
             .level1 = ble_cmd_compo_init,
             .level2 = ble_cmd_compos,
             .level3 = nullptr,
             .opt_size = 0,
             .spec_idx = 0,
             },
    .act_idx = ble_cmd_idx_init,
    .action = ble_cmd_action,
};

static const cmd_comp_desc_t ble_cmd_desc_run = {
    .comp = {
             .name = "ble",
             .level1 = ble_cmd_compo_run,
             .level2 = ble_cmd_compos,
             .level3 = nullptr,
             .opt_size = 0,
             .spec_idx = 0,
             },
    .act_idx = ble_cmd_idx_run,
    .action = ble_cmd_action,
};

static inline const cmd_comp_desc_t *get_ble_desc(void)
{
    return ble_running ? &ble_cmd_desc_run : &ble_cmd_desc_init;
}

void ble_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    cmd_arg_completion_dispatch(cli, token, pos, &get_ble_desc()->comp, nullptr);
}

void on_ble_command(EmbeddedCli *cli, char *args, void *context)
{
    cmd_dispatch(cli, args, (args == nullptr) ? 0 : embeddedCliGetTokenCount(args), get_ble_desc());
}
#endif // ENABLE_BLE

#ifdef ENABLE_PCI
static void on_pci_test_cmd(EmbeddedCli *cli, char *args, int count)
{
    pci_test_start();
}

#ifdef ENABLE_RTOS
static bool pci_running = false;

static void on_pci_start_cmd(EmbeddedCli *cli, char *args, int count)
{
    pci_start();
    pci_running = true;
}

static void on_pci_stop_cmd(EmbeddedCli *cli, char *args, int count)
{
    pci_suspend();
    pci_running = false;
}

static const char  *pci_cmd_compo_init[] = {"start", nullptr};
static const char  *pci_cmd_compo_run[] = {"stop", "test", nullptr};
static const char **pci_cmd_compos[] = {single, single};

static const uint8_t pci_cmd_idx_init[] = {0, 1};
static const uint8_t pci_cmd_idx_run[] = {0, 2, 3};

static void (*pci_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    nullptr,          // [0] Reserved
    on_pci_start_cmd, // [1] 'pci start'
    on_pci_stop_cmd,  // [2] 'pci stop'
    on_pci_test_cmd,  // [3] 'pci test'
};

static const cmd_comp_desc_t pci_cmd_desc_init = {
    .comp = {
             .name = "pci",
             .level1 = pci_cmd_compo_init,
             .level2 = pci_cmd_compos,
             .level3 = nullptr,
             .opt_size = 0,
             .spec_idx = 0,
             },
    .act_idx = pci_cmd_idx_init,
    .action = pci_cmd_action,
};

static const cmd_comp_desc_t pci_cmd_desc_run = {
    .comp = {
             .name = "pci",
             .level1 = pci_cmd_compo_run,
             .level2 = pci_cmd_compos,
             .level3 = nullptr,
             .opt_size = 0,
             .spec_idx = 0,
             },
    .act_idx = pci_cmd_idx_run,
    .action = pci_cmd_action,
};

static inline const cmd_comp_desc_t *get_pci_desc(void)
{
    return pci_running ? &pci_cmd_desc_run : &pci_cmd_desc_init;
}

#else  // !ENABLE_RTOS (Bare-metal event loop: 'pci test' only)

static const char   *pci_cmd_compo_bm[] = {"test", nullptr};
static const char  **pci_cmd_compos[] = {single};
static const uint8_t pci_cmd_idx_bm[] = {0, 1};

static void (*pci_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    nullptr,         // [0] Reserved
    on_pci_test_cmd, // [1] 'pci test'
};

static const cmd_comp_desc_t pci_cmd_desc_bm = {
    .comp = {
             .name = "pci",
             .level1 = pci_cmd_compo_bm,
             .level2 = pci_cmd_compos,
             .level3 = nullptr,
             .opt_size = 0,
             .spec_idx = 0,
             },
    .act_idx = pci_cmd_idx_bm,
    .action = pci_cmd_action,
};

static inline const cmd_comp_desc_t *get_pci_desc(void)
{
    return &pci_cmd_desc_bm;
}
#endif // ENABLE_RTOS

void pci_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    cmd_arg_completion_dispatch(cli, token, pos, &get_pci_desc()->comp, nullptr);
}

void on_pci_command(EmbeddedCli *cli, char *args, void *context)
{
    cmd_dispatch(cli, args, (args == nullptr) ? 0 : embeddedCliGetTokenCount(args), get_pci_desc());
}
#endif // BARE_METAL && ENABLE_PCI

#ifdef ENABLE_SIM
static bool sim_running = false;

static void on_sim_start(EmbeddedCli *cli, char *args, int count)
{
    sim_start();
    sim_running = true;
}

static void on_sim_stop(EmbeddedCli *cli, char *args, int count)
{
    sim_suspend();
    sim_running = false;
}

static void on_sim_cmd(EmbeddedCli *cli, char *args, int count)
{
    on_sim_menu(cli, args, nullptr);
}

static const char  *sim_cmd_compo_init[] = {"", "start", nullptr};
static const char  *sim_cmd_compo_run[] = {"", "stop", nullptr};
static const char **sim_cmd_compos[] = {
    single, // No L2 args for sole 'sim'
    single, // No L2 args for 'sim start' / 'sim stop'
};

static const uint8_t sim_cmd_idx_init[] = {0, 1, 2};
static const uint8_t sim_cmd_idx_run[] = {0, 1, 3};

static void (*sim_cmd_action[])(EmbeddedCli *cli, char *args, int count) = {
    nullptr,      // [0] Reserved
    on_sim_cmd,   // [1] sole 'sim' -> opens sim menu
    on_sim_start, // [2] 'sim start'
    on_sim_stop,  // [3] 'sim stop'
};

static const cmd_comp_desc_t sim_cmd_desc_init = {
    .comp = {
             .name = "sim",
             .level1 = sim_cmd_compo_init,
             .level2 = sim_cmd_compos,
             .level3 = nullptr,
             .opt_size = 0,
             .spec_idx = 0,
             },
    .act_idx = sim_cmd_idx_init,
    .action = sim_cmd_action,
};

static const cmd_comp_desc_t sim_cmd_desc_run = {
    .comp = {
             .name = "sim",
             .level1 = sim_cmd_compo_run,
             .level2 = sim_cmd_compos,
             .level3 = nullptr,
             .opt_size = 0,
             .spec_idx = 0,
             },
    .act_idx = sim_cmd_idx_run,
    .action = sim_cmd_action,
};

static inline const cmd_comp_desc_t *get_sim_desc(void)
{
    return sim_running ? &sim_cmd_desc_run : &sim_cmd_desc_init;
}

void sim_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos)
{
    cmd_arg_completion_dispatch(cli, token, pos, &get_sim_desc()->comp, nullptr);
}

void on_sim_command(EmbeddedCli *cli, char *args, void *context)
{
    if (args == nullptr) {
        on_sim_menu(cli, args, context);
        return;
    }
    cmd_dispatch(cli, args, embeddedCliGetTokenCount(args), get_sim_desc());
}
#endif // ENABLE_SIM

void set_system_commands(EmbeddedCli *cli)
{
    uint16_t bid;

    set_diagnostic_and_config_commands(cli);

#ifdef ENABLE_BLE
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "ble",
            .flags = BINDING_FLAG_TOKENIZE_ARGS | BINDING_FLAG_HAS_HELP | BINDING_FLAG_WIDE,
            .binding = on_ble_command, // on ble command
        }
    );
    embeddedCliAddCompletion("ble", ble_cmd_completion);
#endif // ENABLE_BLE

#ifdef ENABLE_SIM
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "sim",
            .flags = BINDING_FLAG_TOKENIZE_ARGS | BINDING_FLAG_HAS_HELP | BINDING_FLAG_WIDE,
            .binding = on_sim_command, // on sim command
        }
    );
    embeddedCliAddCompletion("sim", sim_cmd_completion);
    bid = embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "start",
            .flags = BINDING_FLAG_HIDDEN | BINDING_FLAG_APP_CONTEXT,
            .binding = on_start_hw_sim, // on starting HW simulation
        }
    );
    sim_ctx[0] = bid;
    bid = embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "stop",
            .flags = BINDING_FLAG_HIDDEN | BINDING_FLAG_APP_CONTEXT,
            .binding = on_stop_hw_sim, // on stoping HW simulation
        }
    );
    sim_ctx[1] = bid;
    bid = embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "pause",
            .flags = BINDING_FLAG_HIDDEN,
            .binding = on_pause, // on pausing HW simulation
        }
    );
    sim_ctx[2] = bid;
    bid = embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "resume",
            .flags = BINDING_FLAG_HIDDEN,
            .binding = on_resume, // on resuming HW simulation
        }
    );
    sim_ctx[3] = bid;
#endif // ENABLE_SIM

#if defined(ENABLE_PCI)
    embeddedCliAddBinding(
        cli,
        (CliCommandBinding){
            .name = "pci",
            .flags = BINDING_FLAG_TOKENIZE_ARGS | BINDING_FLAG_HAS_HELP | BINDING_FLAG_WIDE,
            .binding = on_pci_command, // on pci command
        }
    );
    embeddedCliAddCompletion("pci", pci_cmd_completion);
#endif // ENABLE_PCI
}
