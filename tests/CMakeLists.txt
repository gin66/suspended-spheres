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
set(PATTERN_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/pattern_core")
set(TMC_UART_BITS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/tmc_uart_bits")
set(HOMING_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/homing_core")
set(RELEASE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/release_core")
set(FLEET_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/fleet_core")
set(PANEL_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/panel_core")
set(NET_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/net_core")

add_library(motion_core STATIC
    "${MOTION_CORE_DIR}/src/motion_core.cpp"
)
target_include_directories(motion_core PUBLIC
    "${MOTION_CORE_DIR}/include"
)

add_library(pattern_core STATIC
    "${PATTERN_CORE_DIR}/src/bake.cpp"
    "${PATTERN_CORE_DIR}/src/hbp.cpp"
    "${PATTERN_CORE_DIR}/src/json.cpp"
    "${PATTERN_CORE_DIR}/src/limits.cpp"
    "${PATTERN_CORE_DIR}/src/live.cpp"
)
target_include_directories(pattern_core PUBLIC
    "${PATTERN_CORE_DIR}/include"
)
target_link_libraries(pattern_core PUBLIC motion_core)

add_library(tmc_uart_bits STATIC
    "${TMC_UART_BITS_DIR}/src/tmc_uart_bits.cpp"
)
target_include_directories(tmc_uart_bits PUBLIC
    "${TMC_UART_BITS_DIR}/include"
)

add_library(homing_core STATIC
    "${HOMING_CORE_DIR}/src/homing_core.cpp"
)
target_include_directories(homing_core PUBLIC
    "${HOMING_CORE_DIR}/include"
)

add_library(release_core STATIC
    "${RELEASE_CORE_DIR}/src/release_core.cpp"
)
target_include_directories(release_core PUBLIC
    "${RELEASE_CORE_DIR}/include"
)

add_library(panel_core STATIC
    "${PANEL_CORE_DIR}/src/panel_core.cpp"
)
target_link_libraries(panel_core PUBLIC release_core)
target_include_directories(panel_core PUBLIC
    "${PANEL_CORE_DIR}/include"
)

add_library(net_core STATIC
    "${NET_CORE_DIR}/src/net_core.cpp"
)
target_link_libraries(net_core PUBLIC release_core panel_core)
target_include_directories(net_core PUBLIC
    "${NET_CORE_DIR}/include"
)

add_library(fleet_core STATIC
    "${FLEET_CORE_DIR}/src/fleet_core.cpp"
)
target_link_libraries(fleet_core PUBLIC release_core motion_core)
target_include_directories(fleet_core PUBLIC
    "${FLEET_CORE_DIR}/include"
)

# One host executable.  test_runner.cpp is C++17; abi_c.c is the C caller
# that proves H4 (unmangled names, matching integer widths).
add_executable(motion_core_tests
    test_list.cpp
    test_runner.cpp
    test_identifiers.cpp
    test_payloads.cpp
    test_protocol.cpp
    test_bus.cpp
    test_playback.cpp
    test_steps.cpp
    test_clock.cpp
    test_node.cpp
    test_commission.cpp
    test_pattern.cpp
    test_live.cpp
    test_tmc_uart_bits.cpp
    test_homing_core.cpp
    test_release_r0.cpp
    test_release_core.cpp
    test_release_pack.cpp
    test_fleet_core.cpp
    test_panel_core.cpp
    test_ota_core.cpp
    test_workflow.cpp
    test_net_core.cpp
    abi_c.c
)

target_link_libraries(motion_core_tests PRIVATE motion_core pattern_core
    tmc_uart_bits homing_core release_core fleet_core panel_core net_core)
target_include_directories(motion_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Release packer: writes a release directory, never publishes.
add_executable(release_pack
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/release_pack.cpp"
)
target_link_libraries(release_pack PRIVATE release_core)

# Headless authoring tool: bake one file, write its report, optionally export.
add_executable(pattern_check
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/pattern_check.cpp"
)
target_link_libraries(pattern_check PRIVATE pattern_core)

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
target_compile_definitions(motion_core_tests PRIVATE
    RELEASE_PACK_BIN="$<TARGET_FILE:release_pack>"
    ESP32GUI_PLATFORMIO_INI="${CMAKE_CURRENT_SOURCE_DIR}/../esp32gui/platformio.ini"
    ESP32GUI_RELEASE_GATE_H="${CMAKE_CURRENT_SOURCE_DIR}/../esp32gui/include/release_gate.h"
    RELEASE_WORKFLOW_YML="${CMAKE_CURRENT_SOURCE_DIR}/../.github/workflows/release.yml")
add_dependencies(motion_core_tests release_pack)
