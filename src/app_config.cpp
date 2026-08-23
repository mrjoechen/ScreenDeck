#include "app_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "media_store.h"

namespace {
constexpr char CONFIG_PATH[] = "/config.json";

const char* pageTypeName(PageType type) {
  return type == PageType::Image ? "image" : "text";
}
}  // namespace

AppConfig appConfig;

bool AppConfig::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[storage] LittleFS mount failed");
    return false;
  }
  if (!LittleFS.exists("/img")) {
    LittleFS.mkdir("/img");
  }
  return load();
}

bool AppConfig::load() {
  brightness_ = 80;
  language_ = InterfaceLanguage::Chinese;
  pageTransitionStyle_ = PageTransitionStyle::FadeThroughBlack;
  timezoneOffsetMinutes_ = 480;
  showDateTime_ = false;
  showWeather_ = false;
  screenOffEnabled_ = false;
  screenOffStartMinutes_ = 1320;
  screenOffEndMinutes_ = 420;
  pageCount_ = 0;
  bool configChanged = false;

  if (!LittleFS.exists(CONFIG_PATH)) {
    return save();
  }

  File file = LittleFS.open(CONFIG_PATH, FILE_READ);
  if (!file) {
    return false;
  }

  DynamicJsonDocument doc(49152);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.printf("[storage] config parse failed: %s\n", error.c_str());
    brightness_ = 80;
    pageCount_ = 0;
    return save();
  }

  brightness_ = constrain(doc["brightness"] | 80, 5, 100);
  const String language = doc["language"] | "zh";
  language_ = language == "en" ? InterfaceLanguage::English
                                : InterfaceLanguage::Chinese;
  const String pageTransition = doc["pageTransition"] | "fade";
  pageTransitionStyle_ = pageTransition == "slide"
                             ? PageTransitionStyle::SlideHorizontal
                             : PageTransitionStyle::FadeThroughBlack;
  timezoneOffsetMinutes_ =
      constrain(doc["timezoneOffsetMinutes"] | 480, -720, 840);
  showDateTime_ = doc["showDateTime"] | false;
  showWeather_ = doc["showWeather"] | false;
  screenOffEnabled_ = doc["screenOffEnabled"] | false;
  screenOffStartMinutes_ =
      constrain(doc["screenOffStartMinutes"] | 1320, 0, 1439);
  screenOffEndMinutes_ =
      constrain(doc["screenOffEndMinutes"] | 420, 0, 1439);
  JsonArray pages = doc["pages"].as<JsonArray>();
  for (JsonObject item : pages) {
    if (pageCount_ >= MAX_CONTENT_PAGES) {
      break;
    }

    ContentPage& page = pages_[pageCount_];
    page.id = item["id"] | static_cast<uint32_t>(pageCount_ + 1);
    const String type = item["type"] | "text";
    page.type = type == "image" ? PageType::Image : PageType::Text;
    page.text = String(static_cast<const char*>(item["text"] | ""));
    page.imagePath = String(static_cast<const char*>(item["path"] | ""));
    page.background = item["background"] | 0x111111;
    page.foreground = item["foreground"] | 0xF5F0E8;

    if (page.type == PageType::Image && !mediaIsSdPath(page.imagePath) &&
        page.imagePath.endsWith(".jpeg") &&
        LittleFS.exists(page.imagePath)) {
      const String jpgPath =
          page.imagePath.substring(0, page.imagePath.length() - 5) + ".jpg";
      if (LittleFS.exists(jpgPath) ||
          LittleFS.rename(page.imagePath, jpgPath)) {
        page.imagePath = jpgPath;
        configChanged = true;
        Serial.printf("[storage] migrated JPEG path to %s\n",
                      page.imagePath.c_str());
      }
    }

    // Pages backed by the TF card are kept even when the card is missing: the
    // config is loaded before the card can be mounted, and an unplugged card
    // must not silently erase the playlist.
    if (page.type == PageType::Image && !mediaIsSdPath(page.imagePath) &&
        !LittleFS.exists(page.imagePath)) {
      Serial.printf("[storage] skipping missing image: %s\n",
                    page.imagePath.c_str());
      continue;
    }
    ++pageCount_;
  }

  Serial.printf("[storage] loaded %u content pages\n",
                static_cast<unsigned>(pageCount_));
  return !configChanged || save();
}

