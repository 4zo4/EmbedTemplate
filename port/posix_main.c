/**
 * @file posix_main.c
 * @brief Main for posix build.
 * This file contains the main function for the non-RTOS build. Without an RTOS, the main function is an event loop.
 * On bare metal, the event loop handles events from hardware interrupts.
 * On a POSIX system, the event loop is driven by the CLI input.
 */
#include <stdint.h>
#include <unistd.h>

#include "arch_ops.h"
#include "gpio_demo_regs.h"
#include "gpio.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h"

// prototypes without include file
int      cli_init(void **cli_ctx);
bool     cli_run(void *cli_ctx);
void     cli_exit(void *cli_ctx);
void     init_uart(void);
void     init_systick(void);
void     init_timestamp(void);
uint64_t get_timestamp48(void);
void     init_watchdog(void);

// -- Globals --

extern volatile bool    keep_running;
extern volatile uint8_t event_notify;

// -- End of globals --

void idle(void)
{
#ifdef BARE_METAL
    HALT_CPU();
#else
    /*
     * Force main thread to sleep for a short duration.
     * 1000 microseconds = 1ms
     */
    usleep(1000);
#endif
}

int main(void)
{
    void *cli_ctx;

    alignas(8) static volatile gpio_ctrl_t gpio; // Local instance of GPIO registers -
                                                 // simulated memory-mapped hardware
#ifdef BARE_METAL
    init_timestamp();
    init_systick();
    init_uart();
    init_watchdog();
#endif
    get_timestamp48();                           // start time
    gpio_set_regs(&gpio);                        // Set the base address for the GPIO driver
    // Set log level for gpio
    log_set_level(DOMAIN_DEV, ENTITY_GPIO, LOG_LEVEL_INFO);

    // Initialize the GPIO Controller to a known safe state
    gpio_init_controller(&gpio);

    // Configure Pin 0: Heater (Output), Pin 1: Cooler (Output),
    //           Pin 2: Emergency Cooler (Output),
    //           Pin 0-7: Sensor Inputs (Temperature), Pin 8: Alarm Input
    gpio_init_pin(&gpio, 0, true);
    gpio_init_pin(&gpio, 1, true);
    gpio_init_pin(&gpio, 2, true);
    gpio_init_pin(&gpio, 8, false);
    gpio_init_pin_mask(&gpio, 0xFF, false);

    // Configure and Enable Interrupt for the Alarm Pin
    gpio_configure_interrupt(&gpio, 8, false); // Active High
    // Clear any stale status from the init/boot period
    gpio_clear_interrupt(&gpio, 8);
    // Configure Watchdog with a 150ms timeout
    gpio_wdt_setup(&gpio, 150);

    if (cli_init(&cli_ctx))
        return 0;

    while (keep_running) {
#ifdef BARE_METAL
        uint32_t primask = disable_interrupts();
        uint32_t event = event_notify;
        event_notify = 0; // Clear the events
        restore_interrupts(primask);

        if (event) {
            if (event & BIT(0)) { // is SysTick event
                NOP(); // Placeholder for tasks that need to run on SysTick 
            }
            if (event & BIT(1)) { // is Data Ready event
                NOP(); // CLI passthrough for data pending and read from UART
            }
            keep_running = cli_run(cli_ctx);
        }
#else
        keep_running = cli_run(cli_ctx);
#endif
        idle();
    }

    cli_exit(cli_ctx);
    return 0;
}
