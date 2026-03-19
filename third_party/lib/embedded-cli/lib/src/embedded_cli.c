#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "embedded_cli.h"

#define CLI_TOKEN_NPOS 0xffff

#ifndef UNUSED
#define UNUSED(x) (void)x
#endif

#define PREPARE_IMPL(t) \
  EmbeddedCliImpl* impl = (EmbeddedCliImpl*)t->_impl

#define IS_FLAG_SET(flags, flag) (((flags) & (flag)) != 0)

#define SET_FLAG(flags, flag) ((flags) |= (flag))

#define UNSET_FLAG(flags, flag) ((flags) &= ~(flag))

/**
 * Indicates that rx buffer overflow happened. In such case last command
 * that wasn't finished (no \r or \n were received) will be discarded
 */
#define CLI_FLAG_OVERFLOW 0x01u

/**
 * Indicates that initialization is completed. Initialization is completed in
 * first call to process and needed, for example, to print invitation message.
 */
#define CLI_FLAG_INIT_COMPLETE 0x02u

/**
 * Indicates that CLI structure and internal structures were allocated with
 * malloc and should be freed
 */
#define CLI_FLAG_ALLOCATED 0x04u

/**
 * Indicates that escape mode is enabled.
 */
#define CLI_FLAG_ESCAPE_MODE 0x08u

/**
 * Indicates that CLI in mode when it will print directly to output without
 * clear of current command and printing it back
 */
#define CLI_FLAG_DIRECT_PRINT 0x10u

/**
* Indicates that cursor direction should be forward
*/
#define CURSOR_DIRECTION_FORWARD true

/**
* Indicates that cursor direction should be backward
*/
#define CURSOR_DIRECTION_BACKWARD false

typedef struct EmbeddedCliImpl EmbeddedCliImpl;
typedef struct AutocompletedCommand AutocompletedCommand;
typedef struct FifoBuf FifoBuf;
typedef struct CliHistory CliHistory;

struct FifoBuf {
    char *buf;
    /**
     * Position of first element in buffer. From this position elements are taken
     */
    uint16_t front;
    /**
     * Position after last element. At this position new elements are inserted
     */
    uint16_t back;
    /**
     * Size of buffer
     */
    uint16_t size;
};

struct CliHistory {
    /**
     * Items in buffer are separated by null-chars
     */
    char *buf;

    /**
     * Total size of buffer
     */
    uint16_t bufferSize;

    /**
     * Index of currently selected element. This allows to navigate history
     * After command is sent, current element is reset to 0 (no element)
     */
    uint16_t current;

    /**
     * Number of items in buffer
     * Items are counted from top to bottom (and are 1 based).
     * So the most recent item is 1 and the oldest is itemCount.
     */
    uint16_t itemsCount;
};

struct EmbeddedCliImpl {
    /**
     * Invitation string. Is printed at the beginning of each line with user
     * input
     */
    const char *invitation;

    CliHistory history;

    /**
     * Buffer for storing received chars.
     * Chars are stored in FIFO mode.
     */
    FifoBuf rxBuffer;

    /**
     * Buffer for current command
     */
    char *cmdBuffer;

    /**
     * Size of current command
     */
    uint16_t cmdSize;

    /**
     * Total size of command buffer
     */
    uint16_t cmdMaxSize;

    CliCommandBinding *bindings;

    uint16_t bindingsCount;

    uint16_t maxBindingsCount;

    /**
     * Total length of input line. This doesn't include invitation but
     * includes current command and its live autocompletion
     */
    uint16_t inputLineLength;

    /**
     * Stores last character that was processed.
     */
    char lastChar;

    /**
     * Flags are defined as CLI_FLAG_*
     */
    uint16_t flags;

    /**
     * Cursor position for current command from right to left 
     * 0 = end of command
     */
    uint16_t cursorPos;
};

static EmbeddedCliConfig defaultConfig;

/**
 * Number of commands that cli adds. Commands:
 * - help
 */
#define CLI_INTERNAL_BINDING_COUNT 1

static const char *lineBreak = "\r\n";

/* References for VT100 escape sequences: 
 * https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences 
 * https://ecma-international.org/publications-and-standards/standards/ecma-48/
 */

/** Escape sequence - Cursor forward (right) */
static const char *escSeqCursorRight = "\x1B[C";

/** Escape sequence - Cursor backward (left) */
static const char *escSeqCursorLeft = "\x1B[D";

/** Escape sequence - Cursor save position */
static const char *escSeqCursorSave = "\x1B[s";

/** Escape sequence - Cursor restore position */
static const char *escSeqCursorRestore = "\x1B[u";

/** Escape sequence - Cursor insert character (ICH) */
//static const char *escSeqInsertChar = "\x1B[@";

/** Escape sequence - Cursor delete character (DCH) */
//static const char *escSeqDeleteChar = "\x1B[P";

/**
 * Navigate through command history back and forth. If navigateUp is true,
 * navigate to older commands, otherwise navigate to newer.
 * When history end is reached, nothing happens.
 * @param cli
 * @param navigateUp
 */
static void navigateHistory(EmbeddedCli *cli, bool navigateUp);

/**
 * Process escaped character. After receiving ESC+[ sequence, all chars up to
 * ending character are sent to this function
 * @param cli
 * @param c
 */
static void onEscapedInput(EmbeddedCli *cli, char c);

/**
 * Process input character. Character is valid displayable char and should be
 * added to current command string and displayed to client.
 * @param cli
 * @param c
 */
static void onCharInput(EmbeddedCli *cli, char c);

/**
 * Process control character (like \r or \n) possibly altering state of current
 * command or executing onCommand callback.
 * @param cli
 * @param c
 */
static void onControlInput(EmbeddedCli *cli, char c);

/**
 * Parse command in buffer and execute callback
 * @param cli
 */
static void parseCommand(EmbeddedCli *cli);

/**
 * Setup bindings for internal commands, like help
 * @param cli
 */
static void initInternalBindings(EmbeddedCli *cli);

/**
 * Show help for given tokens (or default help if no tokens)
 * @param cli
 * @param tokens
 * @param context - not used
 */
static void onHelp(EmbeddedCli *cli, char *tokens, void *context);
static void helpWithUse(EmbeddedCli *cli, const char **matches, int count);

/**
 * Show error about unknown command
 * @param cli
 * @param name
 */
static void onUnknownCommand(EmbeddedCli *cli, const char *name);

/**
 * Get autocompleted commands for given prefix.
 * Prefix is compared to known command bindings and autocompleted result
 * is returned in 'matches' array.
 * @param cli
 * @param prefix
 * @return number of matches
 */
static int getAutocompletedCommand(EmbeddedCli *cli, const char **matches, const char *prefix);

/**
 * Handles autocomplete request. If autocomplete possible - fills current
 * command with autocompleted command. When multiple commands satisfy entered
 * prefix, they are printed to output.
 * @param cli
 */
