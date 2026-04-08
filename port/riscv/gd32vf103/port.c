/**
 * @file port.c
 * @brief Port-specific implementations for GD32VF103.
 * This file contains the implementations of the hardware-specific functions for the GD32VF103 microcontroller,
 * including UART initialization and interrupt handlers. It also supports Pico SDK's hooks for UART putc and getc.
 */
#include <stdint.h>
#include <stdio.h>

#include "fifo.h"
#include "utils.h"
#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#undef getchar
#undef putchar

// GD32VF103 UART Base and Register Definitions
#define UART0_BASE 0x40013800
#define UART_STAT (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_DATA (*(volatile uint32_t *)(UART0_BASE + 0x04))
#define UART_BAUD (*(volatile uint32_t *)(UART0_BASE + 0x08))
#define UART_CTL0 (*(volatile uint32_t *)(UART0_BASE + 0x0C))

#define UART_RBNE BIT(5) // Read Data Buffer Not Empty
#define UART_TBE BIT(7)  // Transmit Buffer Empty

#define RCU_BASE 0x40021000
#define RCU_APB2EN (*(volatile uint32_t *)(RCU_BASE + 0x18))
#define ECLIC_BASE 0xD2000000
#define ECLIC_INT_IE(id) (*(volatile uint8_t *)(ECLIC_BASE + 0x1001 + (id) * 4))
#define ECLIC_INT_IP(id) (*(volatile uint8_t *)(ECLIC_BASE + 0x1000 + (id) * 4))
#define ECLIC_INT_ATTR(id) (*(volatile uint8_t *)(ECLIC_BASE + 0x1002 + (id) * 4))
#define GPIOA_CTL1 (*(volatile uint32_t *)0x40010804)

#ifndef ENABLE_RTOS
volatile uint8_t event_notify = 0;
#endif
static bool echo_enabled = true;
static bool buffered_mode = false;

void init_uart(void)
{
    // Enable Clocks (GPIOA + UART0)
    RCU_APB2EN |= BIT(2) | BIT(14);

    // Configure Pins (PA9 = TX, PA10 = RX)
    // PA9: Alternate Function Push-Pull (0x9), PA10: Floating Input (0x4)
    // GPIOA_CTL1 covers pins 8-15
    GPIOA_CTL1 &= ~(0xFF << 4); // Clear PA9 and PA10
    GPIOA_CTL1 |= (0x49 << 4);  // PA9 = 0x9 (AFPP), PA10 = 0x4 (Input)

    // Configure Baud Rate (Assuming 108MHz APB2)
    // 115200 Baud: 108,000,000 / (16 * 115200) = 58.59
    // Integer: 58 (0x3A), Fractional: 0.59 * 16 = 9 (0x9)
    UART_BAUD = (58 << 4) | 9;

    // Enable UART Peripheral (UEN) and RX/TX (REN/TEN)
    UART_CTL0 |= BIT(13) | BIT(3) | BIT(2); // UEN, TEN, REN

    // Enable UART RXNE Interrupt at Peripheral Level
    UART_CTL0 |= UART_RBNE;

    // Configure ECLIC for UART0 (ID 56)
    ECLIC_INT_ATTR(56) = 0x3; // Trigger: Level, Type: Vectored
    ECLIC_INT_IE(56) = 1;     // Enable Interrupt

    // Global Interrupt Enable (MIE bit in mstatus)
    __asm volatile("csrs mstatus, 8");
}

int putchar(int c)
{
    while (!(UART_STAT & UART_TBE))
        ;
    UART_DATA = (uint8_t)c;
    return c;
}

int getchar(void)
{
    return fifo_pop();
}

#ifdef ENABLE_RTOS
/**
 * RISC-V helper to determine if we are in an interrupt context.
 * The 'mcause' register has the high bit set (31 for RV32) during interrupts.
 */
static inline bool xPortIsInsideInterrupt(void)
{
    uintptr_t cause;
    __asm volatile("csrr %0, mcause" : "=r"(cause));
    return (cause >> 31) != 0;
}
#endif

bool stdin_ready(int timeout_ms)
{
    if (!fifo_is_empty())
        return true;
#ifdef ENABLE_RTOS
    extern TaskHandle_t xCliHandle;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED && xCliHandle != nullptr) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) > 0) {
            return true;
        }
        return false;
    }
#else
    (void)timeout_ms;
#endif
    return false;
}

void signal_data_ready(void)
{
#ifdef ENABLE_RTOS
    extern TaskHandle_t xCliHandle;
    if (xCliHandle == nullptr)
        return;

    if (xPortIsInsideInterrupt()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(xCliHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        xTaskNotifyGive(xCliHandle);
    }
#else
    event_notify |= BIT(1);
#endif
}

void uart_flush(void)
{
    while (!(UART_STAT & UART_TBE))
        ;
}

int fflush(FILE *stream)
{
    (void)stream;
    uart_flush();
    return 0;
}

void uart_set_echo(bool enabled)
{
    echo_enabled = enabled;
}

void uart_set_buffered_mode(bool enabled)
{
    buffered_mode = enabled;
}

void __attribute__((interrupt)) UART0_irq_handler(void)
{
    if (UART_STAT & UART_RBNE) {
        char c = (char)UART_DATA;

        if (echo_enabled)
            putchar(c);

        fifo_push(c);

        if (!buffered_mode || (c == '\n' || c == '\r'))
            signal_data_ready();
    }
}

// FWDGT (Free Watchdog) Base and Register Definitions
#define FWDGT_BASE 0x40003000
#define FWDGT_CTL (*(volatile uint32_t *)(FWDGT_BASE + 0x00))
#define FWDGT_PSC (*(volatile uint32_t *)(FWDGT_BASE + 0x04))
#define FWDGT_RLD (*(volatile uint32_t *)(FWDGT_BASE + 0x08))
#define FWDGT_STAT (*(volatile uint32_t *)(FWDGT_BASE + 0x0C))

void init_watchdog(void)
{
#if 0
    /* 0x5555 unlocks registers, 0xCCCC starts watchdog, 0xAAAA reloads */
    FWDGT_CTL = 0x5555; // Unlock
    FWDGT_PSC = 0x03;   // Prescaler /32 (~40kHz LSI / 32 = 1.25kHz)
    FWDGT_RLD = 1250;   // ~1 second timeout
    FWDGT_CTL = 0xAAAA; // Reload
    FWDGT_CTL = 0xCCCC; // Start
#endif
}

void watchdog_feed(void)
{
    FWDGT_CTL = 0xAAAA;
}

#ifdef TARGET_LIB_PICO

static int uart_putc(char c, FILE *file)
{
    (void)file;
    putchar(c);
    return 0;
}

static int uart_getc(FILE *file)
{
    (void)file;
    return getchar();
}

static FILE __stdio_stream = FDEV_SETUP_STREAM(uart_putc, uart_getc, NULL, _FDEV_SETUP_RW);

FILE *const stdin = &__stdio_stream;
FILE *const stdout = &__stdio_stream;
FILE *const stderr = &__stdio_stream;

#endif