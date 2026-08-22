file(GLOB_RECURSE SCREENDECK_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

idf_component_register(
    SRCS ${SCREENDECK_SOURCES}
    INCLUDE_DIRS "../include"
    REQUIRES
        esp_lcd
        esp_lvgl_adapter
        esp_lcd_st7701
        esp_lcd_panel_io_additions
        esp_lcd_touch_gt911
        lvgl
        esp_http_client
        mbedtls)

# LVGL's font converter emits `#include "lvgl/lvgl.h"` unless this is defined.
# That path only exists when LVGL sits in a directory named `lvgl`; as an
# ESP-IDF managed component the header is reachable as plain `lvgl.h`.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