static void onAutocompleteRequest(EmbeddedCli *cli);

/**
 * Removes all input from current line (replaces it with whitespaces)
 * And places cursor at the beginning of the line
 * @param cli
 */
static void clearCurrentLine(EmbeddedCli *cli);

/**
 * Write given string to cli output
 * @param cli
 * @param str
 */
static void writeToOutput(EmbeddedCli *cli, const char *str);

/**
 * Move cursor forward (right) by given number of positions
 * @param cli
 * @param count
 * @param direction: true = forward (right), false = backward (left)
 */
static void moveCursor(EmbeddedCli* cli, uint16_t count, bool direction);

/**
 * Returns true if provided char is a supported control char:
 * \r, \n, \b or 0x7F (treated as \b)
 * @param c
 * @return
 */
static bool isControlChar(char c);

/**
 * Returns true if provided char is a valid displayable character:
 * a-z, A-Z, 0-9, whitespace, punctuation, etc.
 * Currently only ASCII is supported
 * @param c
 * @return
 */
static bool isDisplayableChar(char c);

/**
 * How many elements are currently available in buffer
 * @param buffer
 * @return number of elements
 */
static uint16_t fifoBufAvailable(FifoBuf *buffer);

/**
 * Return first character from buffer and remove it from buffer
 * Buffer must be non-empty, otherwise 0 is returned
 * @param buffer
 * @return
 */
static char fifoBufPop(FifoBuf *buffer);

/**
 * Push character into fifo buffer. If there is no space left, character is
 * discarded and false is returned
 * @param buffer
 * @param a - character to add
 * @return true if char was added to buffer, false otherwise
 */
static bool fifoBufPush(FifoBuf *buffer, char a);

/**
 * Copy provided string to the history buffer.
 * If it is already inside history, it will be removed from it and added again.
 * So after addition, it will always be on top
 * If available size is not enough (and total size is enough) old elements will
 * be removed from history so this item can be put to it
 * @param history
 * @param str
 * @return true if string was put in history
 */
static bool historyPut(CliHistory *history, const char *str);

/**
 * Get item from history. Items are counted from 1 so if item is 0 or greater
 * than itemCount, NULL is returned
 * @param history
 * @param item
 * @return true if string was put in history
 */
static const char *historyGet(CliHistory *history, uint16_t item);

/**
 * Remove specific item from history
 * @param history
 * @param str - string to remove
 * @return
 */
static void historyRemove(CliHistory *history, const char *str);

/**
 * Return position (index of first char) of specified token
 * @param tokenizedStr - tokenized string (separated by \0 with
 * \0\0 at the end)
 * @param pos - token position (counted from 1)
 * @return index of first char of specified token
 */
static uint16_t getTokenPosition(const char *tokenizedStr, uint16_t pos);

/*
 * --- Args auto-completion and binding lookup definitions ---
 */

static uint32_t cliHash(const char *str);
static void registerBindingIndex(const char *cmdName, uint16_t actualIdx);
static int findBindingIndex(const char *cmdName, const CliCommandBinding *bindings);
static int findCompletionIndex(const char *cmdName);

typedef struct {
    uint32_t nameHash;
    void (*onArgsCompletion)(EmbeddedCli *cli, const char *token, uint8_t pos);
} ArgsCompletion;

#ifndef ALIGN_UP
#define ALIGN_UP(n, align) (((n) + (align) - 1) & ~((align) - 1))
#endif
#if defined(__GNUC__) || defined(__clang__)
#define POW2(n) ((n) <= 1 ? 2 : 1u << (32 - __builtin_clz((n) - 1)))
#endif

#define BITMAP_UNIT_SIZE 32
#define BITMAP_SHIFT      5
#define BITMAP_UNIT_MASK  (BITMAP_UNIT_SIZE - 1)
#define BITMAP_ARRAY_SIZE(mapSize) (((mapSize) + BITMAP_UNIT_MASK) >> BITMAP_SHIFT)

#define COMPLETION_MAP_SIZE  POW2(2 * MAX_ARGS_COMPLETIONS)
#define COMPLETION_MAP_MASK  (COMPLETION_MAP_SIZE - 1)
#define COMPLETION_ARRAY_SIZE BITMAP_ARRAY_SIZE(COMPLETION_MAP_SIZE) 

#if MAX_ARGS_COMPLETIONS <= 255
static uint8_t  completionMap[COMPLETION_MAP_SIZE];
#else
static uint16_t completionMap[COMPLETION_MAP_SIZE];
#endif
static uint32_t inuseCompletion[COMPLETION_ARRAY_SIZE];
static ArgsCompletion completionTable[MAX_ARGS_COMPLETIONS];
static int completionsCount = 0;

#define BINDING_MAP_SIZE    POW2(2 * (MAX_BINDINGS + CLI_INTERNAL_BINDING_COUNT))
#define BINDING_MAP_MASK    (BINDING_MAP_SIZE - 1)
#define BINDING_ARRAY_SIZE  BITMAP_ARRAY_SIZE(BINDING_MAP_SIZE)

#if MAX_BINDINGS <= 255
static uint8_t  bindingMap[BINDING_MAP_SIZE];
#else
static uint16_t bindingMap[BINDING_MAP_SIZE];
#endif
static uint32_t inuseBindings[BINDING_ARRAY_SIZE];

#define IS_IN_USE(map, idx) \
    (map[(idx) >> BITMAP_SHIFT] & (1UL << ((idx) & BITMAP_UNIT_MASK)))

#define SET_IN_USE(map, idx) \
    (map[(idx) >> BITMAP_SHIFT] |= (1UL << ((idx) & BITMAP_UNIT_MASK)))

#define CLEAR_IN_USE(map, idx) \
    (map[(idx) >> BITMAP_SHIFT] &= ~(1UL << ((idx) & BITMAP_UNIT_MASK)))

#define MAX_WIDE_BINDINGS 12
static uint8_t wideBindingsIdx[MAX_WIDE_BINDINGS];
static uint16_t wideBindingsCount = 0;
static EmbeddedCliContext cliContext = {NULL, 0, 0};
static EmbeddedCliContext cliAppContext = {NULL, 0, 0};
typedef void (*helper_t)(EmbeddedCli *cli);
static helper_t cliHelpAux;

EmbeddedCliConfig *embeddedCliDefaultConfig(void) {
    defaultConfig.rxBufferSize = 64;
    defaultConfig.cmdBufferSize = 64;
    defaultConfig.historyBufferSize = 128;
    defaultConfig.cliBuffer = NULL;
    defaultConfig.cliBufferSize = 0;
    defaultConfig.maxBindingCount = 8;
    defaultConfig.invitation = "> ";
    return &defaultConfig;
}

