MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 32K
}

ENTRY(reset_handler)

SECTIONS {
    .text : {
        KEEP(*(.vectors))
        KEEP(*(.reset_handler))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        _sidata = .;
    } > FLASH

    .data : AT(_sidata) {
        _sdata = .;
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        _edata = .;
    } > RAM

    .bss : {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
    __freertos_irq_stack_top = _estack;
}
