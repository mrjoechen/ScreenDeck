#include "app_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "media_store.h"
#include "ui_font.h"

namespace {
constexpr char CONFIG_PATH[] = "/config.json";
constexpr char CONFIG_TEMP_PATH[] = "/config.json.tmp";
constexpr size_t MAX_IMAGE_NARRATION_CODEPOINTS = 64;

const char* pageTypeName(PageType type) {
  return type == PageType::Image ? "image" : "text";
}

bool validLlmField(const String& value, size_t capacity) {
  return !value.isEmpty() && value.length() < capacity;
}

bool decodeUtf8CodePoint(const String& value, size_t& offset,
                         uint32_t& codePoint) {
  if (offset >= value.length()) {
    return false;
  }

  const uint8_t lead = static_cast<uint8_t>(value[offset]);
  if (lead <= 0x7F) {
    codePoint = lead;
    ++offset;
    return true;
  }

  size_t sequenceBytes = 0;
  uint32_t decoded = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    sequenceBytes = 2;
    decoded = lead & 0x1F;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    sequenceBytes = 3;
    decoded = lead & 0x0F;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    sequenceBytes = 4;
    decoded = lead & 0x07;
  } else {
    return false;
  }
  if (offset + sequenceBytes > value.length()) {
    return false;
  }

  const uint8_t second = static_cast<uint8_t>(value[offset + 1]);
  if ((second & 0xC0) != 0x80 ||
      (lead == 0xE0 && second < 0xA0) ||
      (lead == 0xED && second > 0x9F) ||
      (lead == 0xF0 && second < 0x90) ||
      (lead == 0xF4 && second > 0x8F)) {
    return false;
  }
  decoded = (decoded << 6) | (second & 0x3F);
  for (size_t index = 2; index < sequenceBytes; ++index) {
    const uint8_t continuation =
        static_cast<uint8_t>(value[offset + index]);
    if ((continuation & 0xC0) != 0x80) {
      return false;
    }
    decoded = (decoded << 6) | (continuation & 0x3F);
  }
  offset += sequenceBytes;
  codePoint = decoded;
  return true;
}

bool containsMarkdownDecoration(const String& value) {
  if (value.indexOf('`') >= 0 || value.indexOf("![") >= 0 ||
      value.indexOf("](") >= 0 || value.indexOf("**") >= 0 ||
      value.indexOf("__") >= 0 || value.indexOf("~~") >= 0) {
    return true;
  }
  constexpr char PAIRED_MARKERS[] = {'*', '_', '~'};
  for (const char marker : PAIRED_MARKERS) {
    const int first = value.indexOf(marker);
    if (first >= 0 && value.indexOf(marker, first + 1) >= 0) {
      return true;
    }
  }

  size_t lineStart = 0;
  while (lineStart < value.length()) {
    size_t lineEnd = lineStart;
    while (lineEnd < value.length() && value[lineEnd] != '\n' &&
           value[lineEnd] != '\r') {
      ++lineEnd;
    }
    size_t cursor = lineStart;
    while (cursor < lineEnd &&
           (value[cursor] == ' ' || value[cursor] == '\t')) {
      ++cursor;
    }
    if (cursor < lineEnd) {
      const char first = value[cursor];
      const bool hasFollowingSpace =
          cursor + 1 < lineEnd &&
          (value[cursor + 1] == ' ' || value[cursor + 1] == '\t');
      if (first == '#' || first == '>' ||
          ((first == '-' || first == '+' || first == '*') &&
           hasFollowingSpace)) {
        return true;
      }

      size_t digitsEnd = cursor;
      while (digitsEnd < lineEnd && value[digitsEnd] >= '0' &&
             value[digitsEnd] <= '9') {
        ++digitsEnd;
      }
      if (digitsEnd > cursor && digitsEnd + 1 < lineEnd &&
          (value[digitsEnd] == '.' || value[digitsEnd] == ')') &&
          (value[digitsEnd + 1] == ' ' || value[digitsEnd + 1] == '\t')) {
        return true;
      }
    }
    lineStart = lineEnd;
    while (lineStart < value.length() &&
           (value[lineStart] == '\n' || value[lineStart] == '\r')) {
      ++lineStart;
    }
  }
  return false;
}

