#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include "app_config.h"
#include "display_ui.h"
#include "llm_narration.h"
#include "media_store.h"
#include "screendeck_version.h"
#include "weather.h"
#include "web_portal.h"

namespace {
constexpr char AP_PASSWORD[] = "screen1234";
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 18000;

bool provisioningMode = false;
String accessPointSsid;

const char* uiText(const char* chinese, const char* english) {
  return appConfig.language() == InterfaceLanguage::Chinese ? chinese
                                                            : english;
}

String deviceSuffix() {
  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X",
           static_cast<uint16_t>(chipId & 0xFFFF));
  return String(suffix);
}

bool connectSavedWifi() {
  Preferences preferences;
  preferences.begin("wifi", true);
  const String ssid = preferences.getString("ssid", "");
  const String password = preferences.getString("password", "");
  preferences.end();
  if (ssid.isEmpty()) {
    return false;
  }

  displayShowBootMessage(uiText("正在连接", "Connecting"), ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("screendeck");
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    displayLoop();
    delay(5);
  }
  return WiFi.status() == WL_CONNECTED;
}

void startProvisioning() {
  provisioningMode = true;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  accessPointSsid = "ScreenDeck-" + deviceSuffix();
  WiFi.softAP(accessPointSsid.c_str(), AP_PASSWORD);
  delay(100);

  const String address = "http://" + WiFi.softAPIP().toString() + "/";
  webPortal.begin(true);
  displayShowProvisioning(accessPointSsid, AP_PASSWORD, address);

  Serial.println("[wifi] provisioning mode");
  Serial.printf("[wifi] AP: %s\n", accessPointSsid.c_str());
  Serial.printf("[wifi] password: %s\n", AP_PASSWORD);
  Serial.printf("[wifi] setup: %s\n", address.c_str());
}

void startOnline() {
  provisioningMode = false;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  if (!MDNS.begin("screendeck")) {
    Serial.println("[mdns] failed to start");
  } else {
    MDNS.addService("http", "tcp", 80);
  }
  webPortal.begin(false);
  weatherBegin();
  llmNarrationBegin();
  displayShowContent(0);
  Serial.printf("[wifi] connected to %s\n", WiFi.SSID().c_str());
  Serial.printf("[wifi] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.println("[wifi] mDNS: http://screendeck.local/");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("================================");
  Serial.println(" ScreenDeck firmware starting");
  Serial.println("================================");
  Serial.printf("[system] version: %s (%s)\n", SCREENDECK_VERSION,
                SCREENDECK_CHANNEL);
  Serial.printf("[system] built: %s\n", SCREENDECK_BUILD_TIME);
  Serial.printf("[system] flash: %u MB\n", ESP.getFlashChipSize() / 1048576);
  Serial.printf("[system] PSRAM: %u MB\n", ESP.getPsramSize() / 1048576);

  if (!psramFound()) {
    Serial.println("[fatal] OPI PSRAM was not detected");
    while (true) {
      delay(1000);
    }
  }

  if (!appConfig.begin()) {
    Serial.println("[fatal] content storage initialization failed");
    while (true) {
      delay(1000);
    }
  }

  if (!displayBegin()) {
    Serial.println("[fatal] display initialization failed");
    while (true) {
      delay(1000);
    }
  }
  displaySetBrightness(appConfig.brightness());
  displayShowBootMessage("ScreenDeck",
                         uiText("正在启动本地内容服务",
                                "Starting local content server"));

  // The TF slot shares its clock and data lines with the panel's command bus,
  // so it can only be probed once the ST7701 initialization sequence is done.
  mediaStoreMountSd();

  if (connectSavedWifi()) {
    startOnline();
  } else {
    startProvisioning();
  }
}

void loop() {
  displayLoop();
  llmNarrationLoop();
  if (!displaySlideInProgress()) {
    uint32_t narrationPageId = 0;
    String narrationPath;
    String narrationText;
    if (llmNarrationTakeResult(narrationPageId, narrationPath,
                               narrationText)) {
      displayBeginStorageWrite(false);
      const bool saved = appConfig.setImageNarration(
          narrationPageId, narrationPath, narrationText);
      const bool obsolete = !appConfig.imageNarrationTargetExists(
          narrationPageId, narrationPath);
      if (saved || obsolete) {
        llmNarrationAcknowledgeResult();
      }
      displayEndStorageWrite();
      if (saved && appConfig.imageNarrationEnabled()) {
        displayMarkContentDirty();
      } else if (!saved) {
        Serial.println(
            obsolete ? "[narration] result discarded because the image page changed"
                     : "[narration] save failed; retaining result for local retry");
      }
    }
  }
  weatherLoop(appConfig.showWeather());
  if (weatherTakeDisplayUpdate()) {
    displayMarkContentDirty();
  }
  if (!llmNarrationMediaReadInProgress() &&
      mediaStoreTakeSdRemovalDetected()) {
    // The failing LVGL/VFS callback has returned before this event is
    // consumed. Close its decoder and file handles before invalidating the
    // FatFS mount; tearing SD down inside the callback would race the worker.
    displayReleaseMedia();
    mediaStoreUnmountSd();
    displayMarkContentDirty();
  }
  if (!llmNarrationMediaReadInProgress() && displayTakeSdRescanRequest()) {
    mediaStoreUnmountSd();
    mediaStoreMountSd();
    displayMarkContentDirty();
  }
  // The panel worker remains independent while a missing card is probed. The
  // media store rate-limits failed attempts, so an inserted card appears on
  // both the device and web status without requiring a manual rescan.
  if (!mediaStoreSdMounted() && mediaStoreEnsureSdMounted()) {
    displayMarkContentDirty();
  }
  if (displayTakeWifiResetRequest() &&
      !webPortal.clearWifiAndRequestRestart()) {
    Serial.println("[wifi] unable to clear saved settings; restart cancelled");
    displayReportWifiResetFailure();
  }
  if (!displaySlideInProgress()) {
    webPortal.loop();
  }

  if (webPortal.restartRequested()) {
    displayShowBootMessage(uiText("正在重启", "Restarting"),
                           uiText("正在应用网络设置",
                                  "Applying network settings"));
    for (int i = 0; i < 25; ++i) {
      displayLoop();
      delay(10);
    }
    ESP.restart();
  }

  delay(3);
}
