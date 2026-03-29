#pragma once

#ifdef BARE_METAL
#if defined(__arm__) || defined(__aarch64__) || defined(__riscv)
#define NOP() __asm__ volatile("nop")
#define HALT_CPU() __asm__ volatile("wfi")
#elif defined(__x86_64__) || defined(_M_X64)
#define NOP() __asm__ volatile("nop")
#define HALT_CPU() __asm__ volatile("hlt")
#else
#define NOP() \
    do { \
    } while (0)
#define HALT_CPU() for(;;)
#endif
#else // !BARE_METAL
#define NOP() \
    do { \
    } while (0)
#define HALT_CPU() for(;;)
#endif

bool stdin_ready(int timeout_ms);
