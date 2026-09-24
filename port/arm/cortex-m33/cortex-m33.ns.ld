/* cortex-m33.ns.ld */
ENTRY(reset_handler)

MEMORY
{
    /* Non-Secure Flash/RAM for the QEMU MPS2-AN505 */
    /* Total physical space of RAM Bank 0 + Bank 1 = 4096K (4MB) */
    /* Secure allocation at start of Bank 0 = 16K */
    /* Total available Non-Secure RAM = 4080K */
    NS_FLASH (rx) : ORIGIN = 0x00020000, LENGTH = 3968K /* 4096K (Total) - 128K (Secure) */
    NS_RAM (rwx)  : ORIGIN = 0x28004000, LENGTH = 4080K /* 4096K (Total) - 16K (Secure) */
}

SECTIONS
{
    .isr_vector : ALIGN(128)
    {
        . = ALIGN(4);
        PROVIDE(vector_table = .);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > NS_FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > NS_FLASH

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
    } > NS_RAM AT > NS_FLASH

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
    } > NS_RAM

    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _end = .;        /* Standard symbol for sbrk */
        PROVIDE(end = .);
    } > NS_RAM

    /* User stack section to check for RAM overflow */
    ._user_stack :
    {
      . = ALIGN(8);
      . = . + 0x400; /* 1KB Stack */
      . = ALIGN(8);
    } > NS_RAM

    _estack = ORIGIN(NS_RAM) + LENGTH(NS_RAM);
}
