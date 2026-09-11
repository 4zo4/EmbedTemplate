/**
 * @file time.c
 * @brief Timestamp and system tick implementation for Cortex-M.
 * This file implements the timestamp and system tick functionality for the Cortex-M microcontroller
 * using the Data Watchpoint and Trace (DWT) unit for high-resolution timing. The timestamp is based on the DWT cycle counter,
 * which provides a 48-bit timestamp with microsecond resolution.
 * The system tick is implemented using the SysTick timer, configured to generate an interrupt every 1 millisecond.
 * The timestamp and SysTick initialization functions should be called during system startup to set up the timing infrastructure.
 * @note The System Tick implementation is only included for non-RTOS environments, as an RTOS would typically provide its own tick mechanism.
 */
#include <stdint.h>
#include <stdio.h>

#include "assert.h"
#include "common.h"
#include "event.h"
#include "init.h"

#define LOG_TIME_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_TIMER), __VA_ARGS__)

// Data Watchpoint and Trace (DWT) cycle counter
#define DWT_CONTROL (*((volatile uint32_t *)0xE0001000))
#define DWT_CYCCNT (*((volatile uint32_t *)0xE0001004))
#define DWT_LAR (*((volatile uint32_t *)0xE0001FB0))
#define DWT_LAR_UNLOCK 0xC5ACCE55
#define DEMCR (*((volatile uint32_t *)0xE000EDFC))
#define DEMCR_TRCENA BIT(24)
#define DWT_CYCCNTENA BIT(0)

#define CPU_HZ cpu_hz

uint32_t init_hw_timer(volatile uintptr_t *timer_hz);
uint32_t get_hw_timer_cycles(void);

// -- Globals --

// user port have to set platform-specific CPU System Clock Speed
alignas(8) volatile uintptr_t cpu_hz;
alignas(8) volatile uintptr_t timer_hz;
alignas(8) static uint64_t boot_ts = 0;
alignas(8) static uint64_t total_cycles = 0;
static uint32_t last_cycles = 0;
static bool     hw_timer = false;

// -- End of globals --

void init_timestamp(void)
{
    if (initialized & TIMESTAMP_INITIALIZED)
        return;

    assert((CPU_HZ != 0) && "[ERROR] Undefined CPU System Clock Speed.");

#ifdef TARGET_CHIP_HW
    DWT_LAR = DWT_LAR_UNLOCK; // Unlock DWT access
#endif
    DEMCR |= DEMCR_TRCENA;        // Enable Trace
    DWT_CYCCNT = 0;               // Reset cycle counter
    DWT_CONTROL |= DWT_CYCCNTENA; // Start cycle counter

    boot_ts = 0;
    total_cycles = 0;
    for (volatile int i = 0; i < 100; i++) {
        NOP();
    }
    last_cycles = DWT_CYCCNT;
    if (last_cycles) {
        timer_hz = CPU_HZ;
    } else {
        last_cycles = init_hw_timer(&timer_hz);
        hw_timer = true;
    }
    initialized |= TIMESTAMP_INITIALIZED;
    log_set_level(DOMAIN_SYS, ENTITY_TIMER, LOG_LEVEL_INFO);
    LOG_TIME_INFO("Timestamp initialized %s with timer_hz=%lu", hw_timer ? "using HW timer" : "using DWT cycle counter", (unsigned long)timer_hz);
}

uint64_t get_timestamp48(void)
{
    uint32_t cycles = hw_timer ? get_hw_timer_cycles() : DWT_CYCCNT;
    uint32_t delta = cycles - last_cycles;
    last_cycles = cycles;
    total_cycles += delta;
    uint64_t now = (total_cycles * 1000000ULL) / timer_hz;

    if (boot_ts == 0) {
        boot_ts = now;
    }

    return (now - boot_ts) & 0xFFFFFFFFFFFFULL;
}

// System Tick (SysTick) Configuration Registers Definitions for Cortex-M
#define STK_CONTROL (*((volatile uint32_t *)0xE000E010))
#define STK_LOAD (*((volatile uint32_t *)0xE000E014))
#define STK_VAL (*((volatile uint32_t *)0xE000E018))
#define STK_CALIB (*((volatile uint32_t *)0xE000E01C))

#define STK_CTRL_ENABLE BIT(0)
#define STK_CTRL_TICKINT BIT(1)
#define STK_CTRL_CLKSOURCE BIT(2)
#define STK_CTRL_COUNTFLAG BIT(16)

#ifndef ENABLE_RTOS

// System Control Block (SCB) Base and System Handler Priority (SHPR) Register Definitions for Cortex-M
#define SCB_BASE 0xE000ED00
#define SCB_SHPR2 (*(volatile uint32_t *)(SCB_BASE + 0x1C)) // SVC Priority
#define SCB_SHPR3 (*(volatile uint32_t *)(SCB_BASE + 0x20)) // SysTick Priority

static void scb_cfg_system_irqs(void)
{
    // Set Supervisor Call (SVC) and SysTick Priority at the lowest priority level
    SCB_SHPR2 |= (0xF0 << 24);
    SCB_SHPR3 |= (0xF0 << 24);
}

static volatile uint32_t system_ticks = 0;

void init_systick(void)
{
    if (initialized & SYSTICK_INITIALIZED)
        return;
    // Calculate reload value for 1ms (ticks per milli)
    STK_LOAD = (CPU_HZ / 1000) - 1;
    STK_VAL = 0;
    STK_CONTROL = (STK_CTRL_CLKSOURCE | STK_CTRL_TICKINT | STK_CTRL_ENABLE);

    scb_cfg_system_irqs();

    initialized |= SYSTICK_INITIALIZED;
    LOG_TIME_INFO("SysTick initialized for 1ms ticks");
}

void SysTick_Handler(void)
{
    system_ticks++;

    /*
     * With this setup SysTick event is 256 milliseconds (0.256 seconds).
     * Alternatively, you can use & 127 for 128ms or & 255 for 256ms, etc. for the desired frequency of the event.
     */
    if ((system_ticks & (256 - 1)) == 0) {
        LOG_TIME_DEBUG("SysTick: %lu", (unsigned long)(system_ticks));
        event_notify |= EVT_SYS_TICK;
    }
}

#endif // !ENABLE_RTOS

/*
 * Initialize the SysTick timer for polling.
 * Note that this function is only used for calibration of the hardware timer
 * and is not intended for general use.
 */
void init_systick_poll(void)
{
    // Calculate reload value for 1ms (ticks per milli)
    STK_LOAD = (CPU_HZ / 1000) - 1;
    STK_VAL = 0;
    STK_CONTROL = (STK_CTRL_CLKSOURCE | STK_CTRL_ENABLE);
    volatile uint32_t linger = STK_CONTROL;
    (void)linger; // clear lingering flags
}

bool systick_poll(void)
{
    return (STK_CONTROL & STK_CTRL_COUNTFLAG) != 0;
}

void systick_reset(void)
{
    STK_CONTROL = 0;
}
