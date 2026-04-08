set(ARCH_WIDTH 32 PARENT_SCOPE)

set(CPU_FLAGS "-march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)

find_file(PICOLIBC_SPECS NAMES picolibc.specs HINTS "/usr/lib/picolibc/riscv64-unknown-elf")

if(PICOLIBC_SPECS)
    message(STATUS "Using Picolibc: picolibc.specs")
    add_compile_definitions(TARGET_LIB_PICO=1)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} -DTARGET_LIB_PICO=1" PARENT_SCOPE)
    set(PICOLIBC_INC "/usr/lib/picolibc/riscv64-unknown-elf/include")
    include_directories(${PICOLIBC_INC})
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/gd32vf103_pico.ld")
    add_link_options("--specs=picolibc.specs")
else()
    message(STATUS "Using Newlib: nano.specs")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/gd32vf103_nano.ld")
    add_link_options(
        "-Wl,-e,reset_handler"
        "--specs=nano.specs"
        "-Wl,--gc-sections"
    )
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CPU_FLAGS} -T ${LINKER_SCRIPT}" PARENT_SCOPE)
set(FREERTOS_CHIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
set(FREERTOS_PORT "RISC-V" PARENT_SCOPE)

set(TARGET_CHIP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/port.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/startup.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/time.c"
    PARENT_SCOPE
)