bool isEmojiCodePoint(uint32_t codePoint) {
  return (codePoint >= 0x1F000 && codePoint <= 0x1FAFF) ||
         (codePoint >= 0x2600 && codePoint <= 0x27BF) ||
         (codePoint >= 0x2300 && codePoint <= 0x23FF) ||
         (codePoint >= 0x2B00 && codePoint <= 0x2BFF) ||
         (codePoint >= 0xE0020 && codePoint <= 0xE007F) ||
         codePoint == 0x00A9 || codePoint == 0x00AE ||
         codePoint == 0x200D || codePoint == 0x203C ||
         codePoint == 0x2049 || codePoint == 0x20E3 ||
         codePoint == 0x2122 || codePoint == 0x2139 ||
         codePoint == 0x3030 || codePoint == 0x303D ||
         codePoint == 0x3297 || codePoint == 0x3299 ||
         codePoint == 0xFE0F;
}

bool isCjkCodePoint(uint32_t codePoint) {
  return (codePoint >= 0x3400 && codePoint <= 0x4DBF) ||
         (codePoint >= 0x4E00 && codePoint <= 0x9FFF) ||
         (codePoint >= 0xF900 && codePoint <= 0xFAFF) ||
         (codePoint >= 0x20000 && codePoint <= 0x323AF);
}
}  // namespace

bool validLlmNarrationPrompt(const String& value) {
  if (value.isEmpty() || value.length() >= MAX_LLM_NARRATION_PROMPT_BYTES) {
    return false;
  }
  size_t offset = 0;
  size_t codepoints = 0;
  while (offset < value.length()) {
    uint32_t codePoint = 0;
    if (!decodeUtf8CodePoint(value, offset, codePoint) || codePoint == 0 ||
        ++codepoints > MAX_LLM_NARRATION_PROMPT_CODEPOINTS) {
      return false;
    }
  }
  return codepoints > 0;
}

