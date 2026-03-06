#pragma once

#if defined(__arm__) || defined(__aarch64__)
#define NOP() __asm__ volatile("nop")
#define HALT_CPU() __asm__ volatile("wfi")
#elif defined(__x86_64__) || defined(_M_X64)
#define NOP() __asm__ volatile("nop")
#define HALT_CPU() __asm__ volatile("hlt")
#elif defined(__riscv)
#define NOP() __asm__ volatile("nop")
#define HALT_CPU() __asm__ volatile("wfi")
#else
#define NOP() \
    do { \
    } while (0)
#define HALT_CPU() while (1)
#endif

bool stdin_ready(int timeout_ms);
