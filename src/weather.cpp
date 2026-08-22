#include "weather.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include "app_config.h"

namespace {
constexpr char IP_LOCATION_URL[] =
    "https://ipwho.is/?fields=success,latitude,longitude,city,region,country";
constexpr char OPEN_METEO_URL[] =
    "https://api.open-meteo.com/v1/forecast";
constexpr uint32_t WEATHER_REFRESH_INTERVAL_MS = 30U * 60U * 1000U;
constexpr uint32_t WEATHER_RETRY_INTERVAL_MS = 15U * 1000U;
constexpr uint32_t HTTP_TIMEOUT_MS = 8000;
constexpr uint32_t WEATHER_TASK_STACK_BYTES = 12288;
constexpr size_t MAX_HTTP_RESPONSE_BYTES = 4096;
constexpr size_t LOCATION_FIELD_BYTES = 48;

portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;
WeatherSnapshot latestSnapshot;
bool snapshotAvailable = false;
bool displayUpdatePending = false;
bool fetchRunning = false;
bool fetchAttempted = false;
uint32_t lastAttemptAt = 0;
uint32_t lastSuccessAt = 0;

bool locationAvailable = false;
bool cachedLocationUsesChinese = false;
double cachedLatitude = 0.0;
double cachedLongitude = 0.0;
char cachedCity[LOCATION_FIELD_BYTES] = "";
char cachedRegion[LOCATION_FIELD_BYTES] = "";
char cachedCountry[LOCATION_FIELD_BYTES] = "";

struct HttpResponse {
  String body;
  bool overflow = false;
};

esp_err_t handleHttpEvent(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }
  HttpResponse* response = static_cast<HttpResponse*>(event->user_data);
  if (!response ||
      response->body.length() + static_cast<size_t>(event->data_len) >
          MAX_HTTP_RESPONSE_BYTES) {
    if (response) {
      response->overflow = true;
    }
    return ESP_FAIL;
  }
  response->body.concat(static_cast<const char*>(event->data),
                        event->data_len);
  return ESP_OK;
}

bool getJson(const String& url, DynamicJsonDocument& document) {
  HttpResponse response;
  response.body.reserve(2048);
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.event_handler = handleHttpEvent;
  config.user_data = &response;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }

  const esp_err_t requestResult = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (requestResult != ESP_OK || status != 200 || response.overflow) {
    Serial.printf("[weather] GET failed (%s, %d): %s\n",
                  esp_err_to_name(requestResult), status, url.c_str());
    return false;
  }
  const DeserializationError error = deserializeJson(document, response.body);
  if (error) {
    Serial.printf("[weather] JSON parse failed: %s\n", error.c_str());
    return false;
  }
  return true;
}

void copyLocationField(char* destination, size_t capacity, JsonVariant value) {
  if (!destination || capacity == 0) {
    return;
  }
  destination[0] = '\0';
  const char* text = value.as<const char*>();
  if (!text || text[0] == '\0') {
    return;
  }
  snprintf(destination, capacity, "%s", text);
}

bool locationFieldEqual(const char* left, const char* right) {
  return left && right && left[0] != '\0' && strcmp(left, right) == 0;
}

void formatLocationLine(char* destination, size_t capacity, const char* city,
                        const char* region, const char* country,
                        double latitude, double longitude, bool chinese) {
  if (!destination || capacity == 0) {
    return;
  }
  destination[0] = '\0';
  const char* separator = chinese ? "，" : ", ";
  const char* primary =
      city && city[0] ? city : (region && region[0] ? region : nullptr);
  const char* secondary = nullptr;
  if (primary) {
    if (country && country[0] && !locationFieldEqual(country, primary)) {
      secondary = country;
    } else if (region && region[0] && !locationFieldEqual(region, primary)) {
      secondary = region;
    }
  } else if (country && country[0]) {
    primary = country;
  }

  if (primary) {
    if (secondary) {
      snprintf(destination, capacity, "%s%s%s", primary, separator, secondary);
    } else {
      snprintf(destination, capacity, "%s", primary);
    }
    return;
  }

  snprintf(destination, capacity, "%.2f %c, %.2f %c", std::fabs(latitude),
           latitude >= 0.0 ? 'N' : 'S', std::fabs(longitude),
           longitude >= 0.0 ? 'E' : 'W');
}

bool fetchLocation(double& latitude, double& longitude) {
  const bool wantChinese =
      appConfig.language() == InterfaceLanguage::Chinese;
  if (locationAvailable && cachedLocationUsesChinese == wantChinese) {
    latitude = cachedLatitude;
    longitude = cachedLongitude;
    return true;
  }

  String url = IP_LOCATION_URL;
  if (wantChinese) {
    url += "&lang=zh-CN";
  }

  DynamicJsonDocument document(2048);
  if (!getJson(url, document) || !document["success"].as<bool>() ||
      document["latitude"].isNull() || document["longitude"].isNull()) {
    if (locationAvailable) {
      latitude = cachedLatitude;
      longitude = cachedLongitude;
      Serial.println("[weather] IP location name refresh failed; keeping coordinates");
      return true;
    }
    Serial.println("[weather] IP location unavailable");
    return false;
  }

  latitude = document["latitude"].as<double>();
  longitude = document["longitude"].as<double>();
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
      longitude > 180.0) {
    Serial.println("[weather] IP location returned invalid coordinates");
    return false;
  }
  cachedLatitude = latitude;
  cachedLongitude = longitude;
  copyLocationField(cachedCity, sizeof(cachedCity), document["city"]);
  copyLocationField(cachedRegion, sizeof(cachedRegion), document["region"]);
  copyLocationField(cachedCountry, sizeof(cachedCountry), document["country"]);
  cachedLocationUsesChinese = wantChinese;
  locationAvailable = true;
  Serial.printf("[weather] IP location %.4f, %.4f %s\n", latitude, longitude,
                cachedCity[0] ? cachedCity : "(no city)");
  return true;
}