uint16_t embeddedCliRequiredSize(EmbeddedCliConfig *config) {
    uint16_t bindingsCount = (uint16_t) (config->maxBindingCount + CLI_INTERNAL_BINDING_COUNT);

    size_t totalBytes = 
        ALIGN_UP(sizeof(EmbeddedCli), 8) +
        ALIGN_UP(sizeof(EmbeddedCliImpl), 8) +
        ALIGN_UP(config->rxBufferSize, 8) +
        ALIGN_UP(config->cmdBufferSize + 8, 8) +
        ALIGN_UP(config->historyBufferSize, 8) +
        ALIGN_UP(bindingsCount * sizeof(CliCommandBinding), 8);

    return (uint16_t) totalBytes;
}

EmbeddedCli *embeddedCliNew(EmbeddedCliConfig *config) {
    EmbeddedCli *cli = NULL;

    uint16_t bindingsCount = (uint16_t) (config->maxBindingCount + CLI_INTERNAL_BINDING_COUNT);

    size_t totalSize = embeddedCliRequiredSize(config);
    totalSize = ALIGN_UP(totalSize, 8); 

    bool allocated = false;
    if (config->cliBuffer == NULL) {
        config->cliBuffer = (CLI_UINT *) malloc(totalSize);
        if (config->cliBuffer == NULL)
            return NULL;
        allocated = true;
    } else if (config->cliBufferSize < totalSize) {
        return NULL;
    }

    CLI_UINT *buf = config->cliBuffer;

    if (allocated)
        memset(buf, 0, totalSize);

    cli = (EmbeddedCli *) buf;
    buf += BYTES_TO_CLI_UINTS(ALIGN_UP(sizeof(EmbeddedCli), 8));

    cli->_impl = (EmbeddedCliImpl *) buf;
    buf += BYTES_TO_CLI_UINTS(ALIGN_UP(sizeof(EmbeddedCliImpl), 8));

    PREPARE_IMPL(cli);
    impl->rxBuffer.buf = (char *) buf;
    buf += BYTES_TO_CLI_UINTS(ALIGN_UP(config->rxBufferSize * sizeof(char), 8));

    impl->cmdBuffer = (char *) buf;
    buf += BYTES_TO_CLI_UINTS(ALIGN_UP(config->cmdBufferSize * sizeof(char) + 8, 8));

    impl->bindings = (CliCommandBinding *) buf;
    buf += BYTES_TO_CLI_UINTS(ALIGN_UP(bindingsCount * sizeof(CliCommandBinding), 8));
    impl->history.buf = (char *) buf;
    impl->history.bufferSize = config->historyBufferSize;

    if (allocated)
        SET_FLAG(impl->flags, CLI_FLAG_ALLOCATED);

    impl->rxBuffer.size = config->rxBufferSize;
    impl->rxBuffer.front = 0;
    impl->rxBuffer.back = 0;
    impl->cmdMaxSize = config->cmdBufferSize;
    impl->bindingsCount = 0;
    impl->maxBindingsCount = (uint16_t) (config->maxBindingCount + CLI_INTERNAL_BINDING_COUNT);
    impl->lastChar = '\0';
    impl->invitation = config->invitation;
    impl->cursorPos = 0;

    initInternalBindings(cli);

    return cli;
}

EmbeddedCli *embeddedCliNewDefault(void) {
    return embeddedCliNew(embeddedCliDefaultConfig());
}

void embeddedCliReceiveChar(EmbeddedCli *cli, char c) {
    PREPARE_IMPL(cli);

    if (!fifoBufPush(&impl->rxBuffer, c)) {
        SET_FLAG(impl->flags, CLI_FLAG_OVERFLOW);
    }
}

void embeddedCliProcess(EmbeddedCli *cli) {
    if (cli->writeChar == NULL)
        return;

    PREPARE_IMPL(cli);


    if (!IS_FLAG_SET(impl->flags, CLI_FLAG_INIT_COMPLETE)) {
        SET_FLAG(impl->flags, CLI_FLAG_INIT_COMPLETE);
        writeToOutput(cli, impl->invitation);
    }

    while (fifoBufAvailable(&impl->rxBuffer)) {
        char c = fifoBufPop(&impl->rxBuffer);

        if (IS_FLAG_SET(impl->flags, CLI_FLAG_ESCAPE_MODE)) {
            onEscapedInput(cli, c);
        } else if (impl->lastChar == 0x1B && c == '[') {
            //enter escape mode
            SET_FLAG(impl->flags, CLI_FLAG_ESCAPE_MODE);
        } else if (isControlChar(c)) {
            onControlInput(cli, c);
        } else if (isDisplayableChar(c)) {
            onCharInput(cli, c);
        }

        //printLiveAutocompletion(cli);

        impl->lastChar = c;
    }

    // discard unfinished command if overflow happened
    if (IS_FLAG_SET(impl->flags, CLI_FLAG_OVERFLOW)) {
        impl->cmdSize = 0;
        impl->cmdBuffer[impl->cmdSize] = '\0';
        UNSET_FLAG(impl->flags, CLI_FLAG_OVERFLOW);
    }
}

uint16_t embeddedCliAddBinding(EmbeddedCli *cli, CliCommandBinding binding) {
    PREPARE_IMPL(cli);
    if (impl->bindingsCount == impl->maxBindingsCount)
        return BINDING_INVALID;

    int bindingsCount = impl->bindingsCount;
    impl->bindings[bindingsCount] = binding;

    if (IS_FLAG_SET(binding.flags, BINDING_FLAG_WIDE) &&
        wideBindingsCount < MAX_WIDE_BINDINGS) {
        wideBindingsIdx[wideBindingsCount++] = bindingsCount;
    }
    registerBindingIndex(impl->bindings[bindingsCount].name, bindingsCount);
    ++impl->bindingsCount;
    return bindingsCount;
}

void embeddedCliPrint(EmbeddedCli *cli, const char *string) {
    if (cli->writeChar == NULL)
        return;

    PREPARE_IMPL(cli);

    // Save cursor position
    uint16_t cursorPosSave = impl->cursorPos;

    // remove chars for autocompletion and live command
    if (!IS_FLAG_SET(impl->flags, CLI_FLAG_DIRECT_PRINT))
        clearCurrentLine(cli);

    // Restore cursor position
    impl->cursorPos = cursorPosSave;

    // print provided string
    writeToOutput(cli, string);
    writeToOutput(cli, lineBreak);

    // print current command back to screen
    if (!IS_FLAG_SET(impl->flags, CLI_FLAG_DIRECT_PRINT)) {
        writeToOutput(cli, impl->invitation);
        writeToOutput(cli, impl->cmdBuffer);
        impl->inputLineLength = impl->cmdSize;
        moveCursor(cli, impl->cursorPos, CURSOR_DIRECTION_BACKWARD);
    }
}

void embeddedCliFree(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);
    if (IS_FLAG_SET(impl->flags, CLI_FLAG_ALLOCATED)) {
        // allocation is done in single call to malloc, so need only single free
        free(cli);
    }
}

