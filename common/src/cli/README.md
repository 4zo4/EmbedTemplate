## Advanced Three-Tier Command Architecture

The CLI architecture embraces a **Single Source of Truth** design. Command names appear only once in the code as `const char *name` types, whether they are part of application objects or engine-level bindings.

The engine uses an optimized `getAutocompletedCommand()` function to implement a prioritized, three-tier search strategy to find matching commands:

1.  **Dynamic Object Context**: Scans the dynamic UI context set by `embeddedCliSetContext()` (e.g., current test names).
2.  **Application Context**: Scans the indexed list set by `embeddedCliAddAppContext()`, applying a bitmask via `embeddedCliSetAppContext()` to toggle visibility based on menu state.
3.  **Global/Wide Context**: A fallback scan of global commands (e.g., `help`, `quit`) that are always available.

---

### 1. Global/Wide Context
Global commands are identified by the `BINDING_FLAG_WIDE` flag. These are accessible from any menu level.

```text
embeddedCliAddBinding(cli, (CliCommandBinding){
    .name = "quit",
    .flags = BINDING_FLAG_WIDE,
    .binding = on_quit, 
});

// Alias 'q' also marked as Wide
embeddedCliAddBinding(cli, (CliCommandBinding){
    .name = "q",
    .flags = BINDING_FLAG_HIDDEN | BINDING_FLAG_WIDE,
    .binding = on_quit,
});
```

### 2. Application Context
This tier manages menu-specific commands. First, store the binding indices returned by `embeddedCliAddBinding` into your application state.

```text
typedef struct {
    unsigned char bindings[8];  // Application Context-specific binding indices
    const struct test_registry_s *registry;
    int mode;  // e.g., Main Menu, System Mode, Test Mode
} cli_data_t;

// Register the Indices
uint16_t bid = embeddedCliAddBinding(cli, (CliCommandBinding){
    "system", BINDING_FLAG_HIDDEN, NULL, on_system_menu
});
cli_data.bindings[0] = bid;

// Initialize the engine's AppContext with four binding indice
#define NUM_APP_CONTEXT 4
embeddedCliAddAppContext(cli_data.bindings, NUM_APP_CONTEXT);
```

Toggle Visibility via Bitmask:
Use `embeddedCliSetAppContext(mask)` to define which indexed commands are active in the current UI state.

```text
#define MAP_MAIN_MENU 0x03 // Activates bindings at index 0 and 1

void show_main_menu(EmbeddedCli *cli) {
    // Restricts App Context to only "system" and "test" commands
    embeddedCliSetAppContext(MAP_MAIN_MENU);
    embeddedCliPrint(cli, "Available Modes: test, system");
}
```

### 3. Dynamic Object Context
Use this for hierarchical data (like a test registry) where command names are properties of C objects rather than static bindings.

```text
void show_test_menu(EmbeddedCli *cli) {
    // 1. Clear App Context so only Wide commands and Object commands work
    embeddedCliSetAppContext(0x0);

    // 2. Point engine to the dynamic object array
    // getAutocompletedCommand() will now search registry[i].name
    embeddedCliSetContext(cli_data.registry, cli_data.test_count, sizeof(test_registry_t));

    // 3. Display the menu logic...
    embeddedCliPrint(cli, "Usage: <test_name> (TAB to complete)");
}
```

## Sub-commands & Argument Autocompletion
Unlike standard CLI completions that only match the first verb, this system uses a tree-based dispatcher to provide multi-level sub-commands, positional hints, and dynamic argument fetching.

### 1. Register the Completion Handler
The completion handler name must match the command binding name.
To enable intelligent TAB-completion for complex commands (e.g., `set log <domain> <entity> <level>`), you must register a Completion Handler and define a Completion Tree.