bool fetchWeather(WeatherSnapshot& snapshot) {
  double latitude = 0.0;
  double longitude = 0.0;
  if (!fetchLocation(latitude, longitude)) {
    return false;
  }

  String url = OPEN_METEO_URL;
  url += "?latitude=";
  url += String(latitude, 6);
  url += "&longitude=";
  url += String(longitude, 6);
  url += "&current=temperature_2m,weather_code,is_day";
  url += "&temperature_unit=celsius&timezone=auto";

  DynamicJsonDocument document(2048);
  if (!getJson(url, document)) {
    return false;
  }
  JsonObject current = document["current"].as<JsonObject>();
  if (current.isNull() || current["temperature_2m"].isNull() ||
      current["weather_code"].isNull() || current["is_day"].isNull()) {
    Serial.println("[weather] Open-Meteo response has no current weather");
    return false;
  }

  const double temperature = current["temperature_2m"].as<double>();
  const int code = current["weather_code"].as<int>();
  if (!std::isfinite(temperature) || temperature < -100.0 ||
      temperature > 100.0 || code < 0 || code > UINT8_MAX) {
    Serial.println("[weather] Open-Meteo returned invalid current weather");
    return false;
  }
  snapshot.temperatureCelsius = static_cast<int16_t>(lround(temperature));
  snapshot.weatherCode = static_cast<uint8_t>(code);
  snapshot.isDay = current["is_day"].as<int>() == 1;
  snapshot.locationUsesChinese = cachedLocationUsesChinese;
  formatLocationLine(snapshot.location, sizeof(snapshot.location), cachedCity,
                     cachedRegion, cachedCountry, latitude, longitude,
                     snapshot.locationUsesChinese);
  return true;
}

void weatherFetchTask(void*) {
  WeatherSnapshot snapshot;
  const bool ok = WiFi.status() == WL_CONNECTED && fetchWeather(snapshot);
  const uint32_t finishedAt = millis();

  portENTER_CRITICAL(&weatherMux);
  if (ok) {
    latestSnapshot = snapshot;
    snapshotAvailable = true;
    displayUpdatePending = true;
    lastSuccessAt = finishedAt;
  }
  fetchRunning = false;
  portEXIT_CRITICAL(&weatherMux);

  if (ok) {
    Serial.printf("[weather] %d C, WMO %u, %s, %s\n",
                  static_cast<int>(snapshot.temperatureCelsius),
                  static_cast<unsigned>(snapshot.weatherCode),
                  snapshot.isDay ? "day" : "night",
                  snapshot.location[0] ? snapshot.location : "(no location)");
  } else {
    Serial.println("[weather] update failed; retrying later");
  }
  vTaskDelete(nullptr);
}
}  // namespace

void weatherBegin() {
  portENTER_CRITICAL(&weatherMux);
  fetchRunning = false;
  fetchAttempted = false;
  lastAttemptAt = 0;
  lastSuccessAt = 0;
  portEXIT_CRITICAL(&weatherMux);
}

void weatherLoop(bool enabled) {
  if (!enabled || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  bool shouldFetch = false;
  portENTER_CRITICAL(&weatherMux);
  if (!fetchRunning) {
    const bool retryReady =
        !fetchAttempted || now - lastAttemptAt >= WEATHER_RETRY_INTERVAL_MS;
    const bool languageStale =
        snapshotAvailable &&
        latestSnapshot.locationUsesChinese !=
            (appConfig.language() == InterfaceLanguage::Chinese);
    const bool stale = !snapshotAvailable || languageStale ||
                       now - lastSuccessAt >= WEATHER_REFRESH_INTERVAL_MS;
    shouldFetch = retryReady && stale;
    if (shouldFetch) {
      fetchRunning = true;
      fetchAttempted = true;
      lastAttemptAt = now;
    }
  }
  portEXIT_CRITICAL(&weatherMux);

  if (!shouldFetch) {
    return;
  }
  if (xTaskCreatePinnedToCore(weatherFetchTask, "weather", 
                              WEATHER_TASK_STACK_BYTES, nullptr, 1, nullptr,
                              0) != pdPASS) {
    portENTER_CRITICAL(&weatherMux);
    fetchRunning = false;
    portEXIT_CRITICAL(&weatherMux);
    Serial.println("[weather] unable to start update task");
  }
}

bool weatherGetSnapshot(WeatherSnapshot& snapshot) {
  portENTER_CRITICAL(&weatherMux);
  const bool available = snapshotAvailable;
  if (available) {
    snapshot = latestSnapshot;
  }
  portEXIT_CRITICAL(&weatherMux);
  return available;
}

bool weatherTakeDisplayUpdate() {
  portENTER_CRITICAL(&weatherMux);
  const bool pending = displayUpdatePending;
  displayUpdatePending = false;
  portEXIT_CRITICAL(&weatherMux);
  return pending;
}
