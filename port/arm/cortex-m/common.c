/**
 * @file common.c
 * @brief Common port-specific implementations for Cortex-M4.
 * This file contains the implementations of the common functions for the Cortex-M4 microcontroller,
 * including system interrupt handlers.
 */
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "event.h"
#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

// Nested Vectored Interrupt Controller (NVIC) Base and Register Definitions (Cortex-M4)
#define NVIC_BASE 0xE000E100
#define NVIC_ISER ((volatile uint32_t *)(NVIC_BASE + 0x000)) // Interrupt Set-Enable Register
#define NVIC_ICPR ((volatile uint32_t *)(NVIC_BASE + 0x280)) // Interrupt Clear-Pending Register

#define NVIC_IPR_BASE 0xE000E400
#define NVIC_IPR ((volatile uint8_t *)(NVIC_IPR_BASE)) // Interrupt Priority Register (byte-accessible priority 0-239)

static const char *enb = "enabled";
static const char *dis = "disabled";

#ifndef ENABLE_RTOS
volatile EVT_BITMAP event_notify;
#endif
bool echo_enabled = true;
bool buffered_mode = false;

void nvic_cfg_peripheral_irqs(const irq_config_t *peripheral_irqs, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint8_t irq = peripheral_irqs[i].irq_num;
        uint8_t prio = peripheral_irqs[i].priority;

        // Set Priority, Clear Pending status, and Enable the Interrupt
        NVIC_IPR[irq] = prio;
        NVIC_ICPR[irq >> 5] = BIT(irq & 31);
        NVIC_ISER[irq >> 5] = BIT(irq & 31);
    }
}

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
    event_notify |= EVT_DATA_READY;
#endif
}

int fflush(FILE *stream)
{
    (void)stream;
    uart_flush();
    return 0;
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
