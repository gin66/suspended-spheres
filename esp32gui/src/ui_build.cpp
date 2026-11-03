/*
 * Compile the board-agnostic LVGL screens as part of the firmware image.
 *
 * PlatformIO's ESP-IDF framework ignores build_src_filter, so ui/ cannot be
 * added as a separate source directory. Including ui.cpp here keeps the
 * productive screens in esp32gui/ui/ where the host harness also builds them.
 */
#include "../ui/ui.cpp"
