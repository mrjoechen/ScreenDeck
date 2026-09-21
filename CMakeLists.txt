cmake_minimum_required(VERSION 3.16)

# Override LVGL 8's built-in-only default-font choice for every component.
# Disable its selected built-in too, keeping legacy font glyphs out of firmware.
add_compile_options("-include${CMAKE_SOURCE_DIR}/include/ui_lvgl_font_config.h")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(screendeck)
