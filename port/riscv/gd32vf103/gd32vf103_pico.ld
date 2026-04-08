MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 32K
}

ENTRY(_start)

SECTIONS {
    .text : {
        KEEP(*(.vectors))
        KEEP(*(.init))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        KEEP(*(.fini))
        . = ALIGN(4);
        __data_source = .;
    } > FLASH

    .data : AT(__data_source) {
        . = ALIGN(4);
        __data_start = .;
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM

    .bss : {
        . = ALIGN(4);
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    __data_size = __data_end - __data_start;
    __bss_size = __bss_end - __bss_start;
    __stack = ORIGIN(RAM) + LENGTH(RAM);
    __freertos_irq_stack_top = __stack;
    __tls_base = 0;
}
