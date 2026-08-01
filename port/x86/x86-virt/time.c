#include <stdint.h>
#include <stdio.h>

#include "apic.h"
#include "common.h"
#include "event.h"
#include "init.h"

#ifdef ENABLE_RTOS
#include "FreeRTOS.h"
#endif

#define LOG_TIME_CRITICAL(...) LOG_ENTITY_CRITICAL(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_ERROR(...) LOG_ENTITY_ERROR(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_WARNING(...) LOG_ENTITY_WARNING(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_INFO(...) LOG_ENTITY_INFO(ID_SYS(ENT_TIMER), __VA_ARGS__)
#define LOG_TIME_DEBUG(...) LOG_ENTITY_DEBUG(ID_SYS(ENT_TIMER), __VA_ARGS__)

// x86 8254 Programmable Interval Timer (PIT)
#define PIT_PORT_CHANNEL0 0x40
#define PIT_PORT_COMMAND 0x43
#define PIT_COMMAND_VAL 0x36         // Channel 0, access lobyte/hibyte, Mode 3 (Square Wave)
#define PIT_BASE_FREQUENCY 1193182UL // The fixed physical oscillator crystal frequency

// Local APIC Timer Registers
#define LAPIC_LVT_TIMER_REG (*(volatile uint32_t *)(0xFEE00000UL + 0x0320))
#define LAPIC_TIMER_INIT_REG (*(volatile uint32_t *)(0xFEE00000UL + 0x0380))
#define LAPIC_TIMER_DIV_REG (*(volatile uint32_t *)(0xFEE00000UL + 0x03E0))

alignas(8) static uint64_t boot_ts = 0;

// read rdtsc (Read Time-Stamp Counter)
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void init_pit_timer(uint32_t tick_rate_hz)
{
    uint32_t divisor = PIT_BASE_FREQUENCY / tick_rate_hz;

    // Clamp the value to 16-bit physical hardware limits
    if (divisor > 0xFFFF)
        divisor = 0xFFFF;
    if (divisor < 1)
        divisor = 1;

    // 2. Program the PIT Control Command register
    outb(PIT_PORT_COMMAND, PIT_COMMAND_VAL);

    // 3. Write out the 16-bit split value (Lsb then Msb)
    outb(PIT_PORT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_PORT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void init_local_apic_timer(void)
{
    // 1. Configure the Local APIC Timer internal divider scale to 16 (value 0x03)
    LAPIC_TIMER_DIV_REG = 0x03;

    // 2. Program the LVT register: Periodic Mode (Bit 17 = 1) + assign native Vector 0x2F
    // Bit 16 is initially masked (1)
    LAPIC_LVT_TIMER_REG = BIT(17) | BIT(16) | (X86_IRQ_VECTOR_BASE + LAPIC_TIMER_IRQ_ID);

    // 3. Set the Initial Count Register reload value.
    // QEMU LAPIC bus clock (1GHz) / divide ratio (16) / 1000Hz target = 62500 ticks per millisecond
    LAPIC_TIMER_INIT_REG = 62500UL;
}

void init_timestamp(void)
{
    if (initialized & TIMESTAMP_INITIALIZED)
        return;
    boot_ts = rdtsc();
#ifdef ENABLE_PIT
#define SYS_TICK_IRQ_ID PIT_IRQ_ID
    init_pit_timer(1000);
#else // LAPIC
#define SYS_TICK_IRQ_ID LAPIC_TIMER_IRQ_ID
    init_local_apic_timer();
#endif
    initialized |= TIMESTAMP_INITIALIZED;
    log_set_level(DOMAIN_SYS, ENTITY_TIMER, LOG_LEVEL_INFO);
    LOG_TIME_INFO("Timestamp initialized");
}

uint64_t get_timestamp48(void)
{
    uint64_t now = rdtsc();

    if (boot_ts == 0)
        boot_ts = now;

    return (now - boot_ts) & 0xFFFFFFFFFFFFULL;
}

#ifndef ENABLE_RTOS

static volatile uint32_t system_ticks = 0;

__attribute__((noinline, used)) void SysTick_Handler(void)
{
    system_ticks++;

    /*
     * Setup SysTick event to 1024/512/.. milliseconds (1.024/0.512/.. seconds).
     * Alternatively, you can use & 127 for 128ms or & 255 for 256ms, etc. for the desired frequency of the event.
     */
    if ((system_ticks & (256 - 1)) == 0) {
        LOG_TIME_DEBUG("SysTick: %lu", (unsigned long)(system_ticks));
        event_notify |= EVT_SYS_TICK;
    }
}

void init_systick(void)
{
    if (initialized & SYSTICK_INITIALIZED)
        return;

    apic_register_interrupt(SYS_TICK_IRQ_ID, SysTick_Handler);
    apic_enable_interrupt(SYS_TICK_IRQ_ID);

    initialized |= SYSTICK_INITIALIZED;

    LOG_TIME_INFO("SysTick initialized for 1ms ticks");
}

#endif // !ENABLE_RTOS
