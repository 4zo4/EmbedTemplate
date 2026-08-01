#pragma once

// clang-format off
#if defined(ARCH_ARM) || defined(ARCH_RISCV)
    #define NOP() __asm__ volatile("nop")
    #if defined(__ARM_ARCH_7A__)
    #define HALT_CPU() do { \
        __asm__ volatile("dsb sy" : : : "memory"); \
        __asm__ volatile("wfi"    : : : "memory"); \
        __asm__ volatile("isb sy" : : : "memory"); \
    } while(0)
    #else
    #define HALT_CPU() __asm__ volatile("wfi")
    #endif
#endif
#if defined(ARCH_ARM)
#if defined(__ARM_ARCH_7A__)
    static inline uint32_t disable_interrupts(void) {
        uint32_t cpsr;
        __asm volatile("mrs %0, cpsr" : "=r"(cpsr));
        __asm volatile("cpsid i" : : : "memory");
        return cpsr;
    }
    static inline void restore_interrupts(uint32_t cpsr) {
        __asm volatile("dsb sy" : : : "memory");
        __asm volatile("isb sy" : : : "memory");
        if (!(cpsr & (1U << 7))) {
            __asm volatile("cpsie i" : : : "memory");
        }
    }
    static inline void enable_interrupts(void) {
        __asm volatile("cpsie i" : : : "memory");
    }
    static inline void io_barrier(void) { __asm__ volatile("dsb sy" : : : "memory"); }
    #define memory_barrier() __asm volatile("dmb sy" : : : "memory") /* Data Memory Barrier */
    #define data_sync_barrier() __asm volatile("dsb sy" : : : "memory") /* Data Synchronization Barrier */
    #define ins_sync_barrier() __asm volatile("isb sy" : : : "memory") /* Instruction Synchronization Barrier */
#elif defined(__aarch64__)
    static inline uint32_t disable_interrupts(void) {
        uint64_t daif;
        __asm volatile("mrs %0, daif" : "=r"(daif));
        __asm volatile("msr daifset, #2" : : : "memory");
        return (uint32_t)daif;
    }
    static inline void restore_interrupts(uint32_t daif) {
        uint64_t val = daif;
        __asm volatile("msr daif, %0" : : "r"(val) : "memory");
    }
    static inline void enable_interrupts(void) {
        __asm volatile("msr daifclr, #2" : : : "memory");
    }
    static inline void io_barrier(void) { __asm__ volatile("dsb sy" : : : "memory"); }
    #define memory_barrier() __asm volatile("dmb sy" : : : "memory")
    #define data_sync_barrier() __asm volatile("dsb sy" : : : "memory")
    #define ins_sync_barrier() __asm volatile("isb" : : : "memory")
#elif defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
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
    static inline void io_barrier(void) { __asm__ volatile("" : : : "memory"); }
    #define memory_barrier() __asm volatile("" : : : "memory")
    #define data_sync_barrier() __asm volatile("" : : : "memory")
    #define ins_sync_barrier() __asm volatile("" : : : "memory")
#else
    #error "Unsupported ARM Architecture"
#endif
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
    static inline void io_barrier(void) { __asm__ volatile("fence io, io" : : : "memory"); }
    #define memory_barrier() __asm volatile("fence iorw, iorw" : : : "memory")
    #define data_sync_barrier() __asm volatile("fence iorw, iorw" : : : "memory")
    #define ins_sync_barrier() __asm volatile("fence.i" : : : "memory")
#elif defined(ARCH_X86)
    #ifdef BARE_METAL
    #define NOP() __asm__ volatile("nop")
    #define HALT_CPU() __asm__ volatile("hlt")
    #ifdef __x86_64__
    #define INCODE
    #else // 32-bit x86
    #define INCODE ".code32\n\t"
    #endif
    static inline uint32_t disable_interrupts(void) {
        #ifdef __x86_64__
            uint64_t primask;
            __asm__ volatile ("pushfq ; pop %0 ; cli" : "=r" (primask) : : "memory");
        #else // 32-bit x86
            uint32_t primask;
            __asm__ volatile (INCODE"pushfl; popl %0 ; cli" : "=r" (primask) : : "memory");
        #endif
        return (uint32_t)primask;
    }
    static inline void restore_interrupts(uint32_t primask) {
        if (primask & (1 << 9)) {
            __asm__ volatile (INCODE"sti" : : : "memory");
        }
    }
    static inline void enable_interrupts(void) {
        __asm__ volatile (INCODE"sti" : : : "memory");
    }
    #define memory_barrier() __asm volatile(INCODE"mfence" : : : "memory")
    #define data_sync_barrier() __asm volatile(INCODE"sfence" : : : "memory")
    #define ins_sync_barrier() __asm__ volatile(INCODE"lfence" : : : "memory")
    #else // !BARE_METAL
    #define NOP() do {} while(0)
    #define HALT_CPU() for(;;)
    static inline uint32_t disable_interrupts(void) { return 0; }
    static inline void restore_interrupts(uint32_t primask) { (void)primask; }
    static inline void enable_interrupts(void) {}
    #define memory_barrier() NOP()
    #define data_sync_barrier() NOP()
    #define ins_sync_barrier() NOP()
    #endif
    static inline void io_barrier(void) { __asm__ volatile("" : : : "memory"); }
#else // unsupported architecture
    #define NOP() do {} while(0)
    #define HALT_CPU() for(;;)
    static inline uint32_t disable_interrupts(void) { return 0; }
    static inline void restore_interrupts(uint32_t primask) { (void)primask; }
    static inline void enable_interrupts(void) {}
    #define io_barrier() NOP()
    #define memory_barrier() NOP()
    #define data_sync_barrier() NOP()
    #define ins_sync_barrier() NOP()
#endif
// clang-format on
