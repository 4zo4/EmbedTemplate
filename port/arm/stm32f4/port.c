/**
 * @file port.c
 * @brief Port-specific implementations for STM32F4.
 * This file contains the implementations of the hardware-specific functions for the STM32F4 microcontroller,
 * including UART initialization and interrupt handlers. It defines the register addresses and bit masks
 * for configuring the UART and NVIC. The UART is set up for 115200 baud communication.
 */
#include <stdint.h>
#include <stdio.h>

#include "arch_ops.h"
#include "fifo.h"
#include "log.h"
#include "log_marker.h"
#include "utils.h"
#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#define LOG_SYS_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_UART), __VA_ARGS__)
#define LOG_SYS_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_UART), __VA_ARGS__)

// STM32F4 UART Base and Register Definitions (Cortex-M4)
#define USART1_BASE 0x40011000
#define USART_SR (*(volatile uint32_t *)(USART1_BASE + 0x00)) // Status Register
#define USART_DR (*(volatile uint32_t *)(USART1_BASE + 0x04)) // Data Register
#define USART_CR1 (*(volatile uint32_t *)(USART1_BASE + 0x0C)) // Control Register 1
#define USART_BRR (*(volatile uint32_t *)(USART1_BASE + 0x08)) // Baud Rate Register

#define USART_SR_TXE BIT(7)     // Transmit Data Register Empty
#define USART_SR_RXNE BIT(5)    // Read Data Register Not Empty
#define USART_CR1_UE BIT(13)    // UART Enable
#define USART_CR1_TE BIT(3)     // Transmitter Enable
#define USART_CR1_RE BIT(2)     // Receiver Enable
#define USART_CR1_RXNEIE BIT(5) // RX Interrupt Enable

// Nested Vectored Interrupt Controller (NVIC) Base and Register Definitions (Cortex-M4)
#define NVIC_BASE 0xE000E100
#define NVIC_ISER ((volatile uint32_t *)(NVIC_BASE + 0x000)) // Interrupt Set-Enable Register
#define NVIC_ICPR ((volatile uint32_t *)(NVIC_BASE + 0x280)) // Interrupt Clear-Pending Register

#define NVIC_IPR_BASE 0xE000E400
#define NVIC_IPR ((volatile uint8_t *)(NVIC_IPR_BASE)) // Interrupt Priority Register (byte-accessible priority 0-239)
#define RTOS_SAFE_PRIO (0x0F << 4)

#ifndef ENABLE_RTOS
volatile uint8_t event_notify = 0;
#endif
static bool echo_enabled = true;
static bool buffered_mode = false;

typedef struct irq_config_s {
    uint8_t irq_num;
    uint8_t priority;
} irq_config_t;

static const irq_config_t peripheral_irqs[] = {
    {0,  RTOS_SAFE_PRIO}, // WWDG
    {37, RTOS_SAFE_PRIO}, // USART1
};

static void nvic_cfg_peripheral_irqs(void)
{
    for (int i = 0; i < (int)(sizeof(peripheral_irqs) / sizeof(irq_config_t)); i++) {
        uint8_t irq = peripheral_irqs[i].irq_num;
        uint8_t prio = peripheral_irqs[i].priority;

        // Set Priority, Clear Pending status, and Enable the Interrupt
        NVIC_IPR[irq] = prio;
        NVIC_ICPR[irq >> 5] = BIT(irq & 31);
        NVIC_ISER[irq >> 5] = BIT(irq & 31);
    }
}

void init_uart(void)
{
    // Configure baud rate for 115200 assuming System Clock (168 MHz)
    // Baud = fCK / (8 * (2 - OVER8) * USARTDIV), APB2 Clock (fCK) is 84 MHz for USART1
    USART_BRR = 0x2D9;
    USART_CR1 |= (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE);

    nvic_cfg_peripheral_irqs();

    log_set_level(DOMAIN_SYS, ENTITY_UART, LOG_LEVEL_INFO);
    LOG_SYS_INFO("UART initialized with baud rate 115200");
}

static const char *enb = "enabled";
static const char *dis = "disabled";

void uart_set_echo(bool enabled)
{
    echo_enabled = enabled;
    LOG_SYS_DEBUG("UART echo %s", enabled ? enb : dis);
}

void uart_set_buffered_mode(bool enabled)
{
    buffered_mode = enabled;
    LOG_SYS_DEBUG("UART buffered mode %s", enabled ? enb : dis);
}

void uart_flush(void)
{
    while (!(USART_SR & BIT(6)))
        ;
}

int putchar(int c)
{
    while (!(USART_SR & USART_SR_TXE))
        ;
    USART_DR = (uint32_t)(c & 0xFF);
    return c;
}

int getchar(void)
{
    return fifo_pop();
}

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

int fflush(FILE *stream)
{
    (void)stream;
    uart_flush();
    return 0;
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

static const char *halt_msg = "[HALT] System locked. Waiting for power-on reset (POR)...\r\n";

void nmi_handler(void)
{
    printf("[CRITICAL] NMI Detected!\r\n");
    printf("%s", halt_msg);

    while (true) {
        HALT_CPU();
    }
}

void hard_fault_handler(void)
{
    printf("[CRITICAL] Hard Fault Detected!\r\n");
    printf("%s", halt_msg);

    while (true) {
        HALT_CPU();
    }
}

void mem_manage_handler(void)
{
    printf("[CRITICAL] Memory Management Fault Detected!\r\n");
    printf("%s", halt_msg);

    while (true) {
        HALT_CPU();
    }
}

void bus_fault_handler(void)
{
    printf("[CRITICAL] Bus Fault Detected!\r\n");
    printf("%s", halt_msg);

    while (true) {
        HALT_CPU();
    }
}

void usage_fault_handler(void)
{
    printf("[CRITICAL] Usage Fault Detected!\r\n");
    printf("%s", halt_msg);

    while (true) {
        HALT_CPU();
    }
}

void debug_mon_handler(void)
{
    printf("[CRITICAL] Debug Monitor Detected!\r\n");
    printf("%s", halt_msg);

    while (true) {
        HALT_CPU();
    }
}

// WWDG Register Addresses (APB1 @ 0x40002C00)
#define WWDG_BASE 0x40002C00
#define WWDG_SR (*(volatile uint32_t *)(WWDG_BASE + 0x04)) // Status Register
#define WWDG_CR (*(volatile uint32_t *)(WWDG_BASE + 0x00)) // Control Register
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
