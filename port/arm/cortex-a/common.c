/**
 * @file common.c
 * @brief Common port-specific implementations for Cortex-A system.
 * This file contains the implementations of the common functions for the Cortex-A.
 */
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "event.h"
#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

static const char *enb = "enabled";
static const char *dis = "disabled";

#ifndef ENABLE_RTOS
volatile EVT_BITMAP event_notify;
#endif
bool echo_enabled = true;
bool buffered_mode = false;

void uart_set_echo(bool enabled)
{
    echo_enabled = enabled;
    LOG_UART_DEBUG("UART echo %s", enabled ? enb : dis);
}

void uart_set_buffered_mode(bool enabled)
{
    buffered_mode = enabled;
    LOG_UART_DEBUG("UART buffered mode %s", enabled ? enb : dis);
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

#ifdef ENABLE_RTOS
extern volatile uint32_t ulPortInterruptNesting;

static inline bool xPortIsInsideInterrupt(void)
{
    return (ulPortInterruptNesting > 0);
}
#endif

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