bool validImageNarration(const String& value) {
  if (value.isEmpty() || containsMarkdownDecoration(value)) {
    return false;
  }
  size_t offset = 0;
  size_t codepoints = 0;
  bool hasCjk = false;
  while (offset < value.length()) {
    uint32_t codePoint = 0;
    if (!decodeUtf8CodePoint(value, offset, codePoint) || codePoint == 0 ||
        isEmojiCodePoint(codePoint) ||
        ++codepoints > MAX_IMAGE_NARRATION_CODEPOINTS) {
      return false;
    }
    const bool normalizedWhitespace =
        codePoint == '\r' || codePoint == '\n' || codePoint == '\t';
    if (!normalizedWhitespace &&
        !uiFontMiSansSupportsCodePoint(codePoint)) {
      return false;
    }
    hasCjk |= isCjkCodePoint(codePoint);
  }
  return hasCjk;
}

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
  imageNarrationEnabled_ = true;
  persistedImageNarrationEnabled_ = true;
  screenOffEnabled_ = false;
  screenOffStartMinutes_ = 1320;
  screenOffEndMinutes_ = 420;
  llmBaseUrl_ = "";
  llmApiKey_ = "";
  llmModel_ = "";
  llmNarrationPrompt_ = DEFAULT_LLM_NARRATION_PROMPT;
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
  imageNarrationEnabled_ = doc["imageNarrationEnabled"] | true;
  persistedImageNarrationEnabled_ = imageNarrationEnabled_;
  screenOffEnabled_ = doc["screenOffEnabled"] | false;
  screenOffStartMinutes_ =
      constrain(doc["screenOffStartMinutes"] | 1320, 0, 1439);
  screenOffEndMinutes_ =
      constrain(doc["screenOffEndMinutes"] | 420, 0, 1439);
  llmBaseUrl_ = String(static_cast<const char*>(doc["llmBaseUrl"] | ""));
  llmApiKey_ = String(static_cast<const char*>(doc["llmApiKey"] | ""));
  llmModel_ = String(static_cast<const char*>(doc["llmModel"] | ""));
  if ((!llmBaseUrl_.isEmpty() &&
       !validLlmField(llmBaseUrl_, MAX_LLM_BASE_URL_BYTES)) ||
      (!llmApiKey_.isEmpty() &&
       !validLlmField(llmApiKey_, MAX_LLM_API_KEY_BYTES)) ||
      (!llmModel_.isEmpty() &&
       !validLlmField(llmModel_, MAX_LLM_MODEL_BYTES))) {
    clearLlmConfig();
    configChanged = true;
  }
  if (!doc.containsKey("llmNarrationPrompt")) {
    llmNarrationPrompt_ = DEFAULT_LLM_NARRATION_PROMPT;
  } else {
    llmNarrationPrompt_ =
        String(static_cast<const char*>(doc["llmNarrationPrompt"] | ""));
    llmNarrationPrompt_.trim();
    if (!validLlmNarrationPrompt(llmNarrationPrompt_)) {
      llmNarrationPrompt_ = DEFAULT_LLM_NARRATION_PROMPT;
      configChanged = true;
    }
  }
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
    page.narration =
        String(static_cast<const char*>(item["narration"] | ""));
    if (!page.narration.isEmpty() &&
        !validImageNarration(page.narration)) {
      page.narration = "";
      configChanged = true;
    }
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

  // Narration belongs to the image asset, not to one playlist occurrence.
  // Heal older/partially written configs so duplicate pages never schedule
  // another API request when one copy already has a cached result.
  for (size_t i = 0; i < pageCount_; ++i) {
    if (pages_[i].type != PageType::Image ||
        !pages_[i].narration.isEmpty()) {
      continue;
    }
    for (size_t j = 0; j < pageCount_; ++j) {
      if (pages_[j].type == PageType::Image &&
          pages_[j].imagePath == pages_[i].imagePath &&
          !pages_[j].narration.isEmpty()) {
        pages_[i].narration = pages_[j].narration;
        configChanged = true;
        break;
      }
    }
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
  doc["imageNarrationEnabled"] = imageNarrationEnabled_;
  doc["screenOffEnabled"] = screenOffEnabled_;
  doc["screenOffStartMinutes"] = screenOffStartMinutes_;
  doc["screenOffEndMinutes"] = screenOffEndMinutes_;
  doc["llmBaseUrl"] = llmBaseUrl_;
  doc["llmApiKey"] = llmApiKey_;
  doc["llmModel"] = llmModel_;
  doc["llmNarrationPrompt"] = llmNarrationPrompt_;
  JsonArray pages = doc.createNestedArray("pages");

  for (size_t i = 0; i < pageCount_; ++i) {
    const ContentPage& page = pages_[i];
    JsonObject item = pages.createNestedObject();
    item["id"] = page.id;
    item["type"] = pageTypeName(page.type);
    item["text"] = page.text;
    item["path"] = page.imagePath;
    item["narration"] = page.narration;
    item["background"] = page.background;
    item["foreground"] = page.foreground;
  }

  if (doc.overflowed()) {
    Serial.println("[storage] config document overflowed");
    return false;
  }

  const size_t expectedBytes = measureJson(doc);
  LittleFS.remove(CONFIG_TEMP_PATH);
  File file = LittleFS.open(CONFIG_TEMP_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("[storage] cannot open temporary config for writing");
    return false;
  }
  const size_t writtenBytes = serializeJson(doc, file);
  file.flush();
  file.close();

  File check = LittleFS.open(CONFIG_TEMP_PATH, FILE_READ);
  const bool complete = expectedBytes > 0 &&
                        writtenBytes == expectedBytes && check &&
                        check.size() == expectedBytes;
  check.close();
  if (!complete) {
    LittleFS.remove(CONFIG_TEMP_PATH);
    Serial.printf("[storage] incomplete config write: %u/%u bytes\n",
                  static_cast<unsigned>(writtenBytes),
                  static_cast<unsigned>(expectedBytes));
    return false;
  }
  if (!LittleFS.rename(CONFIG_TEMP_PATH, CONFIG_PATH)) {
    LittleFS.remove(CONFIG_TEMP_PATH);
    Serial.println("[storage] cannot atomically replace config");
    return false;
  }
  persistedImageNarrationEnabled_ = imageNarrationEnabled_;
  return true;
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

bool AppConfig::setLlmConfig(const String& baseUrl, const String& apiKey,
                             const String& model) {
  String normalizedBaseUrl = baseUrl;
  String normalizedApiKey = apiKey;
  String normalizedModel = model;
  normalizedBaseUrl.trim();
  normalizedApiKey.trim();
  normalizedModel.trim();
  if (!validLlmField(normalizedBaseUrl, MAX_LLM_BASE_URL_BYTES) ||
      !validLlmField(normalizedApiKey, MAX_LLM_API_KEY_BYTES) ||
      !validLlmField(normalizedModel, MAX_LLM_MODEL_BYTES)) {
    return false;
  }
  llmBaseUrl_ = normalizedBaseUrl;
  llmApiKey_ = normalizedApiKey;
  llmModel_ = normalizedModel;
  return true;
}

bool AppConfig::setLlmNarrationPrompt(const String& narrationPrompt) {
  String normalizedPrompt = narrationPrompt;
  normalizedPrompt.trim();
  if (!validLlmNarrationPrompt(normalizedPrompt)) {
    return false;
  }
  llmNarrationPrompt_ = normalizedPrompt;
  return true;
}

void AppConfig::clearLlmConfig() {
  llmBaseUrl_ = "";
  llmApiKey_ = "";
  llmModel_ = "";
  llmNarrationPrompt_ = DEFAULT_LLM_NARRATION_PROMPT;
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
  String inheritedNarration;
  for (size_t i = 0; i < pageCount_; ++i) {
    if (pages_[i].type == PageType::Image &&
        pages_[i].imagePath == path && !pages_[i].narration.isEmpty()) {
      inheritedNarration = pages_[i].narration;
      break;
    }
  }

  ContentPage& page = pages_[pageCount_++];
  page = ContentPage{};
  page.id = nextId();
  page.type = PageType::Image;
  page.imagePath = path;
  page.narration = inheritedNarration;
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

bool AppConfig::imageNarrationTargetExists(
    uint32_t pageId, const String& expectedPath) const {
  const int target = findPage(pageId);
  if (expectedPath.isEmpty() ||
      (target >= 0 && (pages_[target].type != PageType::Image ||
                      pages_[target].imagePath != expectedPath))) {
    return false;
  }

  // The page that started the request can be deleted while the network call
  // is in flight. Keep the successful result when another playlist entry
  // still references that exact asset, but never attach it to a reused page
  // id whose path changed.
  if (target < 0) {
    bool pathStillReferenced = false;
    for (size_t i = 0; i < pageCount_; ++i) {
      if (pages_[i].type == PageType::Image &&
          pages_[i].imagePath == expectedPath) {
        pathStillReferenced = true;
        break;
      }
    }
    if (!pathStillReferenced) {
      return false;
    }
  }
  return true;
}

bool AppConfig::setImageNarration(uint32_t pageId,
                                  const String& expectedPath,
                                  const String& narration) {
  if (!imageNarrationTargetExists(pageId, expectedPath)) {
    return false;
  }

  String normalized = narration;
  normalized.trim();
  if (!validImageNarration(normalized)) {
    return false;
  }

  String previous[MAX_CONTENT_PAGES];
  bool matching[MAX_CONTENT_PAGES] = {};
  bool changed = false;
  for (size_t i = 0; i < pageCount_; ++i) {
    if (pages_[i].type == PageType::Image &&
        pages_[i].imagePath == expectedPath) {
      matching[i] = true;
      previous[i] = pages_[i].narration;
      if (pages_[i].narration != normalized) {
        pages_[i].narration = normalized;
        changed = true;
      }
    }
  }
  if (!changed) {
    return true;
  }
  if (save()) {
    return true;
  }
  for (size_t i = 0; i < pageCount_; ++i) {
    if (matching[i]) {
      pages_[i].narration = previous[i];
    }
  }
  return false;
}

bool AppConfig::clearImageNarrations() {
  String previous[MAX_CONTENT_PAGES];
  for (size_t i = 0; i < pageCount_; ++i) {
    if (pages_[i].type == PageType::Image) {
      previous[i] = pages_[i].narration;
      pages_[i].narration = "";
    }
  }
  if (save()) {
    return true;
  }
  for (size_t i = 0; i < pageCount_; ++i) {
    if (pages_[i].type == PageType::Image) {
      pages_[i].narration = previous[i];
    }
  }
  return false;
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
    item["narration"] = page.narration;
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
