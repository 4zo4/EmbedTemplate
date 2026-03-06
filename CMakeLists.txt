cmake_minimum_required(VERSION 3.25)
project(EmbedTemplate)

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
add_subdirectory(third_party) 

add_subdirectory(common)
add_subdirectory(src/drivers)

if(TEST)
    add_subdirectory(src/testhub)
endif()

add_subdirectory(port)
