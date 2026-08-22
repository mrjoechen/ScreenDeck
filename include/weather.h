#pragma once

#include <Arduino.h>

struct WeatherSnapshot {
  int16_t temperatureCelsius = 0;
  uint8_t weatherCode = 0;
  bool isDay = true;
  bool locationUsesChinese = false;
  char location[64] = "";
};

// Weather requests run in a dedicated FreeRTOS task so HTTPS and DNS never
// stall image playback or the local controller. `weatherLoop` only schedules
// work and is safe to call on every pass through Arduino's loop().
void weatherBegin();
void weatherLoop(bool enabled);
bool weatherGetSnapshot(WeatherSnapshot& snapshot);
bool weatherTakeDisplayUpdate();
