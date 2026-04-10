MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 32K
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
        /* Source of .data in Flash */
        _sidata = .; 
    } > FLASH

    /* Data section with Global Pointer */
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

    _estack = ALIGN(ORIGIN(RAM) + LENGTH(RAM), 16);
    
    /* Alias for startup.c for __stack */
    __stack = _estack; 
    
    /* Alias for the FreeRTOS port requirement */
    __freertos_irq_stack_top = _estack;
}
