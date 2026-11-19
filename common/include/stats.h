#pragma once

typedef struct stats_irq_s {
    struct {
        uint32_t uart[2];
        uint32_t wdg;
    } count;
} stats_irq_t;

typedef struct stats_tx_s {
    struct {
        uint32_t uart[2];
    } count;
} stats_tx_t;
