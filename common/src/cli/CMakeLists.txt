set(THIS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(DRV_INC_DIR ${CMAKE_SOURCE_DIR}/src/drivers/include)
set(ARCH_INC_DIR ${COMMON_INC_DIR}/arch)
set(CLI_INC_DIR ${COMMON_INC_DIR}/cli)
set(LOG_INC_DIR ${COMMON_INC_DIR}/diagnostic/logging)
set(TEST_INC_DIR ${COMMON_INC_DIR}/tests)
set(MOCK_LIBC_INC_DIR ${COMMON_INC_DIR}/lib/libc)
set(THIRD_PARTY_CLI_LIB_INC_DIR "${CMAKE_SOURCE_DIR}/third_party/lib/embedded-cli/lib/include")

# --- CLI (cli_lib) ---

set(CLI_INC_DIRS
    ${COMMON_INC_DIR}
    ${COMMON_INC_DIR}/diagnostic/profiler
    ${ARCH_INC_DIR}
    ${CLI_INC_DIR}
    ${DRV_INC_DIR}
    ${LOG_INC_DIR}
    ${THIRD_PARTY_CLI_LIB_INC_DIR}
)

if(CLI) # full CLI
set(CLI_SOURCES
    ${THIS_DIR}/cli_log.c
    ${THIS_DIR}/cli_main.c
    ${THIS_DIR}/cli_system.c
    ${THIS_DIR}/cli_dispatch.c
    ${THIS_DIR}/cli_system_diag.c
    ${THIS_DIR}/cli_set_cmd_impl.c
    ${THIS_DIR}/cli_show_cmd_impl.c
)

add_library(cli_lib OBJECT ${CLI_SOURCES})

if(TEST)
    list(APPEND CLI_INC_DIRS ${TEST_INC_DIR})
    target_sources(cli_lib PRIVATE ${THIS_DIR}/cli_test.c)
    target_compile_definitions(cli_lib PRIVATE ENABLE_TEST)
else()
    target_sources(cli_lib PRIVATE ${THIS_DIR}/cli_no_test.c)
endif()

if(RTOS)
    target_compile_definitions(cli_lib PRIVATE ENABLE_RTOS)
else()
    target_sources(cli_lib PRIVATE ${THIS_DIR}/cli_no_sim.c)
endif()

if(RTOS AND ENABLE_SIM)
    target_compile_definitions(cli_lib PRIVATE ENABLE_SIM)
endif()

if(ENABLE_PROF)
    target_compile_definitions(cli_lib PRIVATE ENABLE_PROF)
endif()

# full CLI uses embedded cli lib
target_link_libraries(cli_lib PUBLIC embedded_cli_lib)

else() # minimal CLI
set(CLI_SOURCES
    ${THIS_DIR}/cli_mini.c
)
add_library(cli_lib OBJECT ${CLI_SOURCES})
endif() # CLI

if(BARE_METAL)
    target_compile_definitions(cli_lib PRIVATE BARE_METAL)
    if(RTOS AND ENABLE_BLE)
        target_compile_definitions(cli_lib PRIVATE ENABLE_BLE)
    endif() # ENABLE_BLE
    if(ENABLE_PCI)
        target_compile_definitions(cli_lib PRIVATE ENABLE_PCI)
    endif() # ENABLE_PCI
endif() # BARE_METAL

target_include_directories(cli_lib BEFORE PRIVATE ${MOCK_LIBC_INC_DIR})
target_include_directories(cli_lib PRIVATE ${CLI_INC_DIRS})
target_compile_options(cli_lib PRIVATE -ffreestanding -fno-builtin ${OFLAGS})
