#pragma once

#ifdef BARE_METAL

#define GLOBALS 0
#define TIMESTAMP 1
#define UART 2
#define SYSTICK 3
#define WATCHDOG 4
#define PCI 5

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#define GLOBALS_INITIALIZED BIT(GLOBALS)
#define TIMESTAMP_INITIALIZED BIT(TIMESTAMP)
#define UART_INITIALIZED BIT(UART)
#define SYSTICK_INITIALIZED BIT(SYSTICK)
#define WATCHDOG_INITIALIZED BIT(WATCHDOG)
#define PCI_INITIALIZED BIT(PCI)

void init_pci(void);
void init_uart(void);
void init_systick(void);
void init_timestamp(void);
void init_watchdog(void);

extern void (*volatile init_port_globals)(void);

extern uint32_t initialized;

#endif
