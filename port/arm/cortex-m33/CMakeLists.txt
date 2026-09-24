set(ARCH_WIDTH 32 PARENT_SCOPE)
set(TARGET_CHIP_FLAGS
    -mcpu=cortex-m33
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv5-sp-d16
    -fno-builtin-printf
    -fno-builtin-puts
)
string(JOIN " " CPU_FLAGS ${TARGET_CHIP_FLAGS})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}" PARENT_SCOPE)
if(ENABLE_SECURE)
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cortex-m33.ns.ld")
    # Enable the Windowed Watchdog for secure build
    set(EXTRA_FLAGS "ENABLE_SECURE" "ENABLE_WWDG" PARENT_SCOPE)
else() # single-flat-image
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cortex-m33.ld")
endif()
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${LINKER_SCRIPT} -Wl,--no-warn-rwx-segments" PARENT_SCOPE)
set(EXTRA_LIBS "-lgcc" PARENT_SCOPE)
set(FREERTOS_PORT "ARM_CM33_NTZ/non_secure" PARENT_SCOPE)
set(CORTEX_M_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cortex-m)
set(TARGET_CHIP_SOURCES
    ${CORTEX_M_DIR}/common.c
    ${CORTEX_M_DIR}/startup.c
    ${CORTEX_M_DIR}/time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port.c
)
set(TARGET_CHIP_SOURCES ${TARGET_CHIP_SOURCES} PARENT_SCOPE)
set(TARGET_CHIP_INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CORTEX_M_DIR}
    PARENT_SCOPE
)
if(ENABLE_SECURE)
    add_executable(secure_boot ${CMAKE_CURRENT_SOURCE_DIR}/init_secure.c)
    target_include_directories(secure_boot PRIVATE
        "${CMAKE_SOURCE_DIR}/common/include"
        "${CMAKE_SOURCE_DIR}/common/include/arch"
    )
    target_compile_options(secure_boot PRIVATE ${TARGET_CHIP_FLAGS} -mcmse -Wall -Wextra -g -O2)
    target_link_options(secure_boot PRIVATE
        ${TARGET_CHIP_FLAGS}
        -mcmse
        "-T${CMAKE_CURRENT_SOURCE_DIR}/cortex-m33.sec.ld"
        "-Wl,--gc-sections"
        "-Wl,--no-warn-rwx-segments"
        "-nostartfiles"
    )
    target_link_libraries(secure_boot PRIVATE gcc)
    set_target_properties(secure_boot PROPERTIES SUFFIX ".elf")
    set_target_properties(secure_boot PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/../../"
    )
    add_custom_command(TARGET secure_boot POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:secure_boot> $<TARGET_FILE_DIR:secure_boot>/secure_boot.bin
        COMMENT "[CMake] Extracting flat secure bootloader binary: secure_boot.bin"
        VERBATIM
    )
endif()
