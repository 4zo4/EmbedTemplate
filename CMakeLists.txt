cmake_minimum_required(VERSION 3.25)
project(EmbedTemplate)
project(EmbedTemplate C ASM)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
        message(STATUS "Target: Embedded ARM")
        set(TARGET_ARCH "arm")
        add_compile_definitions(ARCH_ARM)
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv")
        message(STATUS "Target: Embedded RISC-V")
        set(TARGET_ARCH "riscv")
        add_compile_definitions(ARCH_RISCV)
    else()
        message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(BARE_METAL TRUE)
else()
    message(STATUS "Target: Host x86")
    add_compile_definitions(ARCH_X86)
    set(BARE_METAL FALSE)
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Global Options ---
option(RTOS "Rtos enabled" ON)
option(TEST "Test enabled" ON)

# --- Generator of C include files for Hardware Registers ---
set(RDL_SRC_DIR "${CMAKE_SOURCE_DIR}/hw/rdl")
set(RDL_GEN_DIR "${CMAKE_SOURCE_DIR}/src/drivers/include/regs")
set(PEAKRDL "${CMAKE_SOURCE_DIR}/tools/peakrdl")

# --- Subdirectories ---

if(BARE_METAL)
    set(PLATFORM_DIR "${CMAKE_SOURCE_DIR}/port/${TARGET_ARCH}/${TARGET_CHIP}")
    if(EXISTS "${PLATFORM_DIR}/CMakeLists.txt")
        add_subdirectory("${PLATFORM_DIR}")
        message(STATUS "Platform: ${TARGET_ARCH}/${TARGET_CHIP}")
    else()
        message(FATAL_ERROR "Platform directory not found: ${PLATFORM_DIR}")
    endif()
endif()

add_subdirectory(third_party) 
add_subdirectory(src/drivers)

if(TEST)
    add_subdirectory(src/testhub)
endif()

add_subdirectory(common) # require FREERTOS_PORT variable defined in port/<arch>/<chip>/CMakeLists.txt
add_subdirectory(port)
