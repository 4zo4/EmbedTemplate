#pragma once

#include "arch_ops.h"
#include "fifo.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h"

typedef struct irq_config_s {
    uint8_t irq_num;
    uint8_t priority;
} irq_config_t;

#define RTOS_SAFE_PRIO (0x0F << 4) // Safe IRQ priority for RTOS tasks (lower number = higher priority)

#define LOG_SYS_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_UART), __VA_ARGS__)

int  fflush(FILE *stream);
int  getchar(void);
void uart_flush(void);
void uart_set_echo(bool enabled);
void uart_set_buffered_mode(bool enabled);
void signal_data_ready(void);
bool stdin_ready(int timeout_ms);
void nvic_cfg_peripheral_irqs(const irq_config_t *peripheral_irqs, uint32_t count);

extern bool echo_enabled;
extern bool buffered_mode;
