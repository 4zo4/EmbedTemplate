/**
 * @file time.c
 * @brief Common architectural timer-specific implementations for Cortex-A.
 * This file implements timestamping and background system ticking for the Cortex-A family
 * using the internal ARMv7-A Generic Timer.
 */
#include <stdint.h>
#include <stdio.h>

#include "assert.h"
#include "common.h"
#include "event.h"
#include "gic.h"

#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#define LOG_TIME_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_TIMER), __VA_ARGS__)

#define SYS_TICK_IRQ_ID 27U
#define CPU_HZ cpu_hz
#define TIMER_1MS_COUNT (timer_1ms_ticks)

// -- Globals --

// user port have to set platform-specific CPU System Clock Speed
alignas(8) volatile uintptr_t cpu_hz;
alignas(8) static uint64_t boot_ts = 0;
alignas(8) static uint32_t timer_1ms_ticks = 0;

// -- End of globals --

void init_timestamp(void)
{
    assert((CPU_HZ != 0) && "[ERROR] Undefined CPU System Clock Speed.");

    uint32_t low, high;
    __asm volatile("mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    uint64_t ticks = ((uint64_t)high << 32) | low;

    boot_ts = ticks / (CPU_HZ / 1000000ULL);
    timer_1ms_ticks = CPU_HZ / 1000U;

    log_set_level(DOMAIN_SYS, ENTITY_TIMER, LOG_LEVEL_INFO);
    LOG_TIME_INFO("Timestamp initialized");
}

uint64_t get_timestamp48(void)
{
    uint32_t low, high;

    __asm volatile("mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    uint64_t ticks = ((uint64_t)high << 32) | low;
    uint64_t now = ticks / (CPU_HZ / 1000000ULL);

    return (now - boot_ts) & 0xFFFFFFFFFFFFULL;
}

#ifndef ENABLE_RTOS

#define SYS_TICK_HANDLER SysTick_Handler
static volatile uint32_t system_ticks = 0;

__attribute__((noinline, used)) void SysTick_Handler(void)
{
    uint32_t ctrl;

    __asm volatile("mrc p15, 0, %0, c14, c3, 1" : "=r"(ctrl));
    ctrl |= BIT(1);
    __asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(ctrl));
    __asm volatile("mcr p15, 0, %0, c14, c3, 0" ::"r"(TIMER_1MS_COUNT));
    ctrl &= ~BIT(1);
    __asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(ctrl));

    data_sync_barrier();
    ins_sync_barrier();

    system_ticks++;

    /*
     * With this setup SysTick event is 1024 milliseconds (1.024 seconds).
     * Alternatively, you can use & 127 for 128ms or & 255 for 256ms, etc. for the desired frequency of the event.
     */
    if ((system_ticks & (1024 - 1)) == 0) {
        LOG_TIME_DEBUG("SysTick: %lu", (unsigned long)(system_ticks));
        event_notify |= EVT_SYS_TICK;
    }
}

#else

#define SYS_TICK_HANDLER FreeRTOS_Tick_Handler

void clear_systick(void)
{
    uint32_t ctrl;

    __asm volatile("mrc p15, 0, %0, c14, c3, 1" : "=r"(ctrl));
    ctrl |= BIT(1);
    __asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(ctrl));
    __asm volatile("mcr p15, 0, %0, c14, c3, 0" ::"r"(TIMER_1MS_COUNT));
    ctrl &= ~BIT(1);
    __asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(ctrl));

    data_sync_barrier();
    ins_sync_barrier();
}
#endif

void init_systick(void)
{
    uint32_t ctrl = 1U;

    gic_register_interrupt(SYS_TICK_IRQ_ID, SYS_TICK_HANDLER);

    __asm volatile("mcr p15, 0, %0, c14, c3, 0" ::"r"(TIMER_1MS_COUNT));
    __asm volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(ctrl));

    gic_enable_interrupt(SYS_TICK_IRQ_ID, 0x20);

    LOG_TIME_INFO("SysTick initialized for 1ms ticks");
}
