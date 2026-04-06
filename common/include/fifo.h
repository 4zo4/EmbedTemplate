#pragma once

/**
 * @brief Thread-safe (SPSC) Atomic FIFO
 */
void fifo_init(void);
void fifo_push(char c);
int  fifo_pop(void);
bool fifo_is_empty(void);
