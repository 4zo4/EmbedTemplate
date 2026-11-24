if(NOT (BARE_METAL AND RTOS))
    return()
endif()

if(ENABLE_NET)

# --- BLE (nimble_lib) ---
set(ENABLE_BLE TRUE)
add_subdirectory(port/nimble)

# --- MQTT (coremqtt_lib) ---
set(ENABLE_MQTT TRUE)
add_subdirectory(port/coremqtt)

endif() # ENABLE_NET
