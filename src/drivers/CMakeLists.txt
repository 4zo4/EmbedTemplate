set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DRV_INC_DIR ${THIS_DIR}/include)
set(REGS_INC_DIR ${DRV_INC_DIR}/regs)
set(LOG_INC_DIRS
    ${THIS_DIR}/../../common/include
    ${THIS_DIR}/../../common/include/diagnostic/logging
)
set(SOC_GPIO_SOURCES gpio/gpio.c)
set(SOC_PCI_SOURCES pci/pci_rc.c)
set(SOC_SYSCTRL_SOURCES sysctrl/sysctrl.c)
set(SOC_TIMER_SOURCES timer/timer.c)
set(SOC_UART_SOURCES uart/uart.c)

if("$CACHE{TARGET_CHIP}" STREQUAL "cortex-a9-virt")
    set(DRIVERS ENABLE_GPIO ENABLE_PCI)
else()
    set(DRIVERS ENABLE_GPIO ENABLE_SYSCTRL ENABLE_TIMER ENABLE_UART)
endif()

foreach(drv ${DRIVERS})
    if(NOT DEFINED CACHE{${drv}})
        set(${drv} TRUE CACHE BOOL "${drv}" FORCE)
    endif()
endforeach()

set(SOC_SOURCES "")
if(ENABLE_GPIO)
    list(APPEND SOC_SOURCES ${SOC_GPIO_SOURCES})
    set_source_files_properties(${SOC_GPIO_SOURCES} PROPERTIES COMPILE_DEFINITIONS ENABLE_GPIO)
endif()

if(ENABLE_PCI)
    list(APPEND SOC_SOURCES ${SOC_PCI_SOURCES})
    set_source_files_properties(${SOC_PCI_SOURCES} PROPERTIES COMPILE_DEFINITIONS ENABLE_PCI)
endif()

if(ENABLE_SYSCTRL)
    list(APPEND SOC_SOURCES ${SOC_SYSCTRL_SOURCES})
    set_source_files_properties(${SOC_SYSCTRL_SOURCES} PROPERTIES COMPILE_DEFINITIONS ENABLE_SYSCTRL)
endif()

if(ENABLE_TIMER)
    list(APPEND SOC_SOURCES ${SOC_TIMER_SOURCES})
    set_source_files_properties(${SOC_TIMER_SOURCES} PROPERTIES COMPILE_DEFINITIONS ENABLE_TIMER)
endif()

if(ENABLE_UART)
    list(APPEND SOC_SOURCES ${SOC_UART_SOURCES})
    set_source_files_properties(${SOC_UART_SOURCES} PROPERTIES COMPILE_DEFINITIONS ENABLE_UART)
endif()

add_library(soc_drivers STATIC ${SOC_SOURCES})
add_dependencies(soc_drivers generated_regs)
target_include_directories(soc_drivers PRIVATE
    ${DRV_INC_DIR}
    ${LOG_INC_DIRS}
    ${REGS_INC_DIR}
)
target_compile_options(soc_drivers PRIVATE -fno-builtin-printf -fno-builtin-puts -Wall -Wextra -O3)
