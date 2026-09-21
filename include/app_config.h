#pragma once

#include <Arduino.h>

constexpr size_t MAX_CONTENT_PAGES = 16;
// Capacities include the trailing NUL used by the narration worker's fixed
// snapshots, so valid persisted values must be strictly shorter than these.
constexpr size_t MAX_LLM_BASE_URL_BYTES = 384;
constexpr size_t MAX_LLM_API_KEY_BYTES = 512;
constexpr size_t MAX_LLM_MODEL_BYTES = 128;
constexpr size_t MAX_LLM_NARRATION_PROMPT_CODEPOINTS = 2048;
constexpr size_t MAX_LLM_NARRATION_PROMPT_BYTES =
    4 * MAX_LLM_NARRATION_PROMPT_CODEPOINTS + 1;
constexpr char DEFAULT_LLM_NARRATION_PROMPT[] =
    "准确、自然地概括图片主体、环境与氛围。";

// These validators are shared by persistence, web handlers, and worker entry
// points so browser-only limits cannot become part of the data contract.
bool validLlmNarrationPrompt(const String& value);
bool validImageNarration(const String& value);

enum class PageType : uint8_t {
  Text,
  Image,
};

enum class InterfaceLanguage : uint8_t {
  Chinese,
  English,
};

enum class PageTransitionStyle : uint8_t {
  FadeThroughBlack,
  SlideHorizontal,
};

struct ContentPage {
  uint32_t id = 0;
  PageType type = PageType::Text;
  String text;
  String imagePath;
  String narration;
  uint32_t background = 0x111111;
  uint32_t foreground = 0xF5F0E8;
};

class AppConfig {
 public:
  bool begin();
  bool load();
  bool save() const;

  uint8_t brightness() const { return brightness_; }
  void setBrightness(uint8_t percent);

  InterfaceLanguage language() const { return language_; }
  const char* languageCode() const {
    return language_ == InterfaceLanguage::English ? "en" : "zh";
  }
  void setLanguage(InterfaceLanguage language) { language_ = language; }

  PageTransitionStyle pageTransitionStyle() const { return pageTransitionStyle_; }
  const char* pageTransitionStyleCode() const {
    return pageTransitionStyle_ == PageTransitionStyle::SlideHorizontal
               ? "slide"
               : "fade";
  }
  void setPageTransitionStyle(PageTransitionStyle style) {
    pageTransitionStyle_ = style;
  }

  int16_t timezoneOffsetMinutes() const { return timezoneOffsetMinutes_; }
  void setTimezoneOffsetMinutes(int16_t minutes);
  bool showDateTime() const { return showDateTime_; }
  void setShowDateTime(bool enabled) { showDateTime_ = enabled; }
  bool showWeather() const { return showWeather_; }
  void setShowWeather(bool enabled) { showWeather_ = enabled; }
  bool imageNarrationEnabled() const { return imageNarrationEnabled_; }
  void setImageNarrationEnabled(bool enabled) {
    imageNarrationEnabled_ = enabled;
  }
  bool persistedImageNarrationEnabled() const {
    return persistedImageNarrationEnabled_;
  }
  void restorePersistedImageNarrationEnabled() {
    imageNarrationEnabled_ = persistedImageNarrationEnabled_;
  }
  bool screenOffEnabled() const { return screenOffEnabled_; }
  void setScreenOffEnabled(bool enabled) { screenOffEnabled_ = enabled; }
  uint16_t screenOffStartMinutes() const { return screenOffStartMinutes_; }
  uint16_t screenOffEndMinutes() const { return screenOffEndMinutes_; }
  void setScreenOffWindow(uint16_t startMinutes, uint16_t endMinutes);

  const String& llmBaseUrl() const { return llmBaseUrl_; }
  const String& llmApiKey() const { return llmApiKey_; }
  const String& llmModel() const { return llmModel_; }
  const String& llmNarrationPrompt() const { return llmNarrationPrompt_; }
  bool setLlmConfig(const String& baseUrl, const String& apiKey,
                    const String& model);
  bool setLlmNarrationPrompt(const String& narrationPrompt);
  void clearLlmConfig();
  bool llmConfigured() const {
    return !llmBaseUrl_.isEmpty() && !llmApiKey_.isEmpty() &&
           !llmModel_.isEmpty();
  }

  size_t pageCount() const { return pageCount_; }
  const ContentPage& page(size_t index) const { return pages_[index]; }

  bool addText(const String& text, uint32_t background, uint32_t foreground);
  bool addImage(const String& path);
  bool removePage(uint32_t id);
  bool movePage(uint32_t id, int direction);
  bool setImageNarration(uint32_t pageId, const String& expectedPath,
                        const String& narration);
  // Clears only image summaries; a failed atomic save restores the cache.
  bool clearImageNarrations();
  bool imageNarrationTargetExists(uint32_t pageId,
                                  const String& expectedPath) const;

  String toJson() const;

 private:
  int findPage(uint32_t id) const;
  uint32_t nextId() const;

  uint8_t brightness_ = 80;
  InterfaceLanguage language_ = InterfaceLanguage::Chinese;
  PageTransitionStyle pageTransitionStyle_ =
      PageTransitionStyle::FadeThroughBlack;
  int16_t timezoneOffsetMinutes_ = 480;
  bool showDateTime_ = false;
  bool showWeather_ = false;
  bool imageNarrationEnabled_ = true;
  mutable bool persistedImageNarrationEnabled_ = true;
  bool screenOffEnabled_ = false;
  uint16_t screenOffStartMinutes_ = 1320;
  uint16_t screenOffEndMinutes_ = 420;
  String llmBaseUrl_;
  String llmApiKey_;
  String llmModel_;
  String llmNarrationPrompt_ = DEFAULT_LLM_NARRATION_PROMPT;
  size_t pageCount_ = 0;
  ContentPage pages_[MAX_CONTENT_PAGES];
};

extern AppConfig appConfig;
