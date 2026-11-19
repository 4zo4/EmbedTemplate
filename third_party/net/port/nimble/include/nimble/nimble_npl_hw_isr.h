#ifndef NIMBLE_NPL_HW_ISR_H_
#define NIMBLE_NPL_HW_ISR_H_

#include <stdint.h>

#include "nimble/nimble_npl_os_isr.h"

typedef struct {
    volatile uint32_t CPUID;
    volatile uint32_t ICSR;
} nimble_scb_t;

#define SCB_ICSR_VECTACTIVE_Msk (0x1FFUL)

static const nimble_scb_t quasi_scb_in_isr  = { .ICSR = 1U };
static const nimble_scb_t quasi_scb_in_task = { .ICSR = 0U };
#define SCB (os_arch_in_isr() ? &quasi_scb_in_isr : &quasi_scb_in_task)

#endif /* NIMBLE_NPL_HW_ISR_H_ */