void embeddedCliTokenizeArgs(char *args) {
    if (args == NULL)
        return;

    // for now only space, but can add more later
    const char *separators = " ";

    // indicates that arg is quoted so separators are copied as is
    bool quotesEnabled = false;
    // indicates that previous char was a slash, so next char is copied as is
    bool escapeActivated = false;
    int insertPos = 0;

    int i = 0;
    char currentChar;
    while ((currentChar = args[i]) != '\0') {
        ++i;

        if (escapeActivated) {
            escapeActivated = false;
        } else if (currentChar == '\\') {
            escapeActivated = true;
            continue;
        } else if (currentChar == '"') {
            quotesEnabled = !quotesEnabled;
            currentChar = '\0';
        } else if (!quotesEnabled && strchr(separators, currentChar) != NULL) {
            currentChar = '\0';
        }

        // null chars are only copied once and not copied to the beginning
        if (currentChar != '\0' || (insertPos > 0 && args[insertPos - 1] != '\0')) {
            args[insertPos] = currentChar;
            ++insertPos;
        }
    }

    // make args double null-terminated source buffer must be big enough to contain extra spaces
    args[insertPos] = '\0';
    args[insertPos + 1] = '\0';
}

const char *embeddedCliGetToken(const char *tokenizedStr, uint16_t pos) {
    uint16_t i = getTokenPosition(tokenizedStr, pos);

    if (i != CLI_TOKEN_NPOS)
        return &tokenizedStr[i];
    else
        return NULL;
}

char *embeddedCliGetTokenVariable(char *tokenizedStr, uint16_t pos) {
    uint16_t i = getTokenPosition(tokenizedStr, pos);

    if (i != CLI_TOKEN_NPOS)
        return &tokenizedStr[i];
    else
        return NULL;
}

uint16_t embeddedCliFindToken(const char *tokenizedStr, const char *token) {
    if (tokenizedStr == NULL || token == NULL)
        return 0;

    uint16_t size = embeddedCliGetTokenCount(tokenizedStr);
    for (uint16_t i = 1; i <= size; ++i) {
        if (strcmp(embeddedCliGetToken(tokenizedStr, i), token) == 0)
            return i;
    }

    return 0;
}

uint16_t embeddedCliGetTokenCount(const char *tokenizedStr) {
    if (tokenizedStr == NULL || tokenizedStr[0] == '\0')
        return 0;

    int i = 0;
    uint16_t tokenCount = 1;
    while (true) {
        if (tokenizedStr[i] == '\0') {
            if (tokenizedStr[i + 1] == '\0')
                break;
            ++tokenCount;
        }
        ++i;
    }

    return tokenCount;
}

static void navigateHistory(EmbeddedCli *cli, bool navigateUp) {
    PREPARE_IMPL(cli);
    if (impl->history.itemsCount == 0 ||
        (navigateUp && impl->history.current == impl->history.itemsCount) ||
        (!navigateUp && impl->history.current == 0))
        return;

    clearCurrentLine(cli);

    writeToOutput(cli, impl->invitation);

    if (navigateUp)
        ++impl->history.current;
    else
        --impl->history.current;

    const char *item = historyGet(&impl->history, impl->history.current);
    // simple way to handle empty command the same way as others
    if (item == NULL)
        item = "";
    uint16_t len = (uint16_t) strlen(item);
    memcpy(impl->cmdBuffer, item, ALIGN_UP(len, 8));
    impl->cmdBuffer[len] = '\0';
    impl->cmdSize = len;

    writeToOutput(cli, impl->cmdBuffer);
    impl->inputLineLength = impl->cmdSize;
    impl->cursorPos = 0;
}

static void onEscapedInput(EmbeddedCli *cli, char c) {
    PREPARE_IMPL(cli);

    if (c >= 64 && c <= 126) {
        // handle escape sequence
        UNSET_FLAG(impl->flags, CLI_FLAG_ESCAPE_MODE);

        if (c == 'A' || c == 'B') {
            // treat \e[..A as cursor up and \e[..B as cursor down
            // there might be extra chars between [ and A/B, just ignore them
            navigateHistory(cli, c == 'A');
        }

        if (c == 'C' && impl->cursorPos > 0) {
            impl->cursorPos--;
            writeToOutput(cli, escSeqCursorRight);
        }

        if (c == 'D' && impl->cursorPos < strlen(impl->cmdBuffer)) {
            impl->cursorPos++;
            writeToOutput(cli, escSeqCursorLeft);
        }
    }
}

static void onCharInput(EmbeddedCli *cli, char c) {
    PREPARE_IMPL(cli);

    if (impl->cmdSize + 1 >= impl->cmdMaxSize)
        return;

    uint16_t pos = impl->cursorPos;

    if (pos >= impl->inputLineLength) {
        impl->cmdBuffer[pos] = c;
        impl->cmdSize = pos + 1;
        impl->inputLineLength = impl->cmdSize;
        impl->cursorPos = impl->cmdSize;
        impl->cmdBuffer[impl->cmdSize] = '\0';
        impl->lastChar = '\0';

        if (pos == 0 || impl->cmdBuffer[pos-1] == ' ') {
            embeddedCliRefresh(cli); 
        } else {
            cli->writeChar(cli, c); 
        }
    } else {
        memmove(&impl->cmdBuffer[pos + 1], 
                &impl->cmdBuffer[pos], 
                (size_t)(impl->inputLineLength - pos));
        
        impl->cmdBuffer[pos] = c;
        impl->cmdSize++;

        embeddedCliRefresh(cli);
    }
}

static void onControlInput(EmbeddedCli *cli, char c) {
    PREPARE_IMPL(cli);

    // process \r\n and \n\r as single \r\n command
    if ((impl->lastChar == '\r' && c == '\n') ||
        (impl->lastChar == '\n' && c == '\r'))
        return;

    if (c == '\r' || c == '\n') {
        writeToOutput(cli, lineBreak);

        if (impl->cmdSize > 0) {
            impl->cmdBuffer[impl->cmdSize] = '\0';
            parseCommand(cli);
        }

        impl->cmdSize = 0;
        impl->cmdBuffer[impl->cmdSize] = '\0';
        impl->inputLineLength = 0;
        impl->history.current = 0;
        impl->cursorPos = 0;

        writeToOutput(cli, impl->invitation);
    } else if ((c == '\b' || c == 0x7F) && (impl->cursorPos > 0)) {
        if (impl->cursorPos < impl->inputLineLength) {
                memmove(&impl->cmdBuffer[impl->cursorPos - 1], 
                    &impl->cmdBuffer[impl->cursorPos], 
                    (size_t)(impl->inputLineLength - impl->cursorPos));
        }

        impl->cursorPos--;
        impl->inputLineLength--;
        impl->cmdSize = impl->inputLineLength;
        impl->cmdBuffer[impl->inputLineLength] = '\0';

        if (impl->cursorPos == impl->inputLineLength) {
            writeToOutput(cli, "\b \b");
        } else {
            embeddedCliRefresh(cli);
        }
    } else if (c == '\t') {
        if (c == '\t' && impl->lastChar == '\t')
            impl->lastChar = '\0';
        onAutocompleteRequest(cli);
    }
}

