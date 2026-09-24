/**
 * @file port.c
 * @brief Port-specific implementations for Cortex-M33 MPS2-AN505.
 * This file contains the implementations of the hardware-specific functions for the Cortex-M33 MPS2-AN505 microcontroller,
 * including UART initialization and Watchdog interrupt handler. It defines the register addresses and bit masks
 * for configuring the UART and NVIC. The UART is set up for 115200 baud communication.
 */
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "init.h"

typedef struct stats_irq_s {
    struct count_s {
        uint32_t uart;
        uint32_t wdg;
    } count;
} stats_irq_t;

// System Clock Speed (for Cortex-M33 MPS2-AN505 on QEMU, 25MHz)
#define CPU_HZ 25000000ULL // 25 MHz

// ARM CMSDK APB UART Base and Register Definitions (Cortex-M33)
#define UART0_BASE_NS 0x40200000UL // UART0 on MPS2-AN505
#define UART0_BASE_SE 0x50200000UL // Secure access
#define UART0_BASE UART0_BASE_NS
#define UART0_DATA (*((volatile uint32_t *)(UART0_BASE + 0x00)))
#define UART0_STATE (*((volatile uint32_t *)(UART0_BASE + 0x04)))
#define UART0_CTRL (*((volatile uint32_t *)(UART0_BASE + 0x08)))
#define UART0_BAUDDIV (*((volatile uint32_t *)(UART0_BASE + 0x10)))
#define UART0_INTSTATUS (*((volatile uint32_t *)(UART0_BASE + 0x0C)))

// UART State Register Bitmasks
#define UART_STATE_TX_FULL BIT(0) // Transmit Data Register Full
#define UART_STATE_RX_FULL BIT(1) // Receive Data Register Full
// UART Control Register Bitmasks
#define UART_CTRL_TX_EN BIT(0)    // Transmitter Enable
#define UART_CTRL_RX_EN BIT(1)    // Receiver Enable
#define UART_CTRL_TX_INTEN BIT(2) // Transmit Interrupt Enable
#define UART_CTRL_RX_INTEN BIT(3) // Receive Interrupt Enable
// UART Interrupt Status Register Bitmasks
#define UART_INTSTATUS_RX BIT(1) // RX Interrupt Pending

// Helper macros for checking UART status
#define UART_RX_INTERRUPT_PENDING (UART0_INTSTATUS & UART_INTSTATUS_RX)
#if 0 // Enable this if you want to check for data availability in the UART RX FIFO
#define UART_DATA_AVAILABLE (UART0_STATE & UART_STATE_RX_FULL)
#else
#define UART_DATA_AVAILABLE 1
#endif

#ifndef ENABLE_WWDG
#define ENABLE_WWDG 0 // Enable Window Watchdog (WWDG) for Cortex-M33 on MPS2-AN505
#endif

void init_systick_poll(void);
bool systick_poll(void);
void systick_reset(void);

// -- Globals --

alignas(8) uint32_t initialized;
stats_irq_t stats_irq;

static const irq_config_t peripheral_irqs[] = {
    {0,  RTOS_SAFE_PRIO}, // WWDG
    {32, RTOS_SAFE_PRIO}, // UART0 RX
};

// -- End of globals --

void init_uart(void)
{
    if (initialized & UART_INITIALIZED)
        return;

    // Configure baud rate for 115200 assuming System Clock (168 MHz)
    UART0_CTRL = 0;
    UART0_BAUDDIV = (CPU_HZ / 115200);
    // Enable Transmission, Reception, and the RX Data Interrupt
    UART0_CTRL = (UART_CTRL_TX_EN | UART_CTRL_RX_EN | UART_CTRL_RX_INTEN);

    nvic_cfg_peripheral_irqs(peripheral_irqs, sizeof(peripheral_irqs) / sizeof(irq_config_t));

    initialized |= UART_INITIALIZED;
    log_set_level(DOMAIN_SYS, ENTITY_UART, LOG_LEVEL_INFO);
    LOG_SYS_INFO("UART initialized with baud rate 115200");
}

void uart_flush(void)
{
    // Wait until transmission is complete
    while (UART0_STATE & UART_STATE_TX_FULL) {
        NOP();
    }
}

int putchar(int c)
{
    // Wait until previous transmission is complete
    while (UART0_STATE & UART_STATE_TX_FULL) {
        NOP();
    }
    UART0_DATA = (uint32_t)(c & 0xFF);
    return c;
}

// UART0 RX IRQ Handler for Cortex-M33
// Note that handler share the same name as the Cortex-M4 handler,
// but the IRQ number is different (32 for M33 vs 37 for M4)
void UART_irq_handler(void)
{
    stats_irq.count.uart++;

    if (UART_RX_INTERRUPT_PENDING && UART_DATA_AVAILABLE) {
        char c = (char)(UART0_DATA & 0xFF);

        UART0_INTSTATUS = UART_INTSTATUS_RX; // Clear the RX interrupt flag

        if (echo_enabled)
            putchar(c);

        fifo_push(c);

        if (!buffered_mode || (c == '\n' || c == '\r'))
            signal_data_ready();
    }
}

