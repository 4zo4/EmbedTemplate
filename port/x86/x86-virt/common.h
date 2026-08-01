#pragma once

#include "arch_ops.h"
#include "fifo.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h"

#define LOG_UART_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_UART_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_UART_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_UART_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_UART_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_UART), __VA_ARGS__)

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

int  fflush(FILE *stream);
int  getchar(void);
void uart_flush(void);
void uart_set_echo(bool enabled);
void uart_set_buffered_mode(bool enabled);
void signal_data_ready(void);
bool stdin_ready(int timeout_ms);

extern bool echo_enabled;
extern bool buffered_mode;
