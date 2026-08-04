set(ARCH_WIDTH 32 PARENT_SCOPE)
set(TARGET_CHIP_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv4-sp-d16
)
string(JOIN " " CPU_FLAGS ${TARGET_CHIP_FLAGS})
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/stm32f4.ld")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CPU_FLAGS} -T${LINKER_SCRIPT}" PARENT_SCOPE)
add_link_options(
    "-Wl,-e,reset_handler"
    "--specs=nano.specs"
    "--specs=nosys.specs"
    "-Wl,--gc-sections"
)

set(FREERTOS_PORT "ARM_CM4F" PARENT_SCOPE)
set(CORTEX_M_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cortex-m)
set(TARGET_CHIP_LIB "stm32f4_lib" PARENT_SCOPE)
set(TARGET_CHIP_SOURCES "" PARENT_SCOPE) # keep it empty for stm32f4 port - we build the stm32f4_lib locally
set(TARGET_CHIP_LIB_SOURCES
    ${CORTEX_M_DIR}/common.c
    ${CORTEX_M_DIR}/startup.c
    ${CORTEX_M_DIR}/time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port.c
)
add_library(stm32f4_lib OBJECT
    ${TARGET_CHIP_LIB_SOURCES}
)
set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
target_include_directories(stm32f4_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CORTEX_M_DIR}
    ${COMMON_INC_DIR}
    ${COMMON_INC_DIR}/arch
    ${COMMON_INC_DIR}/diagnostic/logging
    ${COMMON_INC_DIR}/os/freertos # freertos config
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
)
target_compile_options(stm32f4_lib PRIVATE ${TARGET_CHIP_FLAGS} -fno-builtin-printf -fno-builtin-puts -Wall -Wextra -O3)
target_compile_definitions(stm32f4_lib PRIVATE BARE_METAL)
if(RTOS)
    target_compile_definitions(stm32f4_lib PRIVATE ENABLE_RTOS "${ENABLE_TARGET_CHIP}")
endif()
