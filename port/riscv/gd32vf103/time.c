/**
 * @file time.c
 * @brief Timestamp and system tick implementation for GD32VF103.
 * This file implements the timestamp and system tick functionality for the GD32VF103 microcontroller
 * using the RISC-V machine timer (mtime) for high-resolution timing. The system tick is implemented
 * using the machine timer compare registers (mtimecmp), configured to generate an interrupt every 1 millisecond.
 * The timestamp and SysTick initialization functions should be called during system startup to set up the timing infrastructure.
 * @note The System Tick implementation is only included for non-RTOS environments, as an RTOS would typically provide its own tick mechanism.
 */
#include <stdint.h>

#include "log.h"
#include "log_marker.h"
#include "utils.h"

#ifdef TARGET_HW_GD32VF103
#define IRQ_HANDLER __attribute__((interrupt))
#else
#define IRQ_HANDLER
#endif

#define LOG_SYS_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_SYS_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_TIMER), __VA_ARGS__)

// GD32VF103 Machine Timer Base and Register Definitions
#define MTIMER_BASE 0xD1000000
#ifdef TARGET_HW_GD32VF103
#define MTIME_LO (*(volatile uint32_t *)(MTIMER_BASE + 0x00))
#define MTIME_HI (*(volatile uint32_t *)(MTIMER_BASE + 0x04))
#define MTIMECMP_LO (*(volatile uint32_t *)(MTIMER_BASE + 0x08))
#define MTIMECMP_HI (*(volatile uint32_t *)(MTIMER_BASE + 0x0C))
#define CORE_FREQ 108000000ULL
#define TICK_DIVISOR 4000                               // Timer clock is 1/4 of Core clock on GD32VF
#define TICK_INTERVAL (CORE_FREQ / TICK_DIVISOR / 1000) // 1ms tick interval
#else // Renode
#define MTIME_LO (*(volatile uint32_t *)(MTIMER_BASE + 0xBFF8))
#define MTIME_HI (*(volatile uint32_t *)(MTIMER_BASE + 0xBFFC))
#define MTIMECMP_LO (*(volatile uint32_t *)(MTIMER_BASE + 0x4000))
#define MTIMECMP_HI (*(volatile uint32_t *)(MTIMER_BASE + 0x4004))
#define TICK_INTERVAL 1000 
#endif

static uint64_t boot_ts = 0;

void init_timestamp(void)
{
    // Machine timer starts at reset; no specific init required for mtime
    log_set_level(DOMAIN_SYS, ENTITY_TIMER, LOG_LEVEL_INFO);
    LOG_SYS_INFO("Timestamp initialized");
}

uint64_t get_timestamp48(void)
{
    uint32_t hi, lo;
    do {
        hi = MTIME_HI;
        lo = MTIME_LO;
    } while (hi != MTIME_HI);

    uint64_t now = (((uint64_t)hi) << 32) | lo;

    if (boot_ts == 0) {
        boot_ts = now;
    }

    return (now - boot_ts) & 0xFFFFFFFFFFFFULL;
}

#ifndef ENABLE_RTOS
void init_systick(void)
{
    uint64_t next = get_timestamp48() + TICK_INTERVAL;

    // Prevent premature trigger by setting High to max
    MTIMECMP_HI = 0xFFFFFFFF;
    // Set Low word
    MTIMECMP_LO = (uint32_t)next;
    // Set real High word
    MTIMECMP_HI = (uint32_t)(next >> 32);

    __asm volatile("csrs mie, %0" : : "r"(0x80));
    __asm volatile("csrs mstatus, %0" : : "r"(0x8));
}

extern volatile uint8_t  event_notify;
static volatile uint32_t system_ticks = 0;

void IRQ_HANDLER timer_handler(void)
{
    uint64_t next_tick = (((uint64_t)MTIMECMP_HI) << 32 | MTIMECMP_LO) + TICK_INTERVAL;
    MTIMECMP_LO = (uint32_t)next_tick;
    MTIMECMP_HI = (uint32_t)(next_tick >> 32);

    system_ticks++;

    /*
     * With this setup SysTick event is 1024 milliseconds (1.024 seconds).
     * Alternatively, you can use & 127 for 128ms or & 255 for 256ms, etc. for the desired frequency of the event.
     */
    if ((system_ticks & (1024 - 1)) == 0) {
        LOG_SYS_DEBUG("SysTick: %lu", (unsigned long)(system_ticks));
        event_notify |= BIT(0); // Set SysTick event flag
    }
}

#endif // !ENABLE_RTOS
