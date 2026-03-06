#include <stdint.h>

#define DWT_CONTROL (*((volatile uint32_t *)0xE0001000))
#define DWT_CYCCNT (*((volatile uint32_t *)0xE0001004))
#define DEMCR (*((volatile uint32_t *)0xE000EDFC))
#define DEMCR_TRCENA (1UL << 24)
#define DWT_CYCCNTENA (1UL << 0)

// System Clock Speed (e.g., 168MHz for STM32F4)
#define CPU_HZ168000000ULL

static uint64_t boot_ts = 0;

void init_timestamp(void)
{
    DEMCR |= DEMCR_TRCENA;        // Enable DWT
    DWT_CYCCNT = 0;               // Reset cycle counter
    DWT_CONTROL |= DWT_CYCCNTENA; // Start cycle counter
}

uint64_t get_timestamp48(void)
{
    uint32_t cycles = DWT_CYCCNT;
    uint64_t now = (uint64_t)cycles * 1000000ULL / CPU_HZ;

    if (boot_ts == 0) {
        boot_ts = now;
    }

    return (now - boot_ts) & 0xFFFFFFFFFFFFULL;
}
