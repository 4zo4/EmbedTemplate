/**
 * @file port.c
 * @brief Port-specific implementations for STM32F4.
 * This file contains the implementations of the hardware-specific functions for the STM32F4 microcontroller,
 * including UART initialization and Watchdog interrupt handler. It defines the register addresses and bit masks
 * for configuring the UART and NVIC. The UART is set up for 115200 baud communication.
 */
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "init.h"
#include "stats.h"

// System Clock Speed (168MHz for STM32F4)
#define CPU_HZ 168000000ULL

// stm32f2xx-usart Base and Register Definitions (Cortex-M4)
#define USART1_BASE 0x40011000UL
#define USART2_BASE 0x40004400UL
#define USART3_BASE 0x40004800UL
#define USART4_BASE 0x40004C00UL
#define USART5_BASE 0x40005000UL
#define USART6_BASE 0x40011400UL

#define USART_BASE(n) ( \
    ((n) == 0)     ? USART1_BASE : \
        ((n) == 1) ? USART2_BASE : \
        ((n) == 2) ? USART3_BASE : \
        ((n) == 3) ? USART4_BASE : \
        ((n) == 4) ? USART5_BASE : \
        ((n) == 5) ? USART6_BASE : \
                     0UL \
)

#define USART_SR(n) (*(volatile uint32_t *)(USART_BASE(n) + 0x00UL))  // Status Register
#define USART_DR(n) (*(volatile uint32_t *)(USART_BASE(n) + 0x04UL))  // Data Register
#define USART_BRR(n) (*(volatile uint32_t *)(USART_BASE(n) + 0x08UL)) // Baud Rate Register
#define USART_CR1(n) (*(volatile uint32_t *)(USART_BASE(n) + 0x0CUL)) // Control Register 1
#define USART_CR2(n) (*(volatile uint32_t *)(USART_BASE(n) + 0x10UL)) // Control Register 2
#define USART_CR3(n) (*(volatile uint32_t *)(USART_BASE(n) + 0x14UL)) // Control Register 3

#define USART_SR_TC BIT(6)      // Transmission Complete
#define USART_SR_TXE BIT(7)     // Transmit Data Register Empty
#define USART_SR_RXNE BIT(5)    // Read Data Register Not Empty
#define USART_CR1_UE BIT(13)    // UART Enable
#define USART_CR1_TE BIT(3)     // Transmitter Enable
#define USART_CR1_RE BIT(2)     // Receiver Enable
#define USART_CR1_RXNEIE BIT(5) // RX Interrupt Enable

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
    {37, RTOS_SAFE_PRIO}, // USART1
    {38, RTOS_SAFE_PRIO}, // USART2
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
    // Configure USART1 baud rate for 115200 assuming System Clock (168 MHz)
    // Baud = fCK / (8 * (2 - OVER8) * USARTDIV), APB2 Clock (fCK) is 84 MHz for USART1
    USART_BRR(0) = 0x2D9;
    USART_CR1(0) |= (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE);

    // Configure USART2 for 115200 baud (APB1 = 42 MHz) and enable Tx, Rx, RxNE IRQs
    USART_BRR(1) = 0x16D;
    USART_CR1(1) |= (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE);

    nvic_cfg_peripheral_irqs(peripheral_irqs, sizeof(peripheral_irqs) / sizeof(irq_config_t));

    initialized |= UART_INITIALIZED;
    log_set_level(DOMAIN_SYS, ENTITY_UART, LOG_LEVEL_INFO);
    LOG_SYS_INFO("UART initialized with baud rate 115200");
}

void uart_flush(void)
{
    // Wait until transmission is complete
    while (!(USART_SR(0) & USART_SR_TC)) {
        NOP();
    }
}

// also used for NimBLE HCI Uart Tx
int uart_tx(int n, int c)
{
    stats_tx.count.uart[n]++;

    // Wait until the transmit data register is empty
    while (!(USART_SR(n) & USART_SR_TXE)) {
        NOP();
    }
    USART_DR(n) = (uint32_t)(c & 0xFF);
    return c;
}

