/**
 * @file init_hw.c
 * @brief A hardware initialization function for the bare-metal.
 */
#include <stdint.h>

#include "init.h"

void init_profiler(void); // prototype without include file

int init_hw(void)
{
#ifdef BARE_METAL
    if (init_port_globals)
        init_port_globals();
    init_timestamp();
#ifdef ENABLE_PROF
    init_profiler();
#endif
#ifndef ENABLE_RTOS
    init_systick();
#endif
    init_uart();
    init_watchdog();
#ifdef ENABLE_PCI
    init_pci();
#endif
#endif // BARE_METAL
    return 0;
}
