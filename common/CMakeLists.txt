set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(THIRD_PARTY_CLI_LIB_INC_DIR "${CMAKE_SOURCE_DIR}/third_party/lib/embedded-cli/lib/include")
set(THIRD_PARTY_CLI_LIB_DIR "${CMAKE_SOURCE_DIR}/third_party/lib/embedded-cli/lib")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
set(TFA_DIR "${CMAKE_SOURCE_DIR}/third_party/bsp/tf-a")
set(TFA_LIBC_SRC_DIR ${TFA_DIR}/lib/libc)
set(TFA_LIBC_INC_DIR ${TFA_DIR}/include/lib/libc)
set(MOCK_LIBC_SRC_DIR ${THIS_DIR}/src/lib/libc)
set(MOCK_LIBC_INC_DIR ${THIS_DIR}/include/lib/libc)
set(ARCH_INC_DIR ${THIS_DIR}/include/arch)
set(DRV_INC_DIR ${THIS_DIR}/../src/drivers/include)
set(REGS_INC_DIR ${DRV_INC_DIR}/regs)
set(LOG_INC_DIRS
    ${THIS_DIR}/include
    ${THIS_DIR}/include/diagnostic/logging
)
set(CLI_INC_DIR ${THIS_DIR}/include/cli)
set(TEST_INC_DIR ${THIS_DIR}/include/tests)

set(TFA_LIBC_SOURCES
    ${TFA_LIBC_SRC_DIR}/printf.c
    ${TFA_LIBC_SRC_DIR}/snprintf.c
    ${TFA_LIBC_SRC_DIR}/memset.c
    ${TFA_LIBC_SRC_DIR}/memcpy.c
    ${TFA_LIBC_SRC_DIR}/memmove.c
    ${TFA_LIBC_SRC_DIR}/strlen.c
    ${TFA_LIBC_SRC_DIR}/strcmp.c
    ${TFA_LIBC_SRC_DIR}/memmove.c  # Used for buffer shifts
    ${TFA_LIBC_SRC_DIR}/strncmp.c  # Used for "help" prefix checks
    ${TFA_LIBC_SRC_DIR}/strchr.c   # Used for arg parsing
    ${TFA_LIBC_SRC_DIR}/strlcpy.c  # Safer string copying
)
set(MOCK_SOURCES
    ${MOCK_LIBC_SRC_DIR}/assert.c  # Custom __assert_func + Panic Dump
    ${MOCK_LIBC_SRC_DIR}/termios.c # Custom termios bridge
    ${MOCK_LIBC_SRC_DIR}/stdio.c   # Mock fflush
    ${MOCK_LIBC_SRC_DIR}/signal.c  # Mock signal/SIGINT
)

# --- TF-A C Lib (tfa_libc_lib) ---
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TFA_ARCH_SUBDIR "aarch64")
else()
    set(TFA_ARCH_SUBDIR "aarch32")
endif()
set(TFA_ARCH_INC "${TFA_LIBC_INC_DIR}/${TFA_ARCH_SUBDIR}")
# Build the TFA Libc Object Library
add_library(tfa_libc_lib OBJECT ${TFA_LIBC_SOURCES} ${MOCK_SOURCES})
target_include_directories(tfa_libc_lib PRIVATE
    ${TFA_ARCH_INC} # TF-A Arch-Specific Headers
    ${MOCK_LIBC_INC_DIR} # selected libc mocks
    ${TFA_LIBC_INC_DIR} # TF-A Libc Headers
    ${ARCH_INC_DIR}
)
if(BARE_METAL)
    target_compile_definitions(tfa_libc_lib PRIVATE BARE_METAL)
endif()
# This lib is harden for Bare-Metal: prevent standard library leakage
target_compile_options(tfa_libc_lib PRIVATE -ffreestanding -nostdlib -fno-builtin -O3)