int putchar(int c)
{
    return uart_tx(0, c);
}

void UART1_irq_handler(void)
{
    stats_irq.count.uart[0]++;

    if (USART_SR(0) & USART_SR_RXNE) {
        char c = (char)(USART_DR(0) & 0xFF);

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

    if (USART_SR(1) & USART_SR_RXNE) {
        uint8_t c = (USART_DR(1) & 0xFF);
        uart_rx(cb_arg, c);
    }
}

// WWDG Register Addresses (APB1 @ 0x40002C00)
#define WWDG_BASE 0x40002C00
#define WWDG_SR (*(volatile uint32_t *)(WWDG_BASE + 0x04))  // Status Register
#define WWDG_CR (*(volatile uint32_t *)(WWDG_BASE + 0x00))  // Control Register
#define WWDG_CFR (*(volatile uint32_t *)(WWDG_BASE + 0x03)) // Configuration Register

void init_watchdog(void)
{
    WWDG_CFR |= BIT(9);
    WWDG_CR = (BIT(7) | 0x7F);
}

void WWDG_irq_handler(void)
{
    WWDG_SR &= ~BIT(0);
    printf("[CRITICAL] Window Watchdog Interrupt Detected!\r\n");
    while (true) {
        HALT_CPU();
    }
}

// RCC Peripheral Clock Enable Register for TIM5 (bit 3 of AHB1/APB1 depending on bus)
// (layout for QEMU STM32F4 netduinoplus2)
#define RCC_APB1ENR (*((volatile uint32_t *)0x40023840))
#define RCC_APB1ENR_TIM5EN BIT(3)
// TIM5 32-bit Base Addresses
#define TIM5_BASE 0x40000C00
#define TIM5_CR1 (*((volatile uint32_t *)(TIM5_BASE + 0x00)))
#define TIM5_PSC (*((volatile uint32_t *)(TIM5_BASE + 0x28)))
#define TIM5_ARR (*((volatile uint32_t *)(TIM5_BASE + 0x2C)))
#define TIM5_CNT (*((volatile uint32_t *)(TIM5_BASE + 0x24)))
#define TIM_CR1_CEN BIT(0) // Counter Enable bit

// helper function to calibrate the hardware timer against SysTick
static uint32_t calibrate_hw_timer(void)
{
    while (!systick_poll()) {
        NOP();
    }

    const uint32_t cal_window_ms = 50;
    uint32_t       cal_start = TIM5_CNT;

    for (uint32_t ms = 0; ms < cal_window_ms; ms++) {
        while (!systick_poll()) {
            NOP();
        }
    }

    uint32_t cal_end = TIM5_CNT;
    uint32_t delta = cal_end - cal_start;

    // calculate the number of timer cycles per millisecond
    return delta * (1000 / cal_window_ms);
}

uint32_t init_hw_timer(volatile uintptr_t *timer_hz)
{
    RCC_APB1ENR |= RCC_APB1ENR_TIM5EN;

    // Set Prescaler to 0 (run at full timer clock speed)
    TIM5_PSC = 0;

    // Set Auto-Reload to max value for a 32-bit counter
    TIM5_ARR = 0xFFFFFFFFUL;

    TIM5_CNT = 0;
    TIM5_CR1 |= TIM_CR1_CEN;

    init_systick_poll(); // Initialize SysTick for 1ms polling
    /*
     * Calibrate the hardware timer against SysTick and change
     * the timer_hz variable to the calibrated value.
     */
    *timer_hz = calibrate_hw_timer();
    systick_reset();

    return TIM5_CNT;
}

uint32_t get_hw_timer_cycles(void)
{
    return TIM5_CNT;
}

static void init_stm32f4_globals(void)
{
    if (initialized & GLOBALS_INITIALIZED)
        return;
    cpu_hz = CPU_HZ;
    initialized |= GLOBALS_INITIALIZED;
}

void (*volatile init_port_globals)(void) = init_stm32f4_globals;
