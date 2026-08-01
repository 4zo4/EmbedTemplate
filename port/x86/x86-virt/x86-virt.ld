/* port/x86/x86-virt/x86-virt.ld */
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* 1MB is the standard physical memory entry point for Multiboot loaders */
    . = 0x100000;

    .text : ALIGN(4) {
        /* Construct the mandatory 12-byte Multiboot binary header directly in place */
        LONG(0x1BADB002);                     /* Magic number */
        LONG(0x00000003);                     /* Flags (Align modules + memory info) */
        LONG(-(0x1BADB002 + 0x00000003));     /* Checksum validation */

        *(.text*)
        *(.rodata*)
    }

    .data : ALIGN(4) {
        *(.data*)
    }

    .got.plt : ALIGN(4) {
        *(.got.plt*)
    }

    _sbss = .;
    __bss_start__ = _sbss;

    .cli_mem (NOLOAD) : ALIGN(8)
    {
        *(.cli_mem)
    }

    .log_buffer (NOLOAD) : ALIGN(8)
    {
        *(.log_buffer)
    }

    .bss : ALIGN(4)
    {
        *(.bss)
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;

    /* Heap allocations grow past this boundary */
    .heap (NOLOAD) : ALIGN(8)
    {
        _end = .;
        PROVIDE(end = .);
        *(.heap*)
    }

    ._boot_stack (NOLOAD) : ALIGN(16)
    {
        . += 0x4000;     /* Reserve 16KB execution stack space */
        _estack = .;     /* Stack grows DOWNWARDS from this highest boundary address */
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