```text
// 1. Bind the command
embeddedCliAddBinding(cli, (CliCommandBinding){
    .name = "set",
    .flags = BINDING_FLAG_TOKENIZE_ARGS | // tokenize args for this command
             BINDING_FLAG_HAS_HELP | // this command has its own help
             BINDING_FLAG_WIDE, // this command is global 
    .binding = on_set_command,
});

// 2. Attach the completion logic
embeddedCliAddCompletion("set", set_cmd_completion);
```

### 2. The Dispatcher Pattern
The handler uses a generic dispatcher that traverses your completion tree based on the current cursor position (pos) and the current token.

```text
void set_cmd_completion(EmbeddedCli *cli, const char *token, uint8_t pos) {
    // Dispatches completion logic using the set_cmd_comp tree defined below
    do_cmd_arg_completion_dispatch(cli, token, pos, &set_cmd_comp, nullptr);
}
```

### 3. Defining the Completion Tree
The tree organizes sub-commands into three levels: Verbs, Sub-Verbs, and Positional Hints.

***Level 1 & 2: Structural Navigation***

```text
static const char *l1_set_cmd[] = {"log", "sim", nullptr};  // 'set log' or 'set sim'
static const char *l2_set_log[] = {"", nullptr};            // 'log' is a terminal
static const char *l2_set_sim[] = {"phy", "temp", nullptr}; // 'set sim phy' or 'set sim temp'
static const char **l2_set_cmd[] = {
    l2_set_log,  // Path for 'set log'
    l2_set_sim,  // Path for 'set sim'
};
```

***Level 3: Positional Args***
When the user has finished typing verbs, the CLI provides "options" for the parameters.

```text
static const char *set_cmd_log_args[] = {"domain", "entity", "level", nullptr};
static const char *set_cmd_sim_phy_args[] = {"ramp", "mass", "cond", "power", "ambient", nullptr};
static const comp_opt_t set_cmd_options[] = {
    { 1, set_cmd_log_args }, // Options for 'set log'
    { 2, set_cmd_sim_phy_args }, // Options for 'set sim phy'
};
```

**User Experience:**
When the user presses `ENTER`, the CLI shows the hint.

```text
> set ?<ENTER>
use: set log|sim
> set sim ?<ENTER>
Usage: set sim <phy|temp>
> set sim phy ?<ENTER>
Usage: set sim phy <ramp> <mass> <cond> <power> <ambient>
> set sim phy 1 2 3 ?<ENTER>
Usage: set sim phy 1 2 3 <power> <ambient>
```

### 4. Specialized Dynamic Completion
While static hints (Level 3) show placeholders like `<domain>`, Specialized Completion allows you to inject real-time values during the TAB-completion process.

**How it works:**
The spec_idx in your cmd_comp_t points to an entry in a global specs table. When the dispatcher reaches a command with a non-zero spec_idx, it executes a custom callback to fetch dynamic strings.

### 4.1. Defining the Completion Tree
Define the specs table and link it to your command configuration.

```text
// 1. Define the table of specialized handlers
static const comp_spec_t specs[] = {
    {0, nullptr           }, // Default: No special handling
    {1, set_log_completion}, // Special Handling 1: 'set log <domain> <entity> <level>'
};

// 2. Configure the Command Tree
static const cmd_comp_t set_cmd_comp = {
    .name = "set",
    .level1 = l1_set_cmd,
    .level2 = l2_set_cmd,
    .level3 = set_cmd_options,
    .opt_size = (uint16_t)(sizeof(set_cmd_options) / sizeof(comp_opt_t)),
    .spec_idx = 1 // Points to set_log_completion in 'specs' table
};
```

**User Experience:**
When the user presses `TAB`, the CLI no longer shows the hint, it shows the actual values.

```text
> set log ?<ENTER>
Usage: set log <domain> <entity> <level>
> set log <TAB>
use: all|dev|sys|test
> set log all <TAB>
use: all|none|cli|log|gpio|sysctrl|timer|uart
> set log all all <TAB>
use: none|critical|error|warn|info|debug
```






