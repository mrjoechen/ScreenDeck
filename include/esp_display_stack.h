#pragma once

#include <stdint.h>

// Initializes the complete Espressif display path: ST7701 command IO,
// esp_lcd RGB triple buffering, GT911 touch, LVGL and esp_lvgl_adapter.
// The LVGL worker is intentionally started separately so the application can
// register its filesystem and input callbacks first.
bool espDisplayStackBegin();
bool espDisplayStackStart();

bool espDisplayStackReadTouch(uint16_t& x, uint16_t& y);

bool espDisplayStackLock(int32_t timeoutMs = -1);
void espDisplayStackUnlock();

bool espDisplayStackPause();
bool espDisplayStackResume();
bool espDisplayStackRefreshNow();

