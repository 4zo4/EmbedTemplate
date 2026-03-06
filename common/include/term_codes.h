#pragma once

// Terminal codes

// --- Navigation & Control ---
#define TERM_CLEAR_SCREEN "\033[2J\033[H"
#define TERM_CLEAR_LINE "\033[2K"
#define TERM_CLEAR_TO_EOL "\033[K"
#define TERM_CURSOR_UP "\033[A"
#define TERM_CURSOR_SAVE "\033[s"
#define TERM_CURSOR_RESTORE "\033[u"
#define TERM_BACKSPACE "\b \b"
#define TERM_SET_CURSOR_AT_LINE(line) "\033[" STR(line) ";1H"
#define TERM_INSERT_LINE "\033[L" // Inserts a blank line at cursor, pushing others down
#define TERM_TRUNCATION_SAFETY_ON "\033[7l"
#define TERM_TRUNCATION_SAFETY_OFF "\033[7h"

// --- Positional Macros ---
// Moves cursor to row Y, column X
#define TERM_GOTO_POS(y, x) "\033[" STR(y) ";" STR(x) "H"
// Sets the scroll region to between start and end (inclusive)
#define TERM_SET_SCROLL_REGION(start, end) "\033[" STR(start) ";" STR(end) "r"
#define TERM_RESET_SCROLL_REGION "\033[r"

// --- Colors & Styling ---
#define UI_COLOR_YELLOW "\033[1;33m"
#define UI_COLOR_CYAN "\033[1;36m"
#define UI_COLOR_GREEN "\033[1;32m"
#define UI_COLOR_RED "\033[1;31m"
#define UI_STYLE_RESET "\033[0m"
#define UI_STYLE_BOLD "\033[1m"

// --- Log Regions ---

#define MENU_REGION_START 1
#define MENU_REGION_END 20
#define SEPARATOR_LINE 21
#define LOG_REGION_START 22
#define LOG_REGION_END 40
#define LOG_REGION_OFFSET 8 // subregion for system logs within the log region