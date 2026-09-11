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

// System Clock Speed (168MHz for STM32F4)
#define CPU_HZ 168000000ULL

// STM32F4 UART Base and Register Definitions (Cortex-M4)
#define USART1_BASE 0x40011000
#define USART_SR (*(volatile uint32_t *)(USART1_BASE + 0x00))  // Status Register
#define USART_DR (*(volatile uint32_t *)(USART1_BASE + 0x04))  // Data Register
#define USART_CR1 (*(volatile uint32_t *)(USART1_BASE + 0x0C)) // Control Register 1
#define USART_BRR (*(volatile uint32_t *)(USART1_BASE + 0x08)) // Baud Rate Register

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

// -- Globals --

alignas(8) uint32_t initialized;

static const irq_config_t peripheral_irqs[] = {
    {0,  RTOS_SAFE_PRIO}, // WWDG
    {37, RTOS_SAFE_PRIO}, // USART1
};

// -- End of globals --

void init_uart(void)
{
    if (initialized & UART_INITIALIZED)
        return;
    // Configure baud rate for 115200 assuming System Clock (168 MHz)
    // Baud = fCK / (8 * (2 - OVER8) * USARTDIV), APB2 Clock (fCK) is 84 MHz for USART1
    USART_BRR = 0x2D9;
    USART_CR1 |= (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE);

    nvic_cfg_peripheral_irqs(peripheral_irqs, sizeof(peripheral_irqs) / sizeof(irq_config_t));

    initialized |= UART_INITIALIZED;
    log_set_level(DOMAIN_SYS, ENTITY_UART, LOG_LEVEL_INFO);
    LOG_SYS_INFO("UART initialized with baud rate 115200");
}

void uart_flush(void)
{
    // Wait until transmission is complete
    while (!(USART_SR & USART_SR_TC)) {
        NOP();
    }
}

int putchar(int c)
{
    // Wait until the transmit data register is empty
    while (!(USART_SR & USART_SR_TXE)) {
        NOP();
    }
    USART_DR = (uint32_t)(c & 0xFF);
    return c;
}

void UART1_irq_handler(void)
{
    if (USART_SR & USART_SR_RXNE) {
        char c = (char)(USART_DR & 0xFF);

        if (echo_enabled)
            putchar(c);

        fifo_push(c);

        if (!buffered_mode || (c == '\n' || c == '\r'))
            signal_data_ready();
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
