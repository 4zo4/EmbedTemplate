#include "FreeRTOSConfig.h"

.section .isr_vector, "ax"
.global _start
.global _vectors

_start:
    dsb
    isb
    /* Force CPU into Supervisor Mode (SVC) and disable async interrupts during boot */
    cpsid   aif, #19

    /* Disable Strict Hardware Alignment Faults */
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 1)
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* Read Coprocessor Access Control Register (CPACR) into r0 */
    mrc     p15, 0, r0, c1, c0, 2

    /* Enable full access permissions for both CP10 and CP11 (bits 23:20 = 1111b) */
    orr     r0, r0, #(0xF << 20)

    /* Write modified flags back to CPACR */
    mcr     p15, 0, r0, c1, c0, 2
    isb                                 @ Flush pipeline to settle coprocessor access permissions

    /* Switch on the VFP/NEON hardware extension globally via the FPEXC Register */
    /* (Enabling bit 30 - EN bit - activates the floating point pipeline execution unit) */
    mov     r0, #(1 << 30)
    vmsr    fpexc, r0                   @ Set EN bit inside Floating-Point Exception Register
    isb

    /* -------------------------------------------------------------------------
     * Configure the Vector Base Address Register (VBAR)
     * ------------------------------------------------------------------------- */
    ldr     r0, =_vectors
    mcr     p15, 0, r0, c12, c0, 0
    isb

    /* -------------------------------------------------------------------------
     * Core Architecture Stack Allocations
     * ------------------------------------------------------------------------- */
    ldr     r0, =_estack

    /* Configure IRQ Mode Stack Pointer (Gets the top 8KB chunk) */
    cps     #18
    bic     r0, r0, #0x7        @ Enforce strict 8-byte alignment
    mov     sp, r0
    sub     r0, r0, #0x2000     @ Allocate a dedicated 8KB safety gap for IRQ processing

    /* Configure Undefined Instruction Mode Stack Pointer (Gets a 1KB chunk) */
    cps     #27                 @ Switch to UND Mode (#27)
    bic     r0, r0, #0x7        @ Enforce strict 8-byte alignment
    mov     sp, r0              @ Assign sp_und
    sub     r0, r0, #0x0400     @ Subtract 1KB safety gap for UND exception processing

    /* Configure Abort Mode Stack Pointer (Gets a 1KB chunk) */
    cps     #23                 @ Switch to ABT Mode (#23)
    bic     r0, r0, #0x7        @ Enforce strict 8-byte alignment
    mov     sp, r0              @ Assign sp_abt
    sub     r0, r0, #0x0400     @ Subtract 1KB safety gap for ABT exception processing

    /* Configure Supervisor Mode (SVC) Stack Pointer (Gets the remaining ~22KB area) */
    cps     #19                 @ Return permanently to Supervisor Mode for runtime execution
    bic     r0, r0, #0x7        @ Enforce strict 8-byte alignment
    mov     sp, r0              @ Hardware sp now matches the base of your exception stack windows

    /* Enforce full pipeline synchronization before initializing software structures */
    dsb     sy
    isb     sy

    /* Clear BSS Section */
    ldr     r0, =_sbss
    ldr     r1, =_ebss
    mov     r2, #0
bss_loop:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     bss_loop

#ifdef TARGET_CORTEX_A9_VIRT
    /* Call a local initialization hook if needed */
#endif

    ldr     r0, =main                   @ jump to main
    blx     r0

dead_loop:
    b       dead_loop

/* ARMv7-A Exception Vector Table */

.align 5 /* Enforces the mandatory 32-byte boundary constraint for VBAR */
_vectors:
    ldr     pc, =reset_handler          @ 0x00: Reset
    ldr     pc, =undefined_handler      @ 0x04: Undefined Instruction
#ifdef ENABLE_RTOS
    ldr     pc, =FreeRTOS_SWI_Handler   @ 0x08: Point to the FreeRTOS ARM_CA9 SWI symbol
#else
    ldr     pc, =swi_handler            @ 0x08: Bare-Metal SWI Call Handler
#endif
    ldr     pc, =prefetch_abort_handler @ 0x0C: Prefetch Abort
    ldr     pc, =data_abort_handler     @ 0x10: Data Abort
    b       .                           @ 0x14: Reserved
#ifdef ENABLE_RTOS
    ldr     pc, =FreeRTOS_IRQ_Handler   @ 0x18: FreeRTOS Shared GIC IRQ Interceptor
#else
    ldr     pc, =baremetal_irq_handler  @ 0x18: Bare-Metal GIC Interceptor
#endif
    ldr     pc, =fiq_handler            @ 0x1C: Fast Interrupt (FIQ)

