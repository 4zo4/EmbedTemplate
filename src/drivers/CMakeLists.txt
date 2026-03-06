set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DRV_INC_DIR ${THIS_DIR}/include)
set(REGS_INC_DIR ${DRV_INC_DIR}/regs)
set(LOG_INC_DIRS
    ${THIS_DIR}/../../common/include
    ${THIS_DIR}/../../common/include/diagnostic/logging
)
set(SOC_SOURCES
    gpio/gpio.c
    sysctrl/sysctrl.c
    timer/timer.c
    uart/uart.c
)

add_library(soc_drivers STATIC ${SOC_SOURCES})
add_dependencies(soc_drivers generated_regs)
target_include_directories(soc_drivers PRIVATE
    ${DRV_INC_DIR}
    ${LOG_INC_DIRS}
    ${REGS_INC_DIR}
)
target_compile_options(soc_drivers PRIVATE -Wall -Wextra -g)
