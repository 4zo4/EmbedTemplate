if(NOT (BARE_METAL AND RTOS AND ENABLE_MQTT))
    message(FATAL_ERROR "Invalid platform settings for MQTT")
endif()

set(COMMON_INC_DIR "${CMAKE_SOURCE_DIR}/common/include")
set(NET_DIR "${CMAKE_SOURCE_DIR}/third_party/net/")
set(COREMQTT_DIR "${NET_DIR}/coremqtt")
set(COREMQTT_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(NIMBLE_DIR "${NET_DIR}/nimble")
set(NIMBLE_EXTERN_PORT_DIR "${NET_DIR}/port/nimble")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/third_party/os/freertos")
set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/GCC/${FREERTOS_PORT}")

file(GLOB COREMQTT_SRCS
    "${COREMQTT_DIR}/source/*.c"
)

set(COREMQTT_LIB_SRCS
    ${COREMQTT_SRCS}
    "${COREMQTT_PORT_DIR}/src/mqtt_ble_transport.c"
    "${COREMQTT_PORT_DIR}/src/mqtt_gatt.c"
    "${COREMQTT_PORT_DIR}/src/mqtt_user.c"
)

add_library(coremqtt_lib OBJECT ${COREMQTT_LIB_SRCS})

# select FreeRTOSConfig.h settings for the target
if(ENABLE_TARGET_CHIP)
    target_compile_definitions(coremqtt_lib PRIVATE "${ENABLE_TARGET_CHIP}")
endif()

target_include_directories(coremqtt_lib PUBLIC
    "${FREERTOS_DIR}/include"
    "${FREERTOS_PORT_DIR}"
    "${COREMQTT_DIR}/source/include"
    "${COREMQTT_DIR}/source/interface"
    "${COMMON_INC_DIR}"
    "${COMMON_INC_DIR}/os/freertos" # FreeRTOSConfig.h
    "${COMMON_INC_DIR}/diagnostic/logging"
    "${COREMQTT_PORT_DIR}/include"
    "${NIMBLE_EXTERN_PORT_DIR}/include"
    "${NIMBLE_DIR}/nimble/include"
    "${NIMBLE_DIR}/nimble/host/include"
    "${NIMBLE_DIR}/nimble/host/services/gatt/include"
    "${NIMBLE_DIR}/nimble/transport/include"
    "${NIMBLE_DIR}/porting/nimble/include"
    "${NIMBLE_DIR}/porting/npl/freertos/include"    
)

target_compile_options(coremqtt_lib PRIVATE
    -ffreestanding
    -nostdlib
    -fno-builtin
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-missing-field-initializers
    ${OFLAGS}
)
