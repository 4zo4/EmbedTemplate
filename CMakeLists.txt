cmake_minimum_required(VERSION 3.25)

project(EmbedTemplate C ASM)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
        message(STATUS "Profile: Embedded ARM")
        set(TARGET_ARCH "arm")
        add_compile_definitions(ARCH_ARM)
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv")
        message(STATUS "Profile: Embedded RISC-V")
        set(TARGET_ARCH "riscv")
        add_compile_definitions(ARCH_RISCV)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES  "x86")
        message(STATUS "Profile: Embedded x86")
        set(TARGET_ARCH "x86")
        add_compile_definitions(ARCH_X86)
    else()
        message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(BARE_METAL TRUE)
else()
    message(STATUS "Profile: Host x86")
    add_compile_definitions(ARCH_X86)
    set(BARE_METAL FALSE)
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Global Options ---
option(CLI "Full CLI enabled" ON)
option(RTOS "Rtos enabled" ON)
option(TEST "Test enabled" ON)

set(ENABLE_SIM TRUE CACHE BOOL "Simulation enabled")

# --- Generator of C include files for Hardware Registers ---
set(RDL_SRC_DIR "${CMAKE_SOURCE_DIR}/hw/rdl")
set(RDL_GEN_DIR "${CMAKE_SOURCE_DIR}/src/drivers/include/regs")
set(PEAKRDL "${CMAKE_SOURCE_DIR}/tools/peakrdl")

# --- Subdirectories ---

if(BARE_METAL)
    set(PLATFORM_DIR "${CMAKE_SOURCE_DIR}/port/${TARGET_ARCH}/${TARGET_CHIP}")
    if(EXISTS "${PLATFORM_DIR}/CMakeLists.txt")
        message(STATUS "Platform: ${TARGET_ARCH}/${TARGET_CHIP}")
        string(TOUPPER "${TARGET_CHIP}" TARGET_CHIP_UPPER)
        string(REPLACE "-" "_" TARGET_CHIP_NAME "${TARGET_CHIP_UPPER}")
        set(ENABLE_TARGET_CHIP "TARGET_${TARGET_CHIP_NAME}" CACHE INTERNAL "Enable Target Chip Macro")
        message(STATUS "Target: ${ENABLE_TARGET_CHIP}")

        add_subdirectory("${PLATFORM_DIR}")
    else()
        message(FATAL_ERROR "Invalid platform directory: ${PLATFORM_DIR}")
    endif()
endif()

add_subdirectory(third_party)
add_subdirectory(src/drivers)

if(TEST)
    add_subdirectory(src/testhub)
endif()

add_subdirectory(common) # require FREERTOS_PORT variable defined in port/<arch>/<chip>/CMakeLists.txt
add_subdirectory(port)
