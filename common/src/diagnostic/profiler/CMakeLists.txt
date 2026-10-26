add_library(profiler_lib STATIC
    profiler.c
)

# Target-specific C++ options. This does not affect standard C compilation
target_compile_options(profiler_lib PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

target_include_directories(profiler_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/lib/libc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/diagnostic/profiler
)
