#pragma once

#include <Arduino.h>

bool displayBegin();
void displaySetBrightness(uint8_t percent);
bool displayGetImageSize(const String& path, uint16_t& width,
                         uint16_t& height);
void displayShowBootMessage(const String& title, const String& detail);
void displayShowProvisioning(const String& ssid, const String& password,
                             const String& address);
void displayShowContent(size_t pageIndex = 0);
void displayMarkContentDirty();
void displayMarkSettingsDirty();
// Closes every open media handle and shows a screen that owns none. Call
// before mounting or unmounting a backing store.
void displayReleaseMedia();
// Pauses the LVGL worker for the duration of a flash write. Long writes (image
// uploads) also gate the backlight so a stalled RGB scanout cannot be seen;
// short ones stay lit, which is what keeps settings changes flash-free.
void displayBeginStorageWrite(bool blankBacklight = true);
void displayEndStorageWrite();
void displayLoop();
bool displayTakeSdRescanRequest();
bool displayTakeWifiResetRequest();
void displayReportWifiResetFailure();
size_t displayCurrentPage();
