#pragma once

#include <Arduino.h>

class WebPortal {
 public:
  void begin(bool provisioningMode);
  void loop();
  bool clearWifiAndRequestRestart();
  bool restartRequested() const;

 private:
  bool provisioning_ = false;
  bool restartRequested_ = false;
  uint32_t restartAt_ = 0;
};

extern WebPortal webPortal;