/* Exception Stubs */
.global reset_handler
.global fiq_handler
.global swi_handler
.global undefined_handler
.global prefetch_abort_handler
.global data_abort_handler

reset_handler:          b _start
fiq_handler:            b .
swi_handler:            b .

undefined_handler:
    /* Hardware is in UND mode. For undefined traps, LR points directly */
    /* to the instruction following the fault. No offset adjustment needed. */

    /* Forcibly align the private Exception Stack Pointer to an 8-byte boundary */
    mov     r12, sp                 @ Capture unaligned sp_und baseline
    and     r3, r12, #7             @ Calculate alignment variance (0 or 4)
    sub     sp, sp, r3              @ Enforce strict 8-byte alignment constraints
    push    {r3, r12}               @ Anchor parameters onto the aligned face
    bl      fault_handler           @ Invoke common C fault handler
    b       .

prefetch_abort_handler:
    /* Hardware is in ABT mode. Correct exception link register to point */
    /* directly to the exact instruction that failed to prefetch. */
    sub     lr, lr, #4

    /* Enforce strict 8-byte alignment constraints on exception stack */
    mov     r12, sp
    and     r3, r12, #7
    sub     sp, sp, r3
    push    {r3, r12}
    bl      fault_handler           @ Invoke common C fault handler
    b       .

data_abort_handler:
    /* Hardware is in ABT mode. Correct exception link register to point */
    /* directly to the exact memory operation instruction that crashed. */
    sub     lr, lr, #8

    /* Enforce strict 8-byte alignment constraints on exception stack */
    mov     r12, sp
    and     r3, r12, #7
    sub     sp, sp, r3
    push    {r3, r12}
    bl      fault_handler           @ Invoke common C fault handler
    b       .

#ifndef ENABLE_RTOS
.section .text
.align 5
.global baremetal_irq_handler
.type baremetal_irq_handler, %function

.global baremetal_irq_handler
.type baremetal_irq_handler, %function
.global baremetal_irq_handler
.global baremetal_irq_handler
baremetal_irq_handler:
    /* Correct the hardware exception return link */
    sub     lr, lr, #4

    /* Commit the active exception context layout straight to the SVC stack */
    srsdb   sp!, #19
    cps     #19                     @ Shift execution environment to Supervisor Mode

    /* Secure caller-saved scratch integer registers used by the C compiler */
    push    {r0, r1, r2, r3, r4, r5, r12, lr}

    /* Enforce strict 8-byte stack alignment constraints for the AAPCS call */
    mov     r4, sp                  @ Capture pristine unaligned stack baseline into r4
    and     r5, r4, #7              @ Calculate alignment variance (0 or 4) into r5
    sub     sp, sp, r5              @ Enforce strict 8-byte alignment constraints
    push    {r4, r5}                @ Anchor the math parameters onto the aligned face

    /* Load the dynamic GICC base variable address from RAM */
    ldr     r2, =gicc_base_addr
    ldr     r2, [r2]                @ r2 = Active board's physical GICC base mapping

    /* Read GICC_IAR cleanly using your base tracking offset (Offset 0x000C) */
    ldr     r0, [r2, #0x000C]       @ Read raw GICC_IAR value into r0

    /* Spurious Interrupt Fast Pass Check */
    movw    r1, #1023
    ubfx    r3, r0, #0, #10         @ Isolate the clean ID into r3 for the check cleanly
    cmp     r3, r1
    bhs     clear_spurious_irq

    /* Stash the raw tracking word into your shared RAM safety variable */
    ldr     r1, =gic_iar
    str     r0, [r1]                @ gic_iar = r0

    /* Invoke IRQ handler passing raw IAR as 1st argument (r0) */
    ldr     r1, =vApplicationIRQHandler
    blx     r1                      @ vApplicationIRQHandler(gic_iar)

irq_exit:
    /*  Snap the stack pointer back to its natural unaligned baseline */
    pop     {r4, r5}
    mov     sp, r4

    /* Restore working registers */
    pop     {r0, r1, r2, r3, r4, r5, r12, lr}

    /*  Complete exception transition back to main execution thread loop */
    rfeia   sp!

clear_spurious_irq:
    ubfx    r0, r0, #0, #10         @ Isolate clean 10-bit ID from raw spurious word into r0

    /* Clear the spurious entry ceiling directly at GICC_EOIR (r2 already holds GICC base) */
    str     r0, [r2, #0x0010]       @ GICC_EOIR = clean spurious ID (typically 1023)

    /* Force full hardware bus synchronization barriers */
    dsb     sy
    isb     sy
    b       irq_exit
#endif









