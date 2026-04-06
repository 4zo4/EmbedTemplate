set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(DRIVERS_INC_DIR "${CMAKE_SOURCE_DIR}/src/drivers/include")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
set(FREERTOS_PORT_DIR "")
set(RTOS_LIBS "")
set(TESTS_LIBS "")

if(BARE_METAL)
    set(STARTUP_SOURCES ${TARGET_CHIP_SOURCES})
    if(RTOS)
        list(APPEND STARTUP_SOURCES ${THIS_DIR}/freertos_sim_main.c)
        list(APPEND RTOS_LIBS freertos_lib)
    else()
        list(APPEND STARTUP_SOURCES ${THIS_DIR}/posix_main.c)
    endif()
else()
    if(RTOS)
        set(STARTUP_SOURCES ${THIS_DIR}/freertos_sim_main.c)
        list(APPEND RTOS_LIBS freertos_lib pthread rt)
    else()
        set(STARTUP_SOURCES ${THIS_DIR}/posix_main.c)
    endif()
endif()

add_executable(runner ${STARTUP_SOURCES})

if(BARE_METAL)
    set(LINKER_SCRIPT ${PLATFORM_DIR}/${TARGET_CHIP}.ld)
    target_link_options(runner PRIVATE "-T${LINKER_SCRIPT}" "--specs=nano.specs" "-Wl,--gc-sections" "-Wl,-z,muldefs")
    target_compile_definitions(runner PRIVATE BARE_METAL)
    if(RTOS)
        # The path to the FreeRTOS port sources is defined by the FREERTOS_PORT variable in <arch>.cmake toolchain
        set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/GCC/${FREERTOS_PORT}")
        list(APPEND RTOS_LIBS freertos_lib)
        target_compile_definitions(runner PRIVATE ENABLE_RTOS)
    endif()
    set_target_properties(runner PROPERTIES SUFFIX ".elf")
else()
    if(RTOS)
        set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix")
        list(APPEND RTOS_LIBS freertos_lib pthread rt)
        target_compile_definitions(runner PRIVATE ENABLE_RTOS)
    endif()
endif()

if(TEST)
    # From src/testhub src/drivers/<block name>/tests common/src/<name>/tests
    list(APPEND TESTS_LIBS soc_tests sys_tests)
endif()

# Link the SDK components
target_link_libraries(runner PRIVATE
    cli_lib
    infra_lib
    soc_drivers    # From src/drivers
    ${RTOS_LIBS}
    ${TESTS_LIBS}
    embedded_cli_lib
    tfa_libc_lib   # TF-A libc
)

target_include_directories(runner PRIVATE
    # FreeRTOS
    ${FREERTOS_DIR}/include
    ${FREERTOS_PORT_DIR}
    ${COMMON_INC_DIR}/os/freertos # freertos config
    
    # Common / Arch
    ${COMMON_INC_DIR}
    ${COMMON_INC_DIR}/arch
    ${COMMON_INC_DIR}/diagnostic/logging
    
    # Drivers / Hardware
    ${DRIVERS_INC_DIR}
    ${DRIVERS_INC_DIR}/regs
)

target_compile_options(runner PRIVATE -fno-builtin-printf -fno-builtin-puts -Wall -Wextra -O3)
target_link_options(runner PRIVATE "-Wl,-Map=runner.map")