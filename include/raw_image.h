#pragma once

#include <Arduino.h>

constexpr uint16_t RAW_IMAGE_WIDTH = 480;
constexpr uint16_t RAW_IMAGE_HEIGHT = 480;
constexpr size_t RAW_IMAGE_HEADER_BYTES = 8;
constexpr size_t RAW_IMAGE_PIXEL_BYTES =
    static_cast<size_t>(RAW_IMAGE_WIDTH) * RAW_IMAGE_HEIGHT * 2;
constexpr size_t RAW_IMAGE_FILE_BYTES =
    RAW_IMAGE_HEADER_BYTES + RAW_IMAGE_PIXEL_BYTES;
constexpr char RAW_IMAGE_EXTENSION[] = ".rgb565";

bool rawImageInspect(const String& path, uint16_t& width, uint16_t& height);
bool rawImageLoadPixels(const String& path, uint8_t* destination,
                        size_t capacity);
