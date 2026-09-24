/* cortex-m33.ld */
ENTRY(reset_handler)

MEMORY
{
    /* Flash/RAM Alias (Code and Data Regions) */
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 4096K
    RAM (rwx)  : ORIGIN = 0x28200000, LENGTH = 4096K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Used by startup.c to copy data from FLASH to RAM */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss; /* Some libs expect this name */
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _end = .;        /* Standard symbol for sbrk */
        PROVIDE(end = .);
    } > RAM

    /* User stack section to check for RAM overflow */
    ._user_stack :
    {
      . = ALIGN(8);
      . = . + 0x400; /* 1KB Stack */
      . = ALIGN(8);
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
