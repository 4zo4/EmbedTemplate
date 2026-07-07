/* cortex-a9-virt.ld */
ENTRY(_start)

MEMORY
{
    /* QEMU Virt Loader automatically places the '-kernel' binary at 0x40000000.
       We map everything to RAM so code, data, and vectors match runtime addresses. */
    RAM (rwx) : ORIGIN = 0x40100000, LENGTH = 4M
}

SECTIONS
{
    /* Enforce that the exception vectors and _start block sit at exactly 0x40000000 */
    .isr_vector :
    {
        . = ALIGN(32); /* ARMv7-A VBAR requires a minimum 32-byte alignment */
        _svector = .;
        KEEP(*(.isr_vector))
        . = ALIGN(8);
    } > RAM

    .text :
    {
        . = ALIGN(8);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(8);
        _etext = .;
    } > RAM

    .cli_mem :
    {
        . = ALIGN(8);
        *(.cli_mem)
        . = ALIGN(8);
    } > RAM

    /* Since everything is loaded directly into RAM, data doesn't
       need to be copied from FLASH. We map LOADADDR to match its virtual address. */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(8);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(8);
        _edata = .;
    } > RAM

    .rodata :
    {
        . = ALIGN(8);   /* Force all string literals, formatting constants, and tables onto 64-bit boundaries */
        *(.rodata)
        *(.rodata*)
        . = ALIGN(8);
    } > RAM

    .log_buffer :
    {
        . = ALIGN(8);
        *(.log_buffer)
        . = ALIGN(8);
    } > RAM

    .bss :
    {
        . = ALIGN(8);
        _sbss = .;
        *(.bss)
        *(.bss*)
        . = ALIGN(8);
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM

    .heap :
    {
        . = ALIGN(8);
        _sheap = .;
        . = . + 4K;
        _eheap = .;
    } > RAM

    /* User stack section to check for RAM usage bounds */
    ._user_stack :
    {
        . = ALIGN(8);
        _sstack = .;

        /* Allocate small private buffers for the hardware exception modes */
        . = . + 1K;
        _und_stack_top = .;

        . = . + 1K;
        _abt_stack_top = .;

        /* Allocate your core 32K application user stack space */
        . = . + 32K;
        . = ALIGN(8);
        _estack = .;
    } > RAM
}
