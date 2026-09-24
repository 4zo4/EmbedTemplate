/* cortex-m33.sec.ld */
ENTRY(secure_boot_entry)

MEMORY
{
    /* Secure Flash and RAM Alias for the QEMU MPS2-AN505, 0x1xxxxxxx alias window */
    SECURE_FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 128K
    SECURE_RAM (rwx)  : ORIGIN = 0x38000000, LENGTH = 16K
}

SECTIONS
{
    .secure_vector_table : ALIGN(128)
    {
        KEEP(*(.secure_vector_table))
        . = ALIGN(4);
    } > SECURE_FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > SECURE_FLASH

    _estack = ORIGIN(SECURE_RAM) + LENGTH(SECURE_RAM);
}