static void parseCommand(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);

    bool isEmpty = true;

    for (int i = 0; i < impl->cmdSize; ++i) {
        if (impl->cmdBuffer[i] != ' ') {
            isEmpty = false;
            break;
        }
    }
    // do not process empty commands
    if (isEmpty)
        return;
    // push command to history before buffer is modified
    historyPut(&impl->history, impl->cmdBuffer);

    char *cmdName = NULL;
    char *cmdArgs = NULL;
    bool nameFinished = false;
    uint16_t len = 0;

    // find command name and command args inside command buffer
    for (int i = 0; i < impl->cmdSize; ++i) {
        char c = impl->cmdBuffer[i];

        if (c == ' ') {
            // all spaces between name and args are filled with zeros
            // so name is a correct null-terminated string
            if (cmdArgs == NULL)
                impl->cmdBuffer[i] = '\0';
            if (cmdName != NULL) {
                nameFinished = true;
                len = i;
            }

        } else if (cmdName == NULL) {
            cmdName = &impl->cmdBuffer[i];
        } else if (cmdArgs == NULL && nameFinished) {
            cmdArgs = &impl->cmdBuffer[i];
        }
    }

    // we keep two last bytes in cmd buffer reserved so cmdSize is always by 2
    // less than cmdMaxSize
    impl->cmdBuffer[impl->cmdSize + 1] = '\0';

    if (cmdName == NULL)
        return;

    int i = findBindingIndex(cmdName, impl->bindings);
    if (i == -1) {
        onUnknownCommand(cli, cmdName);
        return;
    }

    if (impl->bindings[i].binding != NULL) {
        if (IS_FLAG_SET(impl->bindings[i].flags, BINDING_FLAG_TOKENIZE_ARGS))
            embeddedCliTokenizeArgs(cmdArgs);
        // currently, output is blank line, so we can just print directly
        SET_FLAG(impl->flags, CLI_FLAG_DIRECT_PRINT);
        // check if help was requested for commands without help
        if (!IS_FLAG_SET(impl->bindings[i].flags, BINDING_FLAG_HAS_HELP) &&
           (cmdArgs != NULL && (strcmp(cmdArgs, "?") == 0))) {
            cmdName[len + 1] = '\0';
            onHelp(cli, cmdName, NULL);
        } else {
            impl->bindings[i].binding(cli, cmdArgs, impl->bindings[i].context);
        }
        UNSET_FLAG(impl->flags, CLI_FLAG_DIRECT_PRINT);
        return;
    } else if (cli->onCommand != NULL) {
        CliCommand command;
        command.name = cmdName;
        command.args = cmdArgs;

        // currently, output is blank line, so we can just print directly
        SET_FLAG(impl->flags, CLI_FLAG_DIRECT_PRINT);
        cli->onCommand(cli, &command);
        UNSET_FLAG(impl->flags, CLI_FLAG_DIRECT_PRINT);
    } else {
        onUnknownCommand(cli, cmdName);
    }
}

static void initInternalBindings(EmbeddedCli *cli) {
    CliCommandBinding b = {
        .name = "help",
        .flags = BINDING_FLAG_WIDE,
        .binding = onHelp,
    };
    embeddedCliAddBinding(cli, b);
}

void embeddedCliAddHelpAux(void (*aux)(EmbeddedCli *cli)) {
    cliHelpAux = aux;
}

static void helpWithUse(EmbeddedCli *cli, const char **matches, int count) {
    char msg[128] = "use: ";
    int n = 5;
    const char *sep = "";
    bool after_1st_word = false;

    for (int i = 0; i < count; i++) {
        if (matches[i][0] == '\0')
            continue;

        if (n >= (int)sizeof(msg) - 20)
            break;

        if (after_1st_word) {
            sep = "|";
        }

        n += snprintf(msg + n, sizeof(msg) - n, "%s%s", sep, matches[i]);
        after_1st_word = true;
    }
    embeddedCliPrint(cli, msg);
}

