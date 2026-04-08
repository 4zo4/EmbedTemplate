set(PATH_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
set(PATH_COMMON_SRC ${PATH_COMMON}/src)
set(PATH_DRIVERS ${CMAKE_CURRENT_SOURCE_DIR}/../drivers)
set(SPEC_INC_DIR ${CMAKE_SOURCE_DIR}/third_party/lib/embedded-cli/lib/include)

# Path to common logs and arch headers
# Path to common test headers
set(COMMON_INC_DIRS
    ${PATH_COMMON}/include
    ${PATH_COMMON}/include/tests
    ${PATH_COMMON}/include/diagnostic/logging
)
# Paths to the drivers' public and register headers
set(DRV_INC_DIRS
    ${PATH_DRIVERS}/include
    ${PATH_DRIVERS}/include/regs
)
# Paths to the drivers' test headers
set(TEST_DEV_INC_DIRS
    ${PATH_DRIVERS}/gpio/tests
    ${PATH_DRIVERS}/sysctrl/tests
    ${PATH_DRIVERS}/timer/tests
    ${PATH_DRIVERS}/uart/tests
)
# Source files for the device/driver tests
set(TEST_DEV_SOURCES
    dev_test_registry.c # device test registry
    ${PATH_DRIVERS}/gpio/tests/test_gpio.c
    ${PATH_DRIVERS}/sysctrl/tests/test_sysctrl.c
    ${PATH_DRIVERS}/timer/tests/test_timer.c
    ${PATH_DRIVERS}/uart/tests/test_uart.c
)

add_library(soc_tests STATIC ${TEST_DEV_SOURCES})
add_dependencies(soc_tests generated_regs)
target_include_directories(soc_tests PRIVATE
    ${COMMON_INC_DIRS}
    ${DRV_INC_DIRS}
    ${TEST_DEV_INC_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(RTOS)
    target_compile_definitions(soc_tests PUBLIC ENABLE_RTOS)
endif()

target_compile_options(soc_tests PRIVATE -Wall -Wextra -O3)

set(TEST_SYS_SOURCES
    sys_test_registry.c # system test registry
    ${PATH_COMMON_SRC}/cli/tests/test_cli.c
    ${PATH_COMMON_SRC}/diagnostic/logging/tests/test_log.c
)

set_source_files_properties(${PATH_COMMON_SRC}/cli/tests/test_cli.c
                            PROPERTIES
                            INCLUDE_DIRECTORIES "${SPEC_INC_DIR}")

set(TEST_SYS_INC_DIRS
    ${PATH_DRIVERS}/include
    ${PATH_COMMON_SRC}/cli/tests
    ${PATH_COMMON_SRC}/diagnostic/logging/tests
)

add_library(sys_tests STATIC ${TEST_SYS_SOURCES})
target_compile_options(sys_tests PRIVATE -Wall -Wextra -O3)
target_include_directories(sys_tests PRIVATE
    ${COMMON_INC_DIRS}
    ${TEST_SYS_INC_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
