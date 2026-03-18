/**
 * @file cli.h
 * @brief CLI state management and context tracking.
 *
 * This include defines the structures and types used to manage the CLI's
 * operational modes, command visibility, and execution state.
 *
 * NOTE: This include uses native C language types to eliminate
 * dependencies on standard C runtime library headers.
 */
#pragma once

/**
 * @brief Operational modes for managing state of the CLI.
 */
typedef enum {
    NONE = 0,
    MAIN,
    SYSTEM,
    TEST,
    TEST_DEV,
    TEST_SYS,
} cli_mode_e;

#define TEST_INVALID (-1)
typedef struct cli_flags_s {
    int write_enable : 1; // Controls whether the CLI should print output (e.g., disable when waiting for user to press
                          // Enter)
    int no_error : 1;     // Indicates whether the last operation resulted in an error. This is used to determine if we should
                          // show a generic "Invalid selection" message when the user presses Enter without a valid command.
    int cmd_run : 1;      // Indicates whether the universal command was executed in test menu context
} cli_flags_t;

// CLI Data for managing state and messages
typedef struct cli_data_s {
    char          msg_buf[128]; // Buffer for storing messages to be displayed on the CLI after a test execution or error
    unsigned char bindings[8];  // Context specific binding indices registered as the App Context by embeddedCliAddAppContext()
                                // These indices are referenced by a bitmask into embeddedCliSetAppContext() to
                                // toggle command visibility based on the current menu
    const struct test_registry_s *registry;

    cli_flags_t flags; // CLI status and execution flags
    int         mode;  // Current mode of the CLI (e.g., Main Menu, System Mode, Test Mode)
    int         test;  // Current 'test set' index. MAX_BLOCK_INDEX indicates we are in the top dev test menu
} cli_data_t;

void              cli_clear_menu_region(void);
extern cli_data_t cli_data; // Global instance to hold CLI state and messages
extern const int  max_index_sys;
extern const int  max_index_dev;

static inline int get_max_index(void)
{
    return (cli_data.mode == TEST_SYS) ? max_index_sys : max_index_dev;
}