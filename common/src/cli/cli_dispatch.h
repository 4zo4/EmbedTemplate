#pragma once

#ifndef EMBEDDED_CLI_H
#error "embedded_cli.h must precede this include"
#endif

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

typedef struct opt_mask_s {
    uint16_t enable; // bitmask identifying if options are enabled for the command
    uint16_t must;   // bitmask identifying if options are required for the command
} opt_mask_t;

typedef struct cmd_comp_desc_s {
    cmd_comp_t       comp;
    const uint8_t   *act_idx;  // command's action index table
    const opt_mask_t opt_mask; // options mask

    void (**action)(EmbeddedCli *cli, char *args, int count);
} cmd_comp_desc_t;

// helper counting macros
#define COUNT_L2(str_array) (sizeof(str_array) / sizeof(char *) - 1)
#define L2_SLOTS(str_array) (COUNT_L2(str_array) == 0 ? 1 : COUNT_L2(str_array))
#define SPECS_SIZE specs_size
#define SPECS_VALID_SIZE specs_valid_size

void cmd_dispatch(EmbeddedCli *cli, char *args, int count, const cmd_comp_desc_t *desc);
void cmd_arg_completion_dispatch(EmbeddedCli *cli, const char *token, uint8_t pos, const cmd_comp_t *comp, const uint8_t *action_idx);

extern const char      *single[1];
extern comp_spec_t     *specs;
extern args_validate_t *specs_valid;
extern uint16_t         specs_size;
extern uint16_t         specs_valid_size;
