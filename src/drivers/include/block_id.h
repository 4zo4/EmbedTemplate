#pragma once

// SoC block indices
typedef enum {
    GPIO = 0,
    PCI,
    SYSCTRL,
    TIMER,
    UART,
    MAX_BLOCK_INDEX // Extend as needed
} Block_e;

#define NUM_BLOCKS MAX_BLOCK_INDEX