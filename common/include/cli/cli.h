#pragma once

// CLI modes for managing state of the CLI
typedef enum {
    NONE = 0,
    MAIN,
    SYSTEM,
    TEST,
    TEST_DEV,
    TEST_SYS,
} cli_mode_e;

// CLI Data for managing state and messages
typedef struct cli_data_s {
    char msg_buf[128]; // Buffer for storing messages to be displayed on the CLI after a test execution or error

    const struct test_registry_s *registry;

    int  mode;         // Current mode of the CLI (e.g., Main Menu, System Mode, Test Mode)
    int  test;         // Current test set index. MAX_BLOCK_INDEX indicates we are in the top dev test menu
    bool write_enable; // Controls whether the CLI should print output (e.g., disable when waiting for user to press
                       // Enter)
    bool no_error;     // Indicates whether the last operation resulted in an error. This is used to determine if we should
                       // show a generic "Invalid selection" message when the user presses Enter without a valid command.
} cli_data_t;

void              cli_clear_menu_region(void);
extern cli_data_t cli_data; // Global instance to hold CLI state and messages
extern const int  max_index_sys;
extern const int  max_index_dev;

static inline int get_max_index(void)
{
    return (cli_data.mode == TEST_SYS) ? max_index_sys : max_index_dev;
}