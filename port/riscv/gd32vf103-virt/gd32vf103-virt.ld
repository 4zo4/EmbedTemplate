MEMORY
{
    /* QEMU Virt unified RAM partitioned into separate code and data regions */
    FLASH (rx) : ORIGIN = 0x80000000, LENGTH = 4M
    RAM (rwx)  : ORIGIN = 0x80400000, LENGTH = 4M
}

ENTRY(_start)

SECTIONS {
    .text : {
        KEEP(*(.vectors))
        KEEP(*(.init))
        /* Legacy support for code using .reset_handler */
        KEEP(*(.reset_handler))

        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)

        . = ALIGN(4);
        /* Source of .data in Virtual Flash */
        _sidata = .;
    } > FLASH

    /* Data section with Global Pointer mapping */
    .data : AT(_sidata) {
        . = ALIGN(4);
        _sdata = .;
        /* Universal RISC-V requirement for relaxation */
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;

        /* Required for malloc/sbrk in syscalls.c */
        _end = .;
        PROVIDE(end = .);
    } > RAM

    /* Set up stack configuration space pointer at the top of RAM */
    _estack = ALIGN(ORIGIN(RAM) + LENGTH(RAM), 16);

    /* Alias for startup.c for __stack */
    __stack = _estack;

    /* Alias for the FreeRTOS port requirement */
    __freertos_irq_stack_top = _estack;
}
