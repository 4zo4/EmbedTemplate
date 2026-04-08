#pragma once

// clang-format off
#if defined(ARCH_ARM) || defined(ARCH_RISCV)
#define NOP() __asm__ volatile("nop")
#define HALT_CPU() __asm__ volatile("wfi")
#endif
#if defined(ARCH_ARM)
    static inline uint32_t disable_interrupts(void) {
        uint32_t primask;
        __asm volatile ("mrs %0, primask" : "=r" (primask));
        __asm volatile ("cpsid i" : : : "memory");
        return primask;
    }
    static inline void restore_interrupts(uint32_t primask) {
        __asm volatile ("msr primask, %0" : : "r" (primask) : "memory");
    }
    static inline void enable_interrupts(void) {
        __asm volatile ("cpsie i" : : : "memory");
    }
#elif defined(ARCH_RISCV)
    // bit 3 (0x8) is the MIE (Machine Interrupt Enable) bit in mstatus
    static inline uint32_t disable_interrupts(void) {
        uint32_t primask;
        __asm volatile ("csrrci %0, mstatus, 8" : "=r" (primask) : : "memory");
        return primask;
    }
    static inline void restore_interrupts(uint32_t primask) {
        if (primask & 0x8) {
            __asm volatile ("csrsi mstatus, 8" : : : "memory");
        }
    }
    static inline void enable_interrupts(void) {
        __asm volatile ("csrsi mstatus, 8" : : : "memory");
    }
#elif defined(ARCH_X86)
    #ifdef BARE_METAL
    #define NOP() __asm__ volatile("nop")
    #define HALT_CPU() __asm__ volatile("hlt")
    static inline uint32_t disable_interrupts(void) {
        uint64_t primask;
        #ifdef __x86_64__
            __asm__ volatile ("pushfq ; pop %0 ; cli" : "=r" (primask) : : "memory");
        #else // 32-bit x86
            __asm__ volatile ("pushfd ; pop %0 ; cli" : "=r" (primask) : : "memory");
        #endif
        return (uint32_t)primask;
    }
    static inline void restore_interrupts(uint32_t primask) {
        if (primask & (1 << 9)) {
            __asm__ volatile ("sti" : : : "memory");
        }
    }
    static inline void enable_interrupts(void) {
        __asm__ volatile ("sti" : : : "memory");
    }
    #else // !BARE_METAL
    #define NOP() do {} while(0)
    #define HALT_CPU() for(;;)
    static inline uint32_t disable_interrupts(void) { return 0; }
    static inline void restore_interrupts(uint32_t primask) { (void)primask; }
    static inline void enable_interrupts(void) {}
    #endif
#else // unsupported architecture
    #define NOP() do {} while(0)
    #define HALT_CPU() for(;;)
    static inline uint32_t disable_interrupts(void) { return 0; }
    static inline void restore_interrupts(uint32_t primask) { (void)primask; }
    static inline void enable_interrupts(void) {}
#endif
// clang-format on
