# arm-none-eabi.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TARGET_ARCH "arm")
set(TARGET_CHIP "stm32f4")
set(FREERTOS_PORT "ARM_CM4F")

# Specify the cross-compiler
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Target specific flags for STM32F4 (Cortex-M4 with FPU)
set(FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${FLAGS}" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS "${FLAGS}" CACHE INTERNAL "")
set(CMAKE_ASM_FLAGS "${FLAGS}" CACHE INTERNAL "")
set(CMAKE_EXE_LINKER_FLAGS "${FLAGS} --specs=nosys.specs" CACHE INTERNAL "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
