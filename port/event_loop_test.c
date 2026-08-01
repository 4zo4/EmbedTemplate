#include <stdint.h>
#include <unistd.h>

#include "gpio_demo_regs.h"
#include "gpio.h"
#include "log.h"
#include "log_marker.h"

// -- Globals --

alignas(8) static volatile gpio_ctrl_t gpio; // Local instance of GPIO registers -
                                             // simulated memory-mapped hardware
// -- End of globals --

int init_gpio(void)
{
    gpio_set_regs(&gpio); // Set the base address for the GPIO driver
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
    return 0;
}
