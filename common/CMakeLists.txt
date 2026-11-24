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
set(LOG_INC_DIRS
    ${THIS_DIR}/include
    ${THIS_DIR}/include/diagnostic/logging
)

set(TFA_LIBC_SOURCES
    ${TFA_LIBC_SRC_DIR}/printf.c
    ${TFA_LIBC_SRC_DIR}/snprintf.c
    ${TFA_LIBC_SRC_DIR}/memset.c
    ${TFA_LIBC_SRC_DIR}/memcpy.c
    ${TFA_LIBC_SRC_DIR}/strlen.c
    ${TFA_LIBC_SRC_DIR}/strcmp.c
    ${TFA_LIBC_SRC_DIR}/memmove.c  # Used for buffer shifts
    ${TFA_LIBC_SRC_DIR}/strncmp.c  # Used for "help" prefix checks
    ${TFA_LIBC_SRC_DIR}/strchr.c   # Used for arg parsing
    ${TFA_LIBC_SRC_DIR}/strlcpy.c  # Safer string copying
    ${TFA_LIBC_SRC_DIR}/memcmp.c
    ${TFA_LIBC_SRC_DIR}/strlcat.c
    ${TFA_LIBC_SRC_DIR}/strnlen_secure.c
    ${TFA_LIBC_SRC_DIR}/strcpy_secure.c
)
set(TFA_LIBC_MOCK_SOURCES
    ${MOCK_LIBC_SRC_DIR}/assert.c  # Custom __assert_func + Panic Dump
    ${MOCK_LIBC_SRC_DIR}/termios.c # Custom termios bridge
    ${MOCK_LIBC_SRC_DIR}/stdio.c   # Mock fflush
    ${MOCK_LIBC_SRC_DIR}/signal.c  # Mock signal/SIGINT
)

# --- TF-A C Lib (tfa_libc_lib) ---
if(ARCH_WIDTH EQUAL 32)
    set(TFA_ARCH_SUBDIR "aarch32")
else()
    set(TFA_ARCH_SUBDIR "aarch64")
endif()
set(TFA_ARCH_INC "${TFA_LIBC_INC_DIR}/${TFA_ARCH_SUBDIR}")
# Build the TFA Libc Object Library
add_library(tfa_libc_lib OBJECT ${TFA_LIBC_SOURCES} ${TFA_LIBC_MOCK_SOURCES})
target_include_directories(tfa_libc_lib PRIVATE
    ${TFA_ARCH_INC} # TF-A Arch-Specific Headers
    ${MOCK_LIBC_INC_DIR} # selected libc mocks
    ${TFA_LIBC_INC_DIR} # TF-A Libc Headers
    ${ARCH_INC_DIR} # mock sources need access to arch-specific ops
)
if(BARE_METAL)
    target_compile_definitions(tfa_libc_lib PRIVATE BARE_METAL)
endif()
# This lib is harden for Bare-Metal: prevent standard library leakage
target_compile_options(tfa_libc_lib PRIVATE -ffreestanding -nostdlib -fno-builtin ${OFLAGS})

