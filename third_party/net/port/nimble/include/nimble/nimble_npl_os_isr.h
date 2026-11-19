#ifndef NIMBLE_NPL_OS_ISR_H_
#define NIMBLE_NPL_OS_ISR_H_

#if defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__) || \
    defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
    static inline bool os_arch_in_isr(void)
    {
        return xPortIsInsideInterrupt() != pdFALSE;
    }
#elif defined(__i386__) || defined(__x86_64__)
    extern volatile uint32_t ulInterruptNesting;
    static inline bool os_arch_in_isr(void)
    {
        return ulInterruptNesting > 0U;
    }
#else // unsupported architecture
#error "Unsupported architecture for interrupt status"
#endif

#endif /* NIMBLE_NPL_OS_ISR_H_ */