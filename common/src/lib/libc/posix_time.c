#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <time.h>

static uint64_t boot_ts = 0;

uint64_t get_timestamp48(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = ((uint64_t)ts.tv_sec * 1000000ULL + (ts.tv_nsec / 1000ULL));

    if (boot_ts == 0)
        boot_ts = now;

    return (now - boot_ts) & 0xFFFFFFFFFFFFULL;
}
