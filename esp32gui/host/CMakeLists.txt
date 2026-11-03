cmake_minimum_required(VERSION 3.20)
project(esp32gui_host LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

# LVGL 9.2 is the firmware version. The default is the package already
# downloaded for the house panel; override with
#   cmake -S . -B build -DLVGL_ROOT_DIR=/path/to/lvgl
set(_pio_lvgl
    "${CMAKE_SOURCE_DIR}/../../../ESP32_projects/ESP32/28_hauszentrale/.pio/libdeps/esp32usb/lvgl")
set(_extern_lvgl
    "${CMAKE_SOURCE_DIR}/../../../ESP32_projects/ESP32/extern/lvgl")
if(EXISTS "${_pio_lvgl}/lvgl.h")
  set(_default_lvgl "${_pio_lvgl}")
else()
  set(_default_lvgl "${_extern_lvgl}")
endif()
set(LVGL_ROOT_DIR "${_default_lvgl}" CACHE PATH "LVGL source tree")

if(NOT EXISTS "${LVGL_ROOT_DIR}/lvgl.h")
  message(FATAL_ERROR
    "LVGL not found at ${LVGL_ROOT_DIR}. Pass -DLVGL_ROOT_DIR=/path/to/lvgl.")
endif()

find_package(SDL2 REQUIRED)

# Tell LVGL's CMake where our host lv_conf.h lives. Without this the library
# target cannot see it and every LVGL source fails at lv_conf_internal.h.
set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE PATH "Host lv_conf.h")
set(LV_BUILD_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h")

add_subdirectory(${LVGL_ROOT_DIR} lvgl_build EXCLUDE_FROM_ALL)
target_compile_definitions(lvgl PRIVATE
  LV_SDL_INCLUDE_PATH="SDL2/SDL.h"
)
target_include_directories(lvgl PRIVATE ${SDL2_INCLUDE_DIRS})
target_include_directories(lvgl PUBLIC ${LVGL_ROOT_DIR}/src/libs/lodepng)

add_executable(esp32gui_host
  src/main.cpp
  ${CMAKE_SOURCE_DIR}/../ui/ui.cpp
)

target_include_directories(esp32gui_host PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${LVGL_ROOT_DIR}/src/libs/lodepng
  ${CMAKE_SOURCE_DIR}/../ui
)

target_link_libraries(esp32gui_host PRIVATE lvgl)

if(APPLE)
  target_link_options(esp32gui_host PRIVATE -Wl,-no_warn_duplicate_libraries)
endif()
