/**
 * @file time.c
 * @brief Timestamp and system tick implementation for STM32F4.
 * This file implements the timestamp and system tick functionality for the STM32F4 microcontroller
 * using the Data Watchpoint and Trace (DWT) unit for high-resolution timing. The timestamp is based on the DWT cycle counter,
 * which provides a 48-bit timestamp with microsecond resolution.
 * The system tick is implemented using the SysTick timer, configured to generate an interrupt every 1 millisecond.
 * The timestamp and SysTick initialization functions should be called during system startup to set up the timing infrastructure.
 * @note The System Tick implementation is only included for non-RTOS environments, as an RTOS would typically provide its own tick mechanism.
 */
#include <stdint.h>

#include "log.h"
#include "log_marker.h"
#include "utils.h"

#define LOG_SYS_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_TIMER), __VA_ARGS__)

#define DWT_CONTROL (*((volatile uint32_t *)0xE0001000))
#define DWT_CYCCNT (*((volatile uint32_t *)0xE0001004))
#define DWT_LAR (*((volatile uint32_t *)0xE0001FB0))
#define DWT_LAR_UNLOCK 0xC5ACCE55
#define DEMCR (*((volatile uint32_t *)0xE000EDFC))
#define DEMCR_TRCENA BIT(24)
#define DWT_CYCCNTENA BIT(0)

// System Clock Speed (168MHz for STM32F4)
#define CPU_HZ 168000000ULL

static uint64_t boot_ts = 0;

void init_timestamp(void)
{
#ifdef TARGET_HW_STM32F4
    DWT_LAR = DWT_LAR_UNLOCK; // Unlock DWT access
#endif
    DEMCR |= DEMCR_TRCENA;        // Enable Trace
    DWT_CYCCNT = 0;               // Reset cycle counter
    DWT_CONTROL |= DWT_CYCCNTENA; // Start cycle counter

    log_set_level(DOMAIN_SYS, ENTITY_TIMER, LOG_LEVEL_INFO);
    LOG_SYS_INFO("Timestamp initialized");
}

uint64_t get_timestamp48(void)
{
    uint32_t cycles = DWT_CYCCNT;
    uint64_t now = ((uint64_t)cycles * 1000000ULL) / CPU_HZ;

    if (boot_ts == 0) {
        boot_ts = now;
    }

    return (now - boot_ts) & 0xFFFFFFFFFFFFULL;
}

#ifndef ENABLE_RTOS

// System Control Block (SCB) Base and System Handler Priority (SHPR) Register Definitions for Cortex-M4
#define SCB_BASE 0xE000ED00
#define SCB_SHPR2 (*(volatile uint32_t *)(SCB_BASE + 0x1C)) // SVC Priority
#define SCB_SHPR3 (*(volatile uint32_t *)(SCB_BASE + 0x20)) // SysTick Priority

static void scb_cfg_system_irqs(void)
{
    // Set Supervisor Call (SVC) and SysTick Priority at the lowest priority level
    SCB_SHPR2 |= (0xF0 << 24);
    SCB_SHPR3 |= (0xF0 << 24);
}

// System Tick (SysTick) Configuration Registers Definitions for Cortex-M4
#define STK_CONTROL (*((volatile uint32_t *)0xE000E010))
#define STK_LOAD (*((volatile uint32_t *)0xE000E014))
#define STK_VAL (*((volatile uint32_t *)0xE000E018))
#define STK_CALIB (*((volatile uint32_t *)0xE000E01C))

#define STK_CTRL_ENABLE BIT(0)
#define STK_CTRL_TICKINT BIT(1)
#define STK_CTRL_CLKSOURCE BIT(2)
#define STK_CTRL_COUNTFLAG BIT(16)

extern volatile uint8_t  event_notify;
static volatile uint32_t system_ticks = 0;

void init_systick(void)
{
    // Calculate reload value for 1ms (ticks per milli)
    STK_LOAD = (CPU_HZ / 1000) - 1;
    STK_VAL = 0;
    STK_CONTROL = (STK_CTRL_CLKSOURCE | STK_CTRL_TICKINT | STK_CTRL_ENABLE);

    scb_cfg_system_irqs();
    LOG_SYS_INFO("SysTick initialized for 1ms ticks");
}

void SysTick_Handler(void)
{
    system_ticks++;

    /*
     * With this setup SysTick event is 1024 milliseconds (1.024 seconds).
     * Alternatively, you can use & 127 for 128ms or & 255 for 256ms, etc. for the desired frequency of the event.
     */
    if ((system_ticks & (1024 - 1)) == 0) {
        event_notify |= BIT(0);
    }
}

#endif // !ENABLE_RTOS
