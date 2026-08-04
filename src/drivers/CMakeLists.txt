set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DRV_INC_DIR ${THIS_DIR}/include)
set(REGS_INC_DIR ${DRV_INC_DIR}/regs)
set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
set(COMMON_INC_DIRS
    ${COMMON_INC_DIR}
    ${COMMON_INC_DIR}/arch
    ${COMMON_INC_DIR}/lib/libc
    ${COMMON_INC_DIR}/diagnostic/logging
)
set(FREERTOS_DIRS
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/${FREERTOS_PORT} # from the FREERTOS_PORT in port/<arch>/<chip>/CMakeLists.txt
    ${COMMON_INC_DIR}/os/freertos # freertos config
)
set(SOC_GPIO_SOURCES gpio/gpio.c)
set(SOC_PCI_SOURCES pci/pci_rc.c pci/pci_utils.c pci/pci_test.c)
set(SOC_SYSCTRL_SOURCES sysctrl/sysctrl.c)
set(SOC_TIMER_SOURCES timer/timer.c)
set(SOC_UART_SOURCES uart/uart.c)
set(PCI_ABLE_CHIPS "x86-virt" "cortex-a9-virt")

if(ENABLE_PCI)
    if(NOT "$CACHE{TARGET_CHIP}" IN_LIST PCI_ABLE_CHIPS)
        message(STATUS "PCI driver is not supported on target chip '$CACHE{TARGET_CHIP}' Disabling PCI driver...")
        unset(ENABLE_PCI CACHE)
    endif()
endif()

if("$CACHE{TARGET_CHIP}" STREQUAL "x86-virt")
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
    if(BARE_METAL)
        list(APPEND SOC_SOURCES ${SOC_PCI_SOURCES})
        set_source_files_properties(${SOC_PCI_SOURCES} PROPERTIES COMPILE_DEFINITIONS ENABLE_PCI)
    endif()
endif()
if(RTOS)
    set_property(SOURCE pci/pci_test.c APPEND PROPERTY COMPILE_DEFINITIONS ENABLE_RTOS "${ENABLE_TARGET_CHIP}")
    set_source_files_properties(pci/pci_test.c PROPERTIES INCLUDE_DIRECTORIES "${FREERTOS_DIRS}")
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
    ${COMMON_INC_DIRS}
    ${REGS_INC_DIR}
)
if(BARE_METAL)
    target_compile_definitions(soc_drivers PRIVATE BARE_METAL)
endif()
target_compile_options(soc_drivers PRIVATE -fno-builtin-printf -fno-builtin-puts -Wall -Wextra -O3)