void secure_fault_handler(void)
{
    printf("[CRITICAL] Secure Fault Detected!\r\n");
    while (true) {
        HALT_CPU();
    }
}

#define WATCHDOG_BASE_NS 0x40081000UL // APB Watchdog on MPS2-AN505
#define WATCHDOG_BASE_SE 0x50081000UL // Secure access
#define WATCHDOG_BASE WATCHDOG_BASE_NS
#define WWDG_LOAD (*((volatile uint32_t *)(WATCHDOG_BASE + 0x00)))
#define WWDG_VALUE (*((volatile uint32_t *)(WATCHDOG_BASE + 0x04)))
#define WWDG_CTRL (*((volatile uint32_t *)(WATCHDOG_BASE + 0x08)))
#define WWDG_INTCLR (*((volatile uint32_t *)(WATCHDOG_BASE + 0x0C)))
#define WWDG_RIS (*((volatile uint32_t *)(WATCHDOG_BASE + 0x10)))

// CMSDK Watchdog Control Bitmasks
#define WWDG_CTRL_INTEN BIT(0) // Enable Watchdog Interrupt/Counter
#define WWDG_CTRL_RESEN BIT(1) // Enable Reset on secondary timeout

void init_watchdog(void)
{
#if ENABLE_WWDG
    WWDG_CTRL = 0; // Disable Watchdog before configuration
    WWDG_LOAD = 0x00FFFFFFUL;
    // WWDG_CTRL = (WWDG_CTRL_INTEN | WWDG_CTRL_RESEN);
    WWDG_CTRL = WWDG_CTRL_INTEN;
#endif
}

void WWDG_irq_handler(void)
{
    WWDG_INTCLR = 1;
    printf("[CRITICAL] Window Watchdog Interrupt Detected!\r\n");
    while (true) {
        HALT_CPU();
    }
}

// ARM CMSDK Timer Register Definitions
#define CMSDK_TIMER0_BASE_NS 0x40001000UL // CMSDK Timer 0
#define CMSDK_TIMER0_BASE_SE 0x50001000UL // Secure access
#define CMSDK_TIMER0_BASE CMSDK_TIMER0_BASE_NS
#define TIMER_BASE CMSDK_TIMER0_BASE

#define TIMER_CONTROL (*((volatile uint32_t *)(TIMER_BASE + 0x00)))  // CONTROL Register
#define TIMER_VALUE (*((volatile uint32_t *)(TIMER_BASE + 0x04)))    // VALUE Register
#define TIMER_LOAD (*((volatile uint32_t *)(TIMER_BASE + 0x08)))     // RELOAD Register
#define TIMER_INTCLEAR (*((volatile uint32_t *)(TIMER_BASE + 0x0C))) // INTSTATUS/INTCLEAR Register

// Control Register Masks
#define TIMER_CTRL_INTEN BIT(3)  // Timer Interrupt Enable Output
#define TIMER_CTRL_SELECT BIT(2) // External Input Select (1=External, 0=Internal Clock)
#define TIMER_CTRL_RELOAD BIT(1) // Reload capability from Load register on underflow
#define TIMER_CTRL_ENABLE BIT(0) // Counter Enable
#define TIMER_CTRL_INIT (TIMER_CTRL_ENABLE | TIMER_CTRL_RELOAD)

// helper function to calibrate the hardware timer against SysTick
static uint32_t calibrate_hw_timer(void)
{
    while (!systick_poll()) {
        NOP();
    }

    const uint32_t cal_window_ms = 50;

    uint32_t cal_start = TIMER_VALUE;

    for (uint32_t ms = 0; ms < cal_window_ms; ms++) {
        while (!systick_poll()) {
            NOP();
        }
    }

    uint32_t cal_end = TIMER_VALUE;
    uint32_t delta = cal_start - cal_end;

    // calculate the number of timer cycles per millisecond
    return delta * (1000 / cal_window_ms);
}

uint32_t init_hw_timer(volatile uintptr_t *timer_hz)
{
    TIMER_CONTROL = 0;         // Reset timer before configuration
    TIMER_LOAD = 0xFFFFFFFFUL; // Load max 32-bit ceiling threshold
    TIMER_CONTROL = TIMER_CTRL_INIT;
    __asm volatile("dsb sy; isb sy");

    init_systick_poll(); // Initialize SysTick for 1ms polling
    /*
     * Calibrate the hardware timer against SysTick and change
     * the timer_hz variable to the calibrated value.
     */
    *timer_hz = calibrate_hw_timer();
    systick_reset();

    return 0xFFFFFFFFUL - TIMER_VALUE;
}

uint32_t get_hw_timer_cycles(void)
{
    return 0xFFFFFFFFUL - TIMER_VALUE;
}

static void init_cortex_m33_globals(void)
{
    if (initialized & GLOBALS_INITIALIZED)
        return;
    cpu_hz = CPU_HZ;

    initialized |= GLOBALS_INITIALIZED;
}

void (*volatile init_port_globals)(void) = init_cortex_m33_globals;
