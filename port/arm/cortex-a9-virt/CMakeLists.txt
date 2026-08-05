set(ARCH_WIDTH 32 PARENT_SCOPE)
set(TARGET_CHIP_FLAGS
    -mcpu=cortex-a9
    -marm
    -mno-unaligned-access
    -mfloat-abi=hard
    -mfpu=neon-vfpv4
)
string(JOIN " " CPU_FLAGS ${TARGET_CHIP_FLAGS})
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cortex-a9-virt.ld")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)
set(CMAKE_EXE_LINKER_FLAGS "${CPU_FLAGS} -Wl,--gc-sections -T${LINKER_SCRIPT} -nostartfiles -nodefaultlibs -Wl,--entry=_start" PARENT_SCOPE)
set(EXTRA_LIBS "-lgcc" PARENT_SCOPE)
set(FREERTOS_PORT "ARM_CA9" PARENT_SCOPE)
set(CORTEX_A_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cortex-a)
set(TARGET_CHIP_LIB "cortex-a9-virt_lib" PARENT_SCOPE)
set(TARGET_CHIP_SOURCES "" PARENT_SCOPE) # keep it empty for cortex-a9-virt port - we build the cortex-a9-virt_lib locally
set(TARGET_CHIP_LIB_SOURCES
    ${CORTEX_A_DIR}/common.c
    ${CORTEX_A_DIR}/gic.c
    ${CORTEX_A_DIR}/startup.S
    ${CORTEX_A_DIR}/time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port.c
)
if(ENABLE_PCI)
    list(APPEND TARGET_CHIP_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/pci.c")
endif()
add_library(cortex-a9-virt_lib OBJECT
    ${TARGET_CHIP_LIB_SOURCES}
)
set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(DRIVERS_INC_DIR "${CMAKE_SOURCE_DIR}/src/drivers/include")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
target_include_directories(cortex-a9-virt_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CORTEX_A_DIR}
    ${COMMON_INC_DIR}
    ${COMMON_INC_DIR}/arch
    ${COMMON_INC_DIR}/lib/libc
    ${COMMON_INC_DIR}/diagnostic/logging
    ${COMMON_INC_DIR}/os/freertos # freertos config
    ${DRIVERS_INC_DIR}
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CA9
)
target_compile_options(cortex-a9-virt_lib PRIVATE
    ${TARGET_CHIP_FLAGS}
    -mgeneral-regs-only # don't use FPU registers
    -fno-builtin-printf
    -fno-builtin-puts
    -Wall
    -Wextra
    -O3
)

target_compile_definitions(cortex-a9-virt_lib PRIVATE BARE_METAL)
if(ENABLE_PCI)
    target_compile_definitions(cortex-a9-virt_lib PRIVATE ENABLE_PCI)
endif()
if(RTOS)
    target_compile_definitions(cortex-a9-virt_lib PRIVATE ENABLE_RTOS "${ENABLE_TARGET_CHIP}")
endif()
