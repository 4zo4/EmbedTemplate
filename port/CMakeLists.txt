set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DRV_INC_DIR ${THIS_DIR}/../src/drivers/include)
set(REGS_INC_DIR ${DRV_INC_DIR}/regs)
set(LOG_INC_DIRS
    ${THIS_DIR}/../common/include
    ${THIS_DIR}/../common/include/diagnostic/logging
)
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix")
set(RTOS_LIBS "")
set(TESTS_LIBS "")

if(TEST)
    # From src/testhub src/drivers/<block name>/tests common/src/<name>/tests
    list(APPEND TESTS_LIBS soc_tests sys_tests)
endif()

if(RTOS)
    set(STARTUP ${THIS_DIR}/freertos_sim_main.c)
    list(APPEND RTOS_LIBS freertos_lib pthread rt)
else()
    set(STARTUP ${THIS_DIR}/posix_main.c)
endif()

add_executable(runner ${STARTUP})

# Link the SDK components
target_link_libraries(runner PRIVATE
    cli_lib
    infra_lib
    soc_drivers    # From src/drivers
    tfa_libc_lib   # TF-A libc
    ${TESTS_LIBS}
    ${RTOS_LIBS}
    embedded_cli_lib
)

target_include_directories(runner PRIVATE
    ${FREERTOS_DIR}/include
    ${FREERTOS_PORT_DIR}
    ${THIS_DIR}/../common/include/os/freertos # freertos config
    ${REGS_INC_DIR}
    ${DRV_INC_DIR}
    ${LOG_INC_DIRS}
)
target_compile_options(runner PRIVATE -Wall -Wextra -O3)