static void onHelp(EmbeddedCli *cli, char *tokens, void *context) {
    UNUSED(context);
    PREPARE_IMPL(cli);

    if (cliHelpAux)
        cliHelpAux(cli);
    if (impl->bindingsCount == 0) {
        writeToOutput(cli, "Help is not available");
        writeToOutput(cli, lineBreak);
        return;
    }

    uint16_t tokenCount = embeddedCliGetTokenCount(tokens);
    if (tokenCount == 0) {
        writeToOutput(cli, "use: ");
        for (int i = 0; i < impl->bindingsCount; ++i) {
            if (!IS_FLAG_SET(impl->bindings[i].flags, BINDING_FLAG_HIDDEN)) {
                writeToOutput(cli, impl->bindings[i].name);
                writeToOutput(cli, "|");
            }
        }
        writeToOutput(cli, lineBreak);
        writeToOutput(cli, "help: <cmd> ? or <cmd> <arg>? (with TAB)");
        writeToOutput(cli, lineBreak);
    } else if (tokenCount == 1) {
        // try find command
        const char *cmdName = embeddedCliGetToken(tokens, 1);
        bool found = false;
        for (int i = 0; i < impl->bindingsCount; ++i) {
            if (strcmp(impl->bindings[i].name, cmdName) == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            writeToOutput(cli, "Usage: ");
            writeToOutput(cli, cmdName);
            writeToOutput(cli, lineBreak);
        } else {
            onUnknownCommand(cli, cmdName);
        }
    } else {
        writeToOutput(cli, "Command receives one or zero arguments");
        writeToOutput(cli, lineBreak);
    }
}

static void onUnknownCommand(EmbeddedCli *cli, const char *name) {
    writeToOutput(cli, "Unknown command: \"");
    writeToOutput(cli, name);
    writeToOutput(cli, "\". Write \"help\" for a list of available commands");
    writeToOutput(cli, lineBreak);
}

static inline const char *getNameFromBindings(CliCommandBinding *bindings, int i) {
    return bindings[i].name;
}

static inline const char *getNameFromContext(EmbeddedCliContext *ctx, int i) {
    return *(const char **)((uint8_t *)ctx->base + (i * ctx->stride));
}

/**
 * @brief Performs prioritized autocompletion search for the current prefix.
 * 
 * Implements a hierarchical search to find up to 8 command matches:
 * 1. Primary Context: Scans the dynamic 'EmbeddedCliContext' (e.g., current test names).
 * 2. Application Context: Scans the 'App Context' index list, applying a bitmask (stride) 
 *    to filter out commands that are disabled in the current menu state.
 * 3. Global/Wide Context: If no matches in App Context, scans 'wideBindingsIdx' (e.g., help, quit).
 * 
 * @param cli      Pointer to the CLI instance.
 * @param matches  Array to be populated with pointers to matching command strings.
 * @param prefix   The current partial string typed by the user.
 * @return         The number of matches found (capped at 8).
 */
static int getAutocompletedCommand(EmbeddedCli *cli, const char **matches, const char *prefix) {
    size_t prefixLen = strlen(prefix);
    int count = 0;
    int maxcount = 0;
    const char *name;

    PREPARE_IMPL(cli);
    if (impl->bindingsCount == 0 || prefixLen == 0)
        return 0;

    EmbeddedCliContext *ctx = embeddedCliGetContext();

    maxcount = ctx ? ctx->count : 0;

    for (int i = 0; i < maxcount; i++) {
        name = getNameFromContext(ctx, i);
        if (strncmp(name, prefix, prefixLen) == 0) {
            if (count < 8) {
                matches[count] = name;
                count++;
            } else {
                return 8;
            }            
        }
    }

    if (count > 0)
        return count; 

    ctx = embeddedCliGetAppContext();
    const uint8_t *bindingIdx;
    uint16_t map = 0;

    for (int n = 0; n < 2; n++) {
        if (n) {
            maxcount = wideBindingsCount;
            bindingIdx = wideBindingsIdx;
        } else {
            maxcount = ctx->count;
            bindingIdx = ctx->base;
            map = ~(ctx->stride);
        }
        for (int i = 0; i < maxcount; i++) {
            if (map & (1 << i))
                continue;
            name = getNameFromBindings(impl->bindings, bindingIdx[i]);
            if (strncmp(name, prefix, prefixLen) == 0) {
                if (count < 8) {
                    matches[count] = name;
                    count++;
                } else {
                    break;
                }
            }
        }
    }
    return count;
}

/**
 * @brief Finds the length of the common prefix among a set of strings.
 */
static size_t getLongestCommonPrefix(const char **matches, int count) {
    size_t len = 0;
    while (matches[0][len] != '\0') {
        char c = matches[0][len];
        for (int i = 1; i < count; i++) {
            if (matches[i][len] != c) {
                return len;
            }
        }
        len++;
    }
    return len;
}

static void onAutocompleteRequest(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);

    if (impl->cmdSize == 0)
        return;

    char *firstSpace = NULL;
    char *lastSpace = NULL;
    uint8_t pos = 0;

    for (uint16_t i = 0; i < impl->cmdSize; i++) {
        if (impl->cmdBuffer[i] == ' ') {
            if (!firstSpace)
                firstSpace = &impl->cmdBuffer[i];
            lastSpace = &impl->cmdBuffer[i];
            pos++;
        }
    }

    // --- Command Completion ---
    if (firstSpace == NULL) {
        const char *matches[8];
        int count = getAutocompletedCommand(cli, matches, impl->cmdBuffer);
        // is a single match
        if (count == 1) {
            embeddedCliCompletion(cli, matches[0]);
        } else if (count > 1) {
            size_t lcp = getLongestCommonPrefix(matches, count);
            size_t len = strlen(impl->cmdBuffer);

            if (lcp > len) {
                memcpy(impl->cmdBuffer, matches[0], lcp);
                impl->cmdSize = (uint16_t)lcp;
                impl->cmdBuffer[lcp] = '\0';
            }
            helpWithUse(cli, matches, count);
        }
        embeddedCliRefresh(cli);
        return;
    }

    // --- Sub-Command / Argument Completion ---
    alignas(8) char cmdName[32+8];
    size_t cmdLen = (size_t)(firstSpace - impl->cmdBuffer);
    if (cmdLen > 32) cmdLen = 32;
    memcpy(cmdName, impl->cmdBuffer, ALIGN_UP(cmdLen, 8));
    cmdName[cmdLen] = '\0';

    const char *fragment = lastSpace + 1;

    // Check is Help Request '?'
    bool isHelpRequest = (impl->cmdSize > 0 && impl->cmdBuffer[impl->cmdSize - 1] == '?');
    
    if (isHelpRequest) {
        // Truncate the '?' from the buffer so the completion logic doesn't see it
        impl->cmdSize--;
        impl->cmdBuffer[impl->cmdSize] = '\0';
        impl->cursorPos = impl->cmdSize;
        impl->inputLineLength = impl->cmdSize;
    }

    int idx = findCompletionIndex(cmdName);
    if (idx != -1) {
        completionTable[idx].onArgsCompletion(cli, isHelpRequest ? "?" : fragment, pos);
        embeddedCliRefresh(cli);
    }
}

static void clearCurrentLine(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);
    size_t len = impl->inputLineLength + strlen(impl->invitation);

    cli->writeChar(cli, '\r');
    for (size_t i = 0; i < len; ++i) {
        cli->writeChar(cli, ' ');
    }
    cli->writeChar(cli, '\r');
    impl->inputLineLength = 0;

    impl->cursorPos = 0;
}

static void writeToOutput(EmbeddedCli *cli, const char *str) {
    size_t len = strlen(str);

    for (size_t i = 0; i < len; ++i) {
        cli->writeChar(cli, str[i]);
    }
}

static void moveCursor(EmbeddedCli* cli, uint16_t count, bool direction) {
    // Check if we need to send any command
    if (count == 0)
        return;

    // 5 = uint16_t max, 3 = escape sequence, 1 = string termination
    char escBuffer[5 + 3 + 1] = { 0 };
    char dirChar = direction ? escSeqCursorRight[2] : escSeqCursorLeft[2];
    sprintf(escBuffer, "\x1B[%u%c", count, dirChar);
    writeToOutput(cli, escBuffer);
}

static bool isControlChar(char c) {
    return c == '\r' || c == '\n' || c == '\b' || c == '\t' || c == 0x7F;
}

static bool isDisplayableChar(char c) {
    return (c >= 32 && c <= 126);
}

static uint16_t fifoBufAvailable(FifoBuf *buffer) {
    if (buffer->back >= buffer->front)
        return (uint16_t) (buffer->back - buffer->front);
    else
        return (uint16_t) (buffer->size - buffer->front + buffer->back);
}

static char fifoBufPop(FifoBuf *buffer) {
    char a = '\0';
    if (buffer->front != buffer->back) {
        a = buffer->buf[buffer->front];
        buffer->front = (uint16_t) (buffer->front + 1) % buffer->size;
    }
    return a;
}

static bool fifoBufPush(FifoBuf *buffer, char a) {
    uint16_t newBack = (uint16_t) (buffer->back + 1) % buffer->size;
    if (newBack != buffer->front) {
        buffer->buf[buffer->back] = a;
        buffer->back = newBack;
        return true;
    }
    return false;
}

