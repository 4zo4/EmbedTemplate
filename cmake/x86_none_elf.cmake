# x86_none_elf.cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86)

# Force the standard host system compiler tools to act as freestanding bare-metal tools
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_ASM_COMPILER gcc)

# Inform CMake to bypass standard dynamic user-space check routines
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=include 
    OUTPUT_VARIABLE GCC_INTERNAL_INC_DIR 
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -nostdlib -no-pie -Wl,--defsym=putc=putchar -Wl,--defsym=getc=getchar")
