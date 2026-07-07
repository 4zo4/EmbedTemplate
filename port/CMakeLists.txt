set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(DRIVERS_INC_DIR "${CMAKE_SOURCE_DIR}/src/drivers/include")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
set(FREERTOS_PORT_DIR "")
set(EMBEDDED_CLI_LIB "")
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

function(add_post_build_bin_extraction TARGET_NAME)
    if(NOT CMAKE_OBJCOPY)
        message(FATAL_ERROR "[CMake] CMAKE_OBJCOPY utility tool is missing from the toolchain configuration layout.")
    endif()
    message(STATUS "[CMake] Registering flat binary post-build extraction hook for: ${TARGET_NAME}")
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.bin
        COMMENT "Extracting raw flat flash firmware layout payload: ${TARGET_NAME}.bin"
        VERBATIM
    )
endfunction()

add_executable(runner ${STARTUP_SOURCES})
add_post_build_bin_extraction(runner)

if(BARE_METAL)
    set_target_properties(runner PROPERTIES SUFFIX ".elf")
endif()

if(BARE_METAL)
    target_compile_definitions(runner PRIVATE BARE_METAL)
endif()

if(RTOS)
    target_compile_definitions(runner PRIVATE ENABLE_RTOS)
endif()

if(ENABLE_TARGET_CHIP)
    target_compile_definitions(runner PRIVATE "${ENABLE_TARGET_CHIP}")
endif()

if(ENABLE_PCI)
    target_compile_definitions(runner PRIVATE ENABLE_PCI)
endif()

if(RTOS)
    if(BARE_METAL)
        # The path to the FreeRTOS port sources is defined by the FREERTOS_PORT variable in port/<arch>/<chip>/CMakeLists.txt
        set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/GCC/${FREERTOS_PORT}")
        list(APPEND RTOS_LIBS freertos_lib)
    else()
        set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix")
        list(APPEND RTOS_LIBS freertos_lib pthread rt)
    endif()
endif()

if(TEST)
    # From src/testhub src/drivers/<block name>/tests common/src/<name>/tests
    list(APPEND TESTS_LIBS soc_tests sys_tests)
endif()

if(CLI)
    list(APPEND EMBEDDED_CLI_LIB embedded_cli_lib)
endif()

# Link the SDK components
target_link_libraries(runner PRIVATE
    cli_lib
    infra_lib
    soc_drivers    # From src/drivers
    ${RTOS_LIBS}
    ${TESTS_LIBS}
    ${TARGET_CHIP_LIB} # From port/<arch>/<chip>/CMakeLists.txt
    ${EMBEDDED_CLI_LIB}
    tfa_libc_lib   # TF-A libc
    ${EXTRA_LIBS}
)

set(TARGET_INC_DIRS
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

if(BARE_METAL)
    list(APPEND TARGET_INC_DIRS ${COMMON_INC_DIR}/lib/libc)
endif()

target_include_directories(runner PRIVATE ${TARGET_INC_DIRS})
target_compile_options(runner PRIVATE -fno-builtin-printf -fno-builtin-puts -Wall -Wextra -O3)
target_link_options(runner PRIVATE "-Wl,-Map=runner.map")