static bool historyPut(CliHistory *history, const char *str) {
    size_t len = strlen(str);
    // each item is ended with \0 so, need to have that much space at least
    if (history->bufferSize < len + 1)
        return false;

    // remove str from history (if it's present) so we don't get duplicates
    historyRemove(history, str);

    size_t usedSize;
    // remove old items if new one can't fit into buffer
    while (history->itemsCount > 0) {
        const char *item = historyGet(history, history->itemsCount);
        size_t itemLen = strlen(item);
        usedSize = ((size_t) (item - history->buf)) + itemLen + 1;

        size_t freeSpace = history->bufferSize - usedSize;

        if (freeSpace >= len + 1)
            break;

        // space not enough, remove last element
        --history->itemsCount;
    }
    if (history->itemsCount > 0) {
        // when history not empty, shift elements so new item is first
        memmove(&history->buf[len + 1], history->buf, usedSize);
    }
    memcpy(history->buf, str, len + 1);
    ++history->itemsCount;

    return true;
}

static const char *historyGet(CliHistory *history, uint16_t item) {
    if (item == 0 || item > history->itemsCount)
        return NULL;

    // items are stored in the same way (separated by \0 and counted from 1),
    // so can use this call
    return embeddedCliGetToken(history->buf, item);
}

static void historyRemove(CliHistory *history, const char *str) {
    if (str == NULL || history->itemsCount == 0)
        return;
    char *item = NULL;
    uint16_t itemPosition;
    for (itemPosition = 1; itemPosition <= history->itemsCount; ++itemPosition) {
        // items are stored in the same way (separated by \0 and counted from 1),
        // so can use this call
        item = embeddedCliGetTokenVariable(history->buf, itemPosition);
        if (strcmp(item, str) == 0) {
            break;
        }
        item = NULL;
    }
    if (item == NULL)
        return;

    --history->itemsCount;
    if (itemPosition == (history->itemsCount + 1)) {
        // if this is a last element, nothing is remaining to move
        return;
    }

    size_t len = strlen(item);
    size_t remaining = (size_t) (history->bufferSize - (item + len + 1 - history->buf));
    // move everything to the right of found item
    memmove(item, &item[len + 1], remaining);
}

static uint16_t getTokenPosition(const char *tokenizedStr, uint16_t pos) {
    if (tokenizedStr == NULL || pos == 0)
        return CLI_TOKEN_NPOS;
    uint16_t i = 0;
    uint16_t tokenCount = 1;
    while (true) {
        if (tokenCount == pos)
            break;

        if (tokenizedStr[i] == '\0') {
            ++tokenCount;
            if (tokenizedStr[i + 1] == '\0')
                break;
        }

        ++i;
    }

    if (tokenizedStr[i] != '\0')
        return i;
    else
        return CLI_TOKEN_NPOS;
}

/*
 * --- Args auto-completion and binding lookup definitions ---
 */

uint16_t embeddedCliGetBindingsCount(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);
    return impl->bindingsCount;
}

const char* embeddedCliGetInputString(EmbeddedCli *cli, uint16_t *input_size) {
    PREPARE_IMPL(cli);
    if (input_size)
        *input_size = impl->cmdSize;
    return impl->cmdBuffer;
}

void embeddedCliCompletion(EmbeddedCli *cli, const char *name) {
    PREPARE_IMPL(cli);
    size_t nameLen = strlen(name);
    
    // Find the fragment start (backwards from cursor)
    uint16_t tokenStart = impl->cursorPos;
    while (tokenStart > 0 && impl->cmdBuffer[tokenStart - 1] != ' ') {
        tokenStart--;
    }

    // Capacity check (+2 for space and null)
    if ((size_t)tokenStart + nameLen + 2 >= (size_t)impl->cmdMaxSize) 
        return;

    // Overwrite the fragment with the full name
    memcpy(&impl->cmdBuffer[tokenStart], name, nameLen);
    
    // Update cmdSize and add trailing space
    impl->cmdSize = (uint16_t)(tokenStart + nameLen);
    impl->cmdBuffer[impl->cmdSize++] = ' '; 
    impl->cmdBuffer[impl->cmdSize] = '\0';
}

void embeddedCliRefresh(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);

    const char *clearLine = "\r\x1B[2K"; 
    while (*clearLine)
        cli->writeChar(cli, *clearLine++);

    // Reprint Prompt & Buffer
    const char *inv = impl->invitation ? impl->invitation : "> ";
    while (*inv)
        cli->writeChar(cli, *inv++);
    for (uint16_t i = 0; i < impl->cmdSize; ++i) {
        cli->writeChar(cli, impl->cmdBuffer[i]);
    }
    
    // Sync all internal state
    impl->cursorPos = impl->cmdSize;
    impl->inputLineLength = impl->cmdSize;
    impl->cmdBuffer[impl->cmdSize] = '\0';

    // Reset character state & Flush FIFO (Discard pending input)
    impl->lastChar = '\0';
    impl->rxBuffer.front = impl->rxBuffer.back;
}

/**
 * @brief Generates a 32-bit hash using the djb2 algorithm.
 * 
 * This is a high-performance string hashing function (Dan Bernstein's algorithm) 
 * optimized for short identifiers like CLI command names. It uses the initial 
 * magic constant 5381 and a multiplier of 33 (implemented via 'hash << 5 + hash') 
 * to ensure an excellent bit distribution with minimal CPU cycles.
 * 
 * Performance & Safety:
 * - Computational Complexity: O(n) where n is the string length.
 * - Hardware Efficiency: Uses only bit-shifting and addition, avoiding 
 *   expensive multiplication or division instructions.
 * - Sign Safety: Explicitly casts characters to uint8_t to prevent sign 
 *   extension errors with extended ASCII values (>127).
 * 
 * @param str The null-terminated string to be hashed.
 * @return A 32-bit unsigned integer representing the hash of the string.
 */
