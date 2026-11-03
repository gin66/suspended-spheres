cmake_minimum_required(VERSION 3.16)
project(suspended_spheres_host C CXX)

# Host-only harness.  The shared core lives in ../lib/motion_core and is
# compiled here for the PC; rp2040/ is firmware-only and PlatformIO compiles
# the very same core sources.  Invoke as:
#
#     cmake -S tests -B build && cmake --build build
#     ./build/motion_core_tests
#
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MOTION_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/motion_core")

add_library(motion_core STATIC
    "${MOTION_CORE_DIR}/src/motion_core.cpp"
)
target_include_directories(motion_core PUBLIC
    "${MOTION_CORE_DIR}/include"
)

# One host executable.  test_runner.cpp is C++17; abi_c.c is the C caller
# that proves H4 (unmangled names, matching integer widths).
add_executable(motion_core_tests
    test_list.cpp
    test_runner.cpp
    test_identifiers.cpp
    test_payloads.cpp
    test_bus.cpp
    test_playback.cpp
    test_steps.cpp
    abi_c.c
)

target_link_libraries(motion_core_tests PRIVATE motion_core)
target_include_directories(motion_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# H3: forbid float and double in the core directory.  The scan covers the
# public headers and the sources; comments are stripped first.
set(CORE_FILES "${MOTION_CORE_DIR}/include/motion_core.h")
file(GLOB CORE_SOURCES "${MOTION_CORE_DIR}/src/*.cpp")
list(APPEND CORE_FILES ${CORE_SOURCES})

foreach(_src ${CORE_FILES})
    file(READ "${_src}" _content)
    string(REGEX REPLACE "//[^\n]*" "" _content "${_content}")
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" _content "${_content}")
    if(_content MATCHES "(^|[^A-Za-z0-9_])float([^A-Za-z0-9_]|$)"
       OR _content MATCHES "(^|[^A-Za-z0-9_])double([^A-Za-z0-9_]|$)")
        message(FATAL_ERROR
            "H3 violation: floating-point type found in core file ${_src}. "
            "Conversion to floating point is allowed only in the renderer "
            "and in test printers.")
    endif()
endforeach()

# The runtime H3 test only passes if this configure-time scan ran.
target_compile_definitions(motion_core_tests PRIVATE MC_H3_CHECK_RAN=1)