# --- FreeRTOS (freertos_lib) ---
if(RTOS)
    if(BARE_METAL)
        # The path to the FreeRTOS port sources is defined by the FREERTOS_PORT variable in <arch>.cmake toolchain
        set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/GCC/${FREERTOS_PORT}")
        set(FREERTOS_MOCK_SOURCES "")
        set(FREERTOS_PORT_SOURCES ${FREERTOS_PORT_DIR}/port.c)
    else()
        set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix")
        set(FREERTOS_MOCK_SOURCES ${THIS_DIR}/src/os/freertos/mock_interrupt.c)
        set(FREERTOS_PORT_SOURCES ${FREERTOS_PORT_DIR}/port.c ${FREERTOS_PORT_DIR}/utils/wait_for_event.c)
    endif()
    set(FREERTOS_SOURCES
        ${FREERTOS_DIR}/tasks.c
        ${FREERTOS_DIR}/list.c
        ${FREERTOS_DIR}/queue.c
        ${FREERTOS_DIR}/timers.c
        ${FREERTOS_DIR}/event_groups.c
    )
    add_library(freertos_lib OBJECT ${FREERTOS_SOURCES} ${FREERTOS_PORT_SOURCES} ${FREERTOS_MOCK_SOURCES})
    target_include_directories(freertos_lib PRIVATE
        ${FREERTOS_DIR}/include
        ${FREERTOS_PORT_DIR}
        ${FREERTOS_PORT_DIR}/utils
        ${THIS_DIR}/include # for common terminal codes
        ${THIS_DIR}/include/os/freertos # freertos config
    )
    target_compile_options(freertos_lib PRIVATE -D_REENTRANT -O3)
endif()

# --- CLI (cli_lib) ---
set(CLI_SOURCES
    ${THIS_DIR}/src/cli/cli_main.c
    ${THIS_DIR}/src/cli/cli_system.c
)
add_library(cli_lib OBJECT ${CLI_SOURCES})
if(TEST)
    target_sources(cli_lib PRIVATE ${THIS_DIR}/src/cli/cli_test.c)
    target_compile_definitions(cli_lib PRIVATE ENABLE_TEST)
else()
    target_sources(cli_lib PRIVATE ${THIS_DIR}/src/cli/cli_no_test.c)
endif()
if(RTOS)
    target_compile_definitions(cli_lib PRIVATE ENABLE_RTOS)
else()
    target_sources(cli_lib PRIVATE ${THIS_DIR}/src/cli/cli_no_sim.c)
endif()
if(BARE_METAL)
    target_compile_definitions(cli_lib PRIVATE BARE_METAL)
endif()

# CLI uses embedded cli lib
target_link_libraries(cli_lib PUBLIC embedded_cli_lib)
target_include_directories(cli_lib BEFORE PRIVATE ${MOCK_LIBC_INC_DIR})
target_include_directories(cli_lib PRIVATE
    ${THIS_DIR}/include
    ${ARCH_INC_DIR}
    ${CLI_INC_DIR}
    ${DRV_INC_DIR}
    ${LOG_INC_DIRS}
    ${TEST_INC_DIR}
    ${THIRD_PARTY_CLI_LIB_INC_DIR}
)
target_compile_options(cli_lib PRIVATE -ffreestanding -fno-builtin -O3)

# --- Infrastructure (infra_lib) ---
set(INFRA_SOURCES
    ${THIS_DIR}/src/fifo.c
    ${THIS_DIR}/src/diagnostic/logging/log_infra.c
)
add_library(infra_lib OBJECT ${INFRA_SOURCES})
if(BARE_METAL)
    target_compile_definitions(infra_lib PRIVATE BARE_METAL)
else()
    target_sources(infra_lib PRIVATE
        ${THIS_DIR}/src/lib/libc/posix_time.c
        ${THIS_DIR}/src/lib/libc/posix_utils.c
    )
endif()
target_include_directories(infra_lib PRIVATE
    ${THIS_DIR}/include
    ${ARCH_INC_DIR}
    ${LOG_INC_DIRS}
)
target_compile_options(infra_lib PRIVATE -ffreestanding -fno-builtin -O3)

# --- Embedded CLI (embedded_cli_lib) ---
set(EMBEDDED_CLI_SRC ${THIRD_PARTY_CLI_LIB_DIR}/src/embedded_cli.c)
add_library(embedded_cli_lib OBJECT ${EMBEDDED_CLI_SRC})
target_include_directories(embedded_cli_lib PRIVATE
    ${MOCK_LIBC_INC_DIR}
    ${THIRD_PARTY_CLI_LIB_INC_DIR}
)
target_compile_options(embedded_cli_lib PRIVATE 
    -ffreestanding 
    -nostdlib 
    -fno-builtin 
    -O3
)
