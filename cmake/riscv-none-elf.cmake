set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(TARGET_ARCH "riscv" CACHE STRING "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

set(CMAKE_C_COMPILER riscv64-unknown-elf-gcc)
set(CMAKE_ASM_COMPILER riscv64-unknown-elf-gcc)
set(CMAKE_CXX_COMPILER riscv64-unknown-elf-g++)

set(CMAKE_C_FLAGS "${CPU_FLAGS} -ffunction-sections -fdata-sections" CACHE INTERNAL "")
set(PICOLIBC_INC "/usr/lib/picolibc/riscv64-unknown-elf/include")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${PICOLIBC_INC}" CACHE INTERNAL "")
set(CMAKE_ASM_FLAGS "${CPU_FLAGS}" CACHE INTERNAL "")

set(CMAKE_EXE_LINKER_FLAGS "${CPU_FLAGS} -Wl,--gc-sections --specs=picolibc.specs" CACHE INTERNAL "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