bool AppConfig::save() const {
  DynamicJsonDocument doc(49152);
  doc["version"] = 1;
  doc["brightness"] = brightness_;
  doc["language"] = languageCode();
  doc["pageTransition"] = pageTransitionStyleCode();
  doc["timezoneOffsetMinutes"] = timezoneOffsetMinutes_;
  doc["showDateTime"] = showDateTime_;
  doc["showWeather"] = showWeather_;
  doc["screenOffEnabled"] = screenOffEnabled_;
  doc["screenOffStartMinutes"] = screenOffStartMinutes_;
  doc["screenOffEndMinutes"] = screenOffEndMinutes_;
  JsonArray pages = doc.createNestedArray("pages");

  for (size_t i = 0; i < pageCount_; ++i) {
    const ContentPage& page = pages_[i];
    JsonObject item = pages.createNestedObject();
    item["id"] = page.id;
    item["type"] = pageTypeName(page.type);
    item["text"] = page.text;
    item["path"] = page.imagePath;
    item["background"] = page.background;
    item["foreground"] = page.foreground;
  }

  File file = LittleFS.open(CONFIG_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("[storage] cannot open config for writing");
    return false;
  }
  const bool ok = serializeJson(doc, file) > 0;
  file.close();
  return ok;
}

void AppConfig::setBrightness(uint8_t percent) {
  brightness_ = constrain(percent, 5, 100);
}

void AppConfig::setTimezoneOffsetMinutes(int16_t minutes) {
  timezoneOffsetMinutes_ = constrain(minutes, -720, 840);
}

void AppConfig::setScreenOffWindow(uint16_t startMinutes,
                                   uint16_t endMinutes) {
  screenOffStartMinutes_ = min<uint16_t>(startMinutes, 1439);
  screenOffEndMinutes_ = min<uint16_t>(endMinutes, 1439);
}

bool AppConfig::addText(const String& text, uint32_t background,
                        uint32_t foreground) {
  if (pageCount_ >= MAX_CONTENT_PAGES || text.isEmpty() ||
      text.length() > 1536) {
    return false;
  }
  ContentPage& page = pages_[pageCount_++];
  page = ContentPage{};
  page.id = nextId();
  page.type = PageType::Text;
  page.text = text;
  page.background = background & 0xFFFFFF;
  page.foreground = foreground & 0xFFFFFF;
  if (save()) {
    return true;
  }
  --pageCount_;
  return false;
}

bool AppConfig::addImage(const String& path) {
  if (pageCount_ >= MAX_CONTENT_PAGES || path.isEmpty() ||
      !mediaExists(path)) {
    return false;
  }
  ContentPage& page = pages_[pageCount_++];
  page = ContentPage{};
  page.id = nextId();
  page.type = PageType::Image;
  page.imagePath = path;
  page.background = 0x050505;
  if (save()) {
    return true;
  }
  --pageCount_;
  return false;
}

bool AppConfig::removePage(uint32_t id) {
  const int index = findPage(id);
  if (index < 0) {
    return false;
  }

  const String imageToDelete =
      pages_[index].type == PageType::Image ? pages_[index].imagePath : "";
  for (size_t i = index; i + 1 < pageCount_; ++i) {
    pages_[i] = pages_[i + 1];
  }
  --pageCount_;

  if (!imageToDelete.isEmpty()) {
    bool stillReferenced = false;
    for (size_t i = 0; i < pageCount_; ++i) {
      stillReferenced |= pages_[i].imagePath == imageToDelete;
    }
    if (!stillReferenced) {
      // The media store deletes internal uploads and ScreenDeck-managed SD
      // files, while preserving arbitrary files elsewhere on the card.
      mediaRemove(imageToDelete);
    }
  }
  return save();
}

bool AppConfig::movePage(uint32_t id, int direction) {
  const int index = findPage(id);
  const int target = index + (direction < 0 ? -1 : 1);
  if (index < 0 || target < 0 || target >= static_cast<int>(pageCount_)) {
    return false;
  }
  ContentPage tmp = pages_[index];
  pages_[index] = pages_[target];
  pages_[target] = tmp;
  return save();
}

String AppConfig::toJson() const {
  DynamicJsonDocument doc(49152);
  doc["brightness"] = brightness_;
  JsonArray pages = doc.createNestedArray("pages");
  for (size_t i = 0; i < pageCount_; ++i) {
    const ContentPage& page = pages_[i];
    JsonObject item = pages.createNestedObject();
    item["id"] = page.id;
    item["type"] = pageTypeName(page.type);
    item["text"] = page.text;
    item["path"] = page.imagePath;
    item["background"] = page.background;
    item["foreground"] = page.foreground;
  }
  String output;
  serializeJson(doc, output);
  return output;
}

int AppConfig::findPage(uint32_t id) const {
  for (size_t i = 0; i < pageCount_; ++i) {
    if (pages_[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

uint32_t AppConfig::nextId() const {
  uint32_t highest = 0;
  for (size_t i = 0; i < pageCount_; ++i) {
    highest = max(highest, pages_[i].id);
  }
  return highest + 1;
}
