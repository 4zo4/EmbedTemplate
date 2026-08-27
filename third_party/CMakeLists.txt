find_program(PEAKRDL_BIN peakrdl)

if(NOT PEAKRDL_BIN)
    if(EXISTS "$ENV{HOME}/.local/bin/peakrdl")
        set(PEAKRDL_BIN "$ENV{HOME}/.local/bin/peakrdl")
    endif()
endif()

if(NOT PEAKRDL_BIN)
    message(WARNING "PeakRDL tool not found! "
                    "New register headers will not be auto-generated. "
                    "You must install peakrdl if you add new rdl files.")
endif()

if(PEAKRDL_BIN)
    set(PEAKRDL_PLUGIN_PATH "${CMAKE_SOURCE_DIR}/tools/peakrdl/src")
    file(GLOB RDL_FILES "${CMAKE_SOURCE_DIR}/hw/rdl/*.rdl")

    foreach(RDL_PATH ${RDL_FILES})
        get_filename_component(BLOCK_NAME ${RDL_PATH} NAME_WE)

        set(HDR_OUT "${CMAKE_SOURCE_DIR}/src/drivers/include/regs/${BLOCK_NAME}_regs.h")

        add_custom_command(
            OUTPUT "${HDR_OUT}"
            COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${PEAKRDL_PLUGIN_PATH}"
                    "${PEAKRDL_BIN}" c-header "${RDL_PATH}" -o "${HDR_OUT}" --custom --bitfields ltoh --std gnu23
            DEPENDS "${RDL_PATH}"
            COMMENT "PeakRDL: Syncing ${BLOCK_NAME}_regs.h"
            VERBATIM
        )

        list(APPEND ALL_GEN_HEADERS "${HDR_OUT}")
    endforeach()

    add_custom_target(generated_regs DEPENDS ${ALL_GEN_HEADERS})
else()
    add_custom_target(generated_regs COMMENT "Skipping register header generation")
endif()
