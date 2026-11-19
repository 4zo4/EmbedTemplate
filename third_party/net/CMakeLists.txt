if(NOT (BARE_METAL AND RTOS AND ENABLE_BLE))
    return()
endif()

# EXTERN_PORT vs internal port(s) within nimBLE
# .
# ├── include
# │   ├── nimble
# │   │   ├── nimble_npl_hw_isr.h
# │   │   ├── nimble_npl_module_log.h
# │   │   ├── nimble_npl_os.h
# │   │   ├── nimble_npl_os_isr.h
# │   │   └── nimble_npl_os_log.h
# │   └── syscfg
# │       └── syscfg.h
# └── src
#     ├── nimble_port_freertos.c
#     └── npl_os_freertos.c
#
set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/GCC/${FREERTOS_PORT}")
set(NIMBLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nimble")
set(NIMBLE_HAL_INC_DIR "${CMAKE_SOURCE_DIR}/common/src/net/nimble")
set(NIMBLE_EXTERN_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/port/nimble")

file(GLOB NIMBLE_HOST_SRCS CONFIGURE_DEPENDS
    "${NIMBLE_DIR}/nimble/host/src/*.c"
    "${NIMBLE_DIR}/nimble/host/util/src/*.c"
    "${NIMBLE_DIR}/nimble/host/services/gap/src/*.c"
    "${NIMBLE_DIR}/nimble/host/services/gatt/src/*.c"
    "${NIMBLE_DIR}/nimble/host/store/config/src/*.c"
)

list(REMOVE_ITEM NIMBLE_HOST_SRCS
)

file(GLOB NIMBLE_PORT_SRCS CONFIGURE_DEPENDS
    "${NIMBLE_DIR}/porting/nimble/src/*.c"
    "${NIMBLE_EXTERN_PORT_DIR}/src/*.c"
    "${NIMBLE_DIR}/porting/npl/freertos/src/*.c"
)

set_source_files_properties(
    "${NIMBLE_EXTERN_PORT_DIR}/src/npl_os_freertos.c"
    PROPERTIES
    COMPILE_DEFINITIONS [[NIMBLE_NPL_OS_EXTRA_INCLUDE="nimble/nimble_npl_hw_isr.h"]]
)

list(REMOVE_ITEM NIMBLE_PORT_SRCS
    "${NIMBLE_DIR}/porting/npl/freertos/src/nimble_port_freertos.c" # use extern port
    "${NIMBLE_DIR}/porting/npl/freertos/src/npl_os_freertos.c" # use extern port
    "${NIMBLE_DIR}/porting/nimble/src/hal_timer.c"
)

set(NIMBLE_SRCS
    ${NIMBLE_HOST_SRCS}
    ${NIMBLE_PORT_SRCS}
    "${NIMBLE_DIR}/nimble/transport/src/transport.c"
    "${NIMBLE_DIR}/nimble/transport/uart_ll/src/hci_uart.c"
    "${NIMBLE_DIR}/nimble/transport/common/hci_h4/src/hci_h4.c"
)

add_library(nimble_lib OBJECT ${NIMBLE_SRCS})

target_include_directories(nimble_lib PUBLIC
    "${NIMBLE_EXTERN_PORT_DIR}/include"
    "${NIMBLE_HAL_INC_DIR}"
    "${NIMBLE_DIR}/nimble/include"
    "${NIMBLE_DIR}/nimble/host/include"
    "${NIMBLE_DIR}/nimble/host/util/include"
    "${NIMBLE_DIR}/nimble/host/services/gap/include"
    "${NIMBLE_DIR}/nimble/host/services/gatt/include"
    "${NIMBLE_DIR}/nimble/host/store/config/include"
    "${NIMBLE_DIR}/nimble/transport/common/hci_h4/include"
    "${NIMBLE_DIR}/nimble/transport/include"
    "${NIMBLE_DIR}/nimble/transport/uart_ll/include"
    "${NIMBLE_DIR}/porting/nimble/include"
    "${NIMBLE_DIR}/porting/npl/freertos/include"
    "${NIMBLE_DIR}/porting/npl/linux/include"
)

target_include_directories(nimble_lib PRIVATE
    "${FREERTOS_DIR}/include"
    "${FREERTOS_PORT_DIR}"
    "${COMMON_INC_DIR}/os/freertos" # FreeRTOSConfig.h
    "${COMMON_INC_DIR}"
    "${COMMON_INC_DIR}/diagnostic/logging"
)

# select FreeRTOSConfig.h settings for the target
if(ENABLE_TARGET_CHIP)
    target_compile_definitions(nimble_lib PRIVATE "${ENABLE_TARGET_CHIP}")
endif()

target_compile_options(nimble_lib PRIVATE
    -ffreestanding
    -nostdlib
    -fno-builtin
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-missing-field-initializers
    ${OFLAGS}
)
