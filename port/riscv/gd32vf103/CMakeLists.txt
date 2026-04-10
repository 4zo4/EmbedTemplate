set(ARCH_WIDTH 32 PARENT_SCOPE)
set(CPU_FLAGS "-march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany")
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/gd32vf103.ld")

find_path(NEWLIB_INC_DIR NAMES string.h HINTS "/usr/riscv64-unknown-elf/include")
if(NEWLIB_INC_DIR)
    set(LIB_INC_DIR ${NEWLIB_INC_DIR})
    set(CMAKE_EXE_LINKER_FLAGS "${CPU_FLAGS} --specs=nano.specs -Wl,--gc-sections -T ${LINKER_SCRIPT} -Wl,--entry=_start" PARENT_SCOPE)
    set(EXTRA_LIBS "" PARENT_SCOPE)
else()
    find_file(PICOLIBC_SPECS NAMES picolibc.specs HINTS "/usr/lib/picolibc/riscv64-unknown-elf")
    if(PICOLIBC_SPECS)
        set(LIB_INC_DIR "/usr/lib/picolibc/riscv64-unknown-elf/include")
        set(CMAKE_EXE_LINKER_FLAGS "${CPU_FLAGS} -Wl,--gc-sections -T ${LINKER_SCRIPT} -nostartfiles -nodefaultlibs -Wl,--entry=_start" PARENT_SCOPE)
        set(EXTRA_LIBS "-lgcc" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "No RISC-V Newlib or Picolibc C library found!")
    endif()
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} -I${LIB_INC_DIR}" PARENT_SCOPE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)

set(FREERTOS_CHIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
set(FREERTOS_PORT "RISC-V" PARENT_SCOPE)

set(TARGET_CHIP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/port.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/startup.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/time.c"
    PARENT_SCOPE
)
