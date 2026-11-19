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
#include "stats.h"

// System Clock Speed (for Cortex-M33 MPS2-AN505 on QEMU, 25MHz)
#define CPU_HZ 25000000ULL // 25 MHz

// ARM CMSDK APB UART (cmsdk-apb-uart) Base and Register Definitions (Cortex-M33)
#define UART0_BASE_NS 0x40200000UL // UART0 on MPS2-AN505
#define UART0_BASE_SE 0x50200000UL // Secure access
#define UART_BASE_NS(n) (UART0_BASE_NS + ((n) * 0x1000UL))
#define UART_BASE_SE(n) (UART0_BASE_SE + ((n) * 0x1000UL))
#define UART_BASE(n) UART_BASE_NS(n)

#define UART_DATA(n) (*((volatile uint32_t *)(UART_BASE(n) + 0x00UL)))      // Data Register (R/W)
#define UART_STATE(n) (*((volatile uint32_t *)(UART_BASE(n) + 0x04UL)))     // Status Register (R/W)
#define UART_CTRL(n) (*((volatile uint32_t *)(UART_BASE(n) + 0x08UL)))      // Control Register (R/W)
#define UART_INTSTATUS(n) (*((volatile uint32_t *)(UART_BASE(n) + 0x0CUL))) // Interrupt Status (R) / Clear (W)
#define UART_BAUDDIV(n) (*((volatile uint32_t *)(UART_BASE(n) + 0x10UL)))   // Baudrate Divider Register (R/W)

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
#define UART_RX_INTERRUPT_PENDING(n) (UART_INTSTATUS(n) & UART_INTSTATUS_RX)
#define UART_DATA_AVAILABLE(n) (UART_STATE(n) & UART_STATE_RX_FULL)

#ifndef ENABLE_WWDG
#define ENABLE_WWDG 0 // Enable Window Watchdog (WWDG) for Cortex-M33 on MPS2-AN505
#endif

void init_systick_poll(void);
bool systick_poll(void);
void systick_reset(void);

static int uart_rx_null(void *arg, uint8_t byte);

// -- Globals --

alignas(8) uint32_t initialized;
stats_irq_t stats_irq;
stats_tx_t  stats_tx;

static const irq_config_t peripheral_irqs[] = {
    {0,  RTOS_SAFE_PRIO}, // WWDG
    {32, RTOS_SAFE_PRIO}, // UART0 RX
    {34, RTOS_SAFE_PRIO}, // UART1 RX
};

// NimBLE HCI Uart Rx
int (*uart_rx)(void *arg, uint8_t byte) = uart_rx_null;
void *cb_arg = nullptr;

// -- End of globals --

static int uart_rx_null(void *arg, uint8_t byte)
{
    (void)arg;
    (void)byte;
    return 0;
}

void init_uart(void)
{
    if (initialized & UART_INITIALIZED)
        return;

    // Configure UART0 baud rate for 115200
    UART_CTRL(0) = 0;
    UART_BAUDDIV(0) = (CPU_HZ / 115200);
    // Enable Transmission, Reception, and the RX Data Interrupt
    UART_CTRL(0) = (UART_CTRL_TX_EN | UART_CTRL_RX_EN | UART_CTRL_RX_INTEN);

    // Configure UART1
    UART_CTRL(1) = 0;
    UART_BAUDDIV(1) = (CPU_HZ / 115200);
    UART_CTRL(1) = (UART_CTRL_TX_EN | UART_CTRL_RX_EN | UART_CTRL_RX_INTEN);

    nvic_cfg_peripheral_irqs(peripheral_irqs, sizeof(peripheral_irqs) / sizeof(irq_config_t));

    initialized |= UART_INITIALIZED;
    log_set_level(DOMAIN_SYS, ENTITY_UART, LOG_LEVEL_INFO);
    LOG_SYS_INFO("UART initialized with baud rate 115200");
}

void uart_flush(void)
{
    // Wait until transmission is complete
    while (UART_STATE(0) & UART_STATE_TX_FULL) {
        NOP();
    }
}

// also used for NimBLE HCI Uart Tx
int uart_tx(int n, int c)
{
    stats_tx.count.uart[n]++;

    // Wait until previous transmission is complete
    while (UART_STATE(n) & UART_STATE_TX_FULL) {
        NOP();
    }
    UART_DATA(n) = (uint32_t)(c & 0xFF);
    return c;
}

int putchar(int c)
{
    return uart_tx(0, c);
}

// UART0 RX IRQ Handler for Cortex-M33
// Note that handler share the same name as the Cortex-M4 handler,
// but the IRQ number is different (32 for M33 vs 37 for M4)
void UART1_irq_handler(void)
{
    stats_irq.count.uart[0]++;

    while (UART_RX_INTERRUPT_PENDING(0) && UART_DATA_AVAILABLE(0)) {
        UART_INTSTATUS(0) = UART_INTSTATUS_RX; // Clear the RX interrupt flag
        char c = (char)(UART_DATA(0) & 0xFF);

        if (echo_enabled)
            putchar(c);

        fifo_push(c);

        if (!buffered_mode || (c == '\n' || c == '\r'))
            signal_data_ready();
    }
}

void UART2_irq_handler(void)
{
    stats_irq.count.uart[1]++;

    while (UART_RX_INTERRUPT_PENDING(1) && UART_DATA_AVAILABLE(1)) {
        UART_INTSTATUS(1) = UART_INTSTATUS_RX; // Clear the RX interrupt flag
        uint8_t c = (UART_DATA(1) & 0xFF);
        uart_rx(cb_arg, c);
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