# --- FreeRTOS (freertos_lib) ---
if(RTOS)
    if(BARE_METAL)
        # The path to the FreeRTOS port sources is defined by the FREERTOS_PORT variable in port/<arch>/<chip>/CMakeLists.txt
        set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/GCC/${FREERTOS_PORT}")
        set(FREERTOS_PORT_SOURCES "")
        set(FREERTOS_MOCK_SOURCES "")
        if("${TARGET_ARCH}" STREQUAL "riscv" OR
            "${FREERTOS_PORT}" STREQUAL "ARM_CA9" OR
            "${FREERTOS_PORT}" STREQUAL "IA32_flat")
            list(APPEND FREERTOS_PORT_SOURCES "${FREERTOS_PORT_DIR}/portASM.S")
        endif()
        if("${FREERTOS_PORT}" MATCHES "ARM_CM33_NTZ")
            list(APPEND FREERTOS_PORT_SOURCES "${FREERTOS_PORT_DIR}/portasm.c")
        endif()
        list(APPEND FREERTOS_PORT_SOURCES "${FREERTOS_PORT_DIR}/port.c")
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
        ${FREERTOS_DIR}/stream_buffer.c
    )
    add_library(freertos_lib OBJECT ${FREERTOS_SOURCES} ${FREERTOS_PORT_SOURCES} ${FREERTOS_MOCK_SOURCES})
    target_include_directories(freertos_lib PRIVATE
        ${FREERTOS_DIR}/include
        ${FREERTOS_PORT_DIR}
        ${FREERTOS_PORT_DIR}/utils
        ${FREERTOS_CHIP_DIR} # for chip-specific FreeRTOS config and hooks defined in port/<arch>/<chip>/CMakeLists.txt
        ${THIS_DIR}/include # for common terminal codes
        ${THIS_DIR}/include/os/freertos # freertos config
    )
    target_compile_options(freertos_lib PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wa,--no-warn>)
    target_compile_options(freertos_lib PRIVATE -D_REENTRANT -ffreestanding -fno-builtin ${OFLAGS})
    if(ENABLE_TARGET_CHIP)
        target_compile_definitions(freertos_lib PRIVATE "${ENABLE_TARGET_CHIP}")
    endif()
    if("${FREERTOS_PORT}" STREQUAL "ARM_CA9")
        target_compile_definitions(freertos_lib PRIVATE __ARM_ARCH_7A__)
    endif()
endif()

# --- CLI (cli_lib) ---
add_subdirectory(src/cli)

# --- Infrastructure (infra_lib) ---
set(INFRA_SOURCES
    ${THIS_DIR}/src/fifo.c
    ${THIS_DIR}/src/diagnostic/logging/log_infra.c
)
set(INFRA_INC_DIRS
    ${THIS_DIR}/include
    ${ARCH_INC_DIR}
    ${LOG_INC_DIRS}
)
if(RTOS AND ENABLE_NET)
    list(APPEND INFRA_SOURCES "${THIS_DIR}/src/net/nimble/hal/hal_uart.c")
endif()
add_library(infra_lib OBJECT ${INFRA_SOURCES})
if(BARE_METAL)
    target_sources(infra_lib PRIVATE ${THIS_DIR}/src/syscalls.c)
    target_compile_definitions(infra_lib PRIVATE BARE_METAL)
else()
    target_sources(infra_lib PRIVATE
        ${THIS_DIR}/src/lib/libc/posix_time.c
        ${THIS_DIR}/src/lib/libc/posix_utils.c
    )
endif()
target_include_directories(infra_lib PRIVATE ${INFRA_INC_DIRS})
if(LOG_BUF_SIZE)
    set_source_files_properties(
        ${THIS_DIR}/src/diagnostic/logging/log_infra.c
        PROPERTIES COMPILE_DEFINITIONS "LOG_BUF_SIZE=${LOG_BUF_SIZE}"
    )
endif()
set_source_files_properties(
    ${THIS_DIR}/src/diagnostic/logging/log_infra.c
    PROPERTIES COMPILE_OPTIONS "-Wno-stringop-overflow"
)
if(ENABLE_PROF)
set_source_files_properties(
    ${THIS_DIR}/src/diagnostic/logging/log_infra.c
    PROPERTIES COMPILE_FLAGS "-finstrument-functions -fno-inline"
)
endif()
target_compile_options(infra_lib PRIVATE -ffreestanding -fno-builtin ${OFLAGS})

if(ENABLE_PROF)
    add_subdirectory(src/diagnostic/profiler)
    target_link_libraries(infra_lib PRIVATE profiler_lib)
    set_target_properties(infra_lib PROPERTIES LINKER_LANGUAGE CXX)
endif()

# --- Embedded CLI (embedded_cli_lib) ---
if(CLI)
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
        ${OFLAGS}
    )
endif()
