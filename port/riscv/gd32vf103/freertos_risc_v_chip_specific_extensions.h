#ifndef __FREERTOS_RISC_V_CHIP_SPECIFIC_EXTENSIONS_H__
#define __FREERTOS_RISC_V_CHIP_SPECIFIC_EXTENSIONS_H__

#define portasmHAS_MTIME 1
#define portasmADDITIONAL_CONTEXT_SIZE 0

// clang-format off
.macro portasmSAVE_ADDITIONAL_REGISTERS
    // No additional registers to save on RV32IMAC
.endm

.macro portasmRESTORE_ADDITIONAL_REGISTERS
    // No additional registers to restore on RV32IMAC
.endm
// clang-format on

#endif // __FREERTOS_RISC_V_CHIP_SPECIFIC_EXTENSIONS_H__