/**
 * @file event_loop_main.c
 * @brief Main for event loop build.
 * This file contains the main function for the non-RTOS build. Without an RTOS, the main function is an event loop.
 * On bare metal, the event loop handles events from hardware interrupts. On a user-space POSIX system,
 * the event loop is driven by the CLI input.
 */
#include <stdint.h>
#include <unistd.h>

#include "arch_ops.h"
#include "event.h"
#include "utils.h"

// prototypes without include file
int  init_hw(void);
int  init_gpio(void);
int  cli_init(void **cli_ctx);
bool cli_run(void *cli_ctx);
void cli_exit(void *cli_ctx);
void test_pci(void);
void test_pci_msi_irq(int test_idx);
void test_pci_post_msi_irq(int test_idx);

uint64_t get_timestamp48(void);

// -- Globals --

extern volatile bool keep_running;

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

    init_hw();         // initialize hardware
    get_timestamp48(); // start time
#ifdef ENABLE_TEST
    init_gpio();
#endif
    if (cli_init(&cli_ctx))
        return 0;

    while (keep_running) {
#ifdef BARE_METAL
        uint32_t event = __atomic_exchange_n(&event_notify, 0, __ATOMIC_SEQ_CST);

        if (event) {
#ifdef ENABLE_PCI
            if (event & EVT_PCI_TEST) { // is PCI Test event
                test_pci();             // start PCI test (1)
            }
            if (event & EVT_MSI_MASK) {   // is PCI MSI event
                test_pci_post_msi_irq(2); // MSI test epilogue (2.2)
                NOP();                    // Placeholder for tasks that need to run on PCI MSI event
            }
#endif
            if (event & EVT_SYS_TICK) { // is SysTick event
                NOP();                  // Placeholder for tasks that need to run on SysTick
            }
            if (event & EVT_DATA_READY) { // is Data Ready event
                NOP();                    // CLI passthrough for data pending and read from UART
            }
            keep_running = cli_run(cli_ctx);
#ifdef ENABLE_PCI
            if (event & EVT_PCI_TEST) {
                test_pci_msi_irq(2); // start PCI MSI test (2.1) after flashing PCI Test logs
            }
#endif
        }
#else // !BARE_METAL
        keep_running = cli_run(cli_ctx);
#endif
        idle();
    }

    cli_exit(cli_ctx);
    return 0;
}