static uint32_t cliHash(const char *str) {
    uint32_t hash = 5381;
    uint32_t c;
    while ((c = (uint8_t)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/**
 * @brief Performs an O(1) optimized lookup for argument completion handlers.
 * 
 * This function locates the specific completion callback associated with a 
 * given command name using a Shadow Index Map architecture. This allows for 
 * fast lookups while maintaining a dense physical storage table.
 *
 * Logic Differentiation:
 * Like the Binding Table, this system uses a "Shadow Map" (completionMap) to 
 * translate hash-based slots into physical indices. However, collisions are
 * resolved via 32-bit nameHash comparison rather than strcmp(). This provides a 
 * "Lightweight" O(1) path that eliminates the need for string storage.
 *
 * Memory Efficiency:
 * By decoupling the sparse lookup map from the completion table, this 
 * implementation achieves a significant RAM reduction. For an 8-entry 
 * configuration, the system uses a 16-slot Shadow Map (16 bytes) and a 
 * 1-word segmented bitmap (4 bytes) to gatekeep an 8-slot Completion Table (64 bytes). 
 * This results in a total footprint of exactly 84 bytes, providing a 36% 
 * saving over a unified 16-slot hash table (132 bytes) by keeping the 
 * function-pointer storage dense.
 *
 * Scalability & Constraints:
 * The architecture is tiered for efficiency: it uses 8-bit indices for up to 
 * 255 completions (automatically promoting to 16-bit for larger sets) and 
 * a segmented bitmap array (inuseCompletion) that scales to any volume. 
 * The O(1) performance is maintained as the system grows by 
 * scaling the COMPLETION_MAP_SIZE to preserve a 50% load factor.
 * 
 * @param cmdName The command name (e.g., "show", "config") to look up.
 * @return The index in the completionTable if found; -1 otherwise.
 */
static int findCompletionIndex(const char *cmdName) {
    uint32_t hash = cliHash(cmdName);
    uint32_t idx = hash & COMPLETION_MAP_MASK;
    for (int i = 0; i < MAX_ARGS_COMPLETIONS; i++) {
        if (!IS_IN_USE(inuseCompletion, idx))
            break;
        int completionIdx = completionMap[idx];
        if (completionTable[completionIdx].nameHash == hash)
            return (int)completionIdx;
        idx = (idx + 1) & COMPLETION_MAP_MASK;
    }
    return -1;
}

/**
 * @brief Performs an O(1) optimized lookup of a command binding index.
 * 
 * This implementation utilizes a Shadow Index Map (Look-Aside Buffer) to 
 * bypass the standard linear search. It maps a truncated hash of the 
 * command name to its physical index in the bindings array.
 * 
 * The process follows a "Gatekeeper" pattern:
 * 1. Hash the input string (cmdName) using the djb2 algorithm.
 * 2. Check the Segmented In-Use Bitmap to instantly verify if any command 
 *    maps to the calculated hash slot (avoids memory access if missing).
 * 3. Retrieve the candidate index from the bindingMap.
 * 4. Perform a single strcmp() to verify the match. This resolves any 
 *    potential hash collisions and ensures 100% accuracy.
 * Memory Efficiency:
 * The implementation is RAM-optimized for embedded constraints. By 
 * decoupling the large command bindings from the lookup logic, the shadow map 
 * requires only ~5-9 bits per slot for occupancy (segmented bitmap) and 
 * 8 or 16 bits per slot for the index (tiered mapping). For a 256-slot table, 
 * this results in a footprint of roughly 288 bytes, providing O(1) 
 * performance without the overhead of storing full pointers or redundant 
 * string data in the hash table.
 * 
 * @param cmdName  The string name of the command to find.
 * @param bindings The library's internal array of command bindings.
 * @return The index of the binding if found; -1 otherwise.
 */
static int findBindingIndex(const char *cmdName, const CliCommandBinding *bindings) {
    uint32_t hash = cliHash(cmdName);
    uint32_t idx = hash & BINDING_MAP_MASK;

    // Use a fixed probe limit or a fraction of map size
    for (int i = 0; i < 32; i++) {
        if (!IS_IN_USE(inuseBindings, idx))
            break;

        uint32_t actualIdx = (uint32_t)bindingMap[idx];

        if (strcmp(cmdName, bindings[actualIdx].name) == 0) {
            return (int)actualIdx;
        }

        idx = (idx + 1) & BINDING_MAP_MASK;
    }
    return -1;
}

static void registerBindingIndex(const char *cmdName, uint16_t actualIdx) {
    uint32_t hash = cliHash(cmdName);
    uint32_t idx = hash & BINDING_MAP_MASK;
    while (IS_IN_USE(inuseBindings, idx)) {
        idx = (idx + 1) & BINDING_MAP_MASK;
    }
    bindingMap[idx] = (typeof(bindingMap[0]))actualIdx;
    SET_IN_USE(inuseBindings, idx);
}

bool embeddedCliAddCompletion(const char *name, 
    void (*onArgsCompletion)(EmbeddedCli *cli, const char *token, uint8_t pos)) {
    
    if (completionsCount >= MAX_ARGS_COMPLETIONS)
        return false;

    uint32_t hash = cliHash(name);
    int inuse = findCompletionIndex(name);

    if (inuse != -1) {
        completionTable[inuse].onArgsCompletion = onArgsCompletion;
        return true;
    }

    uint32_t idx = hash & COMPLETION_MAP_MASK;

    for (int i = 0; i < COMPLETION_MAP_SIZE; i++) {
        if (!IS_IN_USE(inuseCompletion, idx)) {
            int completionIdx = completionsCount;
            completionTable[completionIdx].nameHash = hash;
            completionTable[completionIdx].onArgsCompletion = onArgsCompletion;
            completionMap[idx] = (typeof(completionMap[0]))completionIdx;
            SET_IN_USE(inuseCompletion, idx);
            completionsCount++;
            return true;
        }

        idx = (idx + 1) & COMPLETION_MAP_MASK;
    }

    return false; // Should never reach here
}

static bool compareBindings(const CliCommandBinding *a, const CliCommandBinding *b) {
    return (strcmp(a->name, b->name) == 0);
}

static inline bool compareArgsCompletion(const ArgsCompletion *a, const ArgsCompletion *b) {
    return (a->nameHash == b->nameHash);
}

int embeddedCliCheckBindingDuplicates(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);
    int count = 0;
    uint16_t bindingsCount = impl->bindingsCount;
    if (bindingsCount < 2)
        return 0;

    for (uint16_t i = 0; i < bindingsCount - 1; i++) {
        for (uint16_t j = i + 1; j < bindingsCount; j++) {
            if (compareBindings(&impl->bindings[i], &impl->bindings[j])) {
                count++;
            }
        }
    }
    return count;
}

int embeddedCliCheckArgsCompletionDuplicates(void) {
    int count = 0;
    if (completionsCount < 2)
        return 0;

    for (uint16_t i = 0; i < completionsCount - 1; i++) {
        for (uint16_t j = i + 1; j < completionsCount; j++) {
            if (compareArgsCompletion(&completionTable[i], &completionTable[j])) {
                count++;
            }
        }
    }
    return count;
}

void embeddedCliSetContext(const void *base, int count, uint16_t stride) {
    cliContext.base = base;
    cliContext.count = count;
    cliContext.stride = stride;
}

EmbeddedCliContext *embeddedCliGetContext(void) {
    return cliContext.count ? &cliContext : NULL;
}

void embeddedCliAddAppContext(uint8_t *base, int count)
{
    cliAppContext.base = base;
    cliAppContext.count = count;
}

EmbeddedCliContext *embeddedCliGetAppContext(void) {
    return cliAppContext.count ? &cliAppContext : NULL;
}

void embeddedCliSetAppContext(uint16_t map) {
    cliAppContext.stride = map;
}