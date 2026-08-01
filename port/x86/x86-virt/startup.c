#include <stdint.h>

int             main(void);
extern uint32_t _sdata, _edata, _sbss, _ebss, _sidata, _estack;

void start_init(void)
{
    volatile uint32_t *bss_ptr = &_sbss;
    volatile uint32_t *bss_end = &_ebss;

    // Zero out .bss
    while (bss_ptr < bss_end) {
        *bss_ptr++ = 0;
    }

    // Jump to main
    main();
    // Should never reach here
    for (;;)
        __asm__ volatile("hlt");
}

__asm__(
    ".section .text\n\t"
    ".global _start\n\t"
    ".type _start, @function\n\t"
    "_start:\n\t"
    ".code32\n\t"
    "cli\n\t"
    "movl $_estack, %esp\n\t"
    "jmp start_init\n\t"
);
