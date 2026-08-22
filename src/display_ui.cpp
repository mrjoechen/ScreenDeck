#include "display_ui.h"

#include <cerrno>
#include <cstring>

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <lvgl.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "backlight_fade.h"
#include "esp_display_stack.h"
#include "media_store.h"
#include "raw_image.h"
#include "screendeck_version.h"
#include "ui_font.h"
#include "weather.h"
#include "weather_icons.h"

namespace {
constexpr int16_t SCREEN_WIDTH = 480;
constexpr int16_t SCREEN_HEIGHT = 480;
constexpr uint8_t BACKLIGHT_PIN = 38;
// This panel's backlight driver cannot follow a fast carrier: at 20 kHz any
// duty below full scale collapsed the LED string and the screen read as off,
// which made the web brightness slider look broken. Firmware that ships for
// this board (ESPHome, openHASP) drives GPIO 38 at 100-150 Hz, which dims
// smoothly.
constexpr uint32_t BACKLIGHT_PWM_HZ = 150;
constexpr uint8_t BACKLIGHT_PWM_BITS = 10;
constexpr uint32_t BACKLIGHT_PWM_MAX = (1U << BACKLIGHT_PWM_BITS) - 1U;
constexpr uint32_t SWIPE_THRESHOLD = 60;
constexpr uint32_t AUTO_ADVANCE_INTERVAL_MS = 5000;
// Animated pages get a longer dwell so a short loop can play out before
// playback moves on.
constexpr uint32_t ANIMATED_ADVANCE_INTERVAL_MS = 15000;
constexpr uint32_t SYSTEM_PAGE_IDLE_TIMEOUT_MS = 10000;
constexpr uint32_t SETTINGS_IDLE_TIMEOUT_MS = 60000;
// Bounds are expressed in "wall-clock epoch" seconds: Gregorian local fields
// interpreted as UTC before applying the configured fixed offset. This keeps
// the full local years 2020 through 2100 available in every timezone.
constexpr int64_t LOCAL_TIME_MIN_EPOCH = 1577836800LL;
constexpr int64_t LOCAL_TIME_MAX_EPOCH_EXCLUSIVE = 4133980800LL;
constexpr uint32_t CLOCK_REFRESH_INTERVAL_MS = 1000;
constexpr uint32_t PLAYBACK_CLOCK_MOVE_INTERVAL_MS = 60000;
// Opacity animation invalidates the full 480 x 480 frame, so keep the once-per-
// minute position change brief.
constexpr uint32_t PLAYBACK_CLOCK_FADE_IN_MS = 120;
constexpr uint8_t PLAYBACK_CLOCK_CORNER_COUNT = 4;
constexpr int16_t PLAYBACK_CLOCK_INSET = 16;
// A double tap inside a scheduled screen-off window wakes the panel. The wake
// then behaves like a phone's screen timeout: it survives as long as the user
// keeps touching, and only expires after this much idle time.
constexpr uint32_t TEMPORARY_WAKE_IDLE_MS = 30000;
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 600;
constexpr uint32_t TAP_MAX_DURATION_MS = 500;
constexpr int16_t TAP_MAX_MOVEMENT = 24;
constexpr uint32_t STORAGE_REVEAL_DELAY_MS = 60;
// On-screen settings buttons step a value per tap. Coalescing the taps into one
// commit keeps a burst of adjustments from writing flash a dozen times.
constexpr uint32_t SETTINGS_SAVE_DEBOUNCE_MS = 1200;
constexpr uint32_t SETTINGS_SAVE_RETRY_MS = 5000;
constexpr uint8_t SETTINGS_SAVE_MAX_RETRIES = 3;
constexpr size_t IMAGE_CACHE_SLOTS = 3;
// How long the current page must have been on screen before a neighbouring
// image may be read from flash, and the minimum gap between two such reads.
// Both stay well inside AUTO_ADVANCE_INTERVAL_MS so the next page is cached
// before playback needs it.
constexpr uint32_t IMAGE_PRELOAD_IDLE_DELAY_MS = 400;
constexpr uint32_t IMAGE_PRELOAD_MIN_INTERVAL_MS = 250;
constexpr uint32_t PAGE_FADE_OUT_MS = 600;
constexpr uint32_t PAGE_FADE_IN_MS = 1400;
// Keep the backlight fully off after forcing the new LVGL frame. At the panel's
// 56 Hz refresh rate, 50 ms covers almost three complete scanout frames.
constexpr uint32_t PAGE_FRAME_SETTLE_MS = 50;
constexpr uint32_t PAGE_FADE_STEP_MS = 12;
constexpr uint16_t EASING_SCALE = 1024;
lv_indev_drv_t inputDriver;
lv_fs_drv_t fsDriver;

size_t currentPage = 0;
bool provisioningScreen = false;
bool contentDirty = false;
bool playableContentUnavailable = false;
// The LVGL worker is paused for every flash write. Blanking the panel on top
// of that is reserved for the long ones (image uploads); a settings commit is
// short enough that gating the backlight only produced a visible black flash.
bool storageWriteActive = false;
bool storageBlankActive = false;
uint32_t backlightResumeAt = 0;
bool touchWasPressed = false;
bool touchBeganWhileBlanked = false;
uint32_t touchStartAt = 0;
int16_t touchStartX = 0;
int16_t touchStartY = 0;
int16_t lastTouchX = 0;
int16_t lastTouchY = 0;
int8_t pendingSwipe = 0;
bool pendingSystemPage = false;
uint32_t lastSwipeAt = 0;
uint32_t lastPageChangeAt = 0;

enum class PageTransitionPhase : uint8_t {
  Idle,
  FadingOut,
  WaitingForFrame,
  FadingIn,
};

enum class PlaybackClockCorner : uint8_t {
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight,
};

PageTransitionPhase pageTransitionPhase = PageTransitionPhase::Idle;
int8_t pageTransitionDirection = 0;
uint32_t pageTransitionStartedAt = 0;
uint32_t pageTransitionLastStepAt = 0;
uint8_t pageTransitionPercent = 100;
uint32_t lastTouchAt = 0;
size_t resumeContentPage = 1;
bool deviceSettingsScreen = false;
bool deviceSettingsDirty = false;
bool settingsSavePending = false;
uint32_t settingsSaveDueAt = 0;
uint8_t settingsSaveRetryCount = 0;
bool wifiResetRequested = false;
bool wifiResetInProgress = false;
bool sdRescanRequested = false;
bool scheduledScreenOff = false;
ScheduledBacklightFade scheduledBacklightFade;
uint32_t wakeOverrideUntil = 0;
uint32_t lastBlankedTapAt = 0;
uint32_t lastClockRefreshAt = 0;
PlaybackClockCorner playbackClockCorner = PlaybackClockCorner::BottomRight;
uint32_t playbackClockLastMoveAt = 0;
bool playbackClockMoveTimerStarted = false;
lv_obj_t* liveClockLabel = nullptr;
lv_obj_t* playbackClockCard = nullptr;
lv_obj_t* playbackDateShadowLabel = nullptr;
lv_obj_t* playbackDateLabel = nullptr;
lv_obj_t* playbackTimeShadowLabel = nullptr;
lv_obj_t* playbackTimeLabel = nullptr;
lv_obj_t* activeAnimation = nullptr;
lv_obj_t* brightnessValueLabel = nullptr;
lv_obj_t* editorYearRoller = nullptr;
lv_obj_t* editorMonthRoller = nullptr;
lv_obj_t* editorDayRoller = nullptr;
lv_obj_t* editorHourRoller = nullptr;
lv_obj_t* editorMinuteRoller = nullptr;
// Last text pushed into each clock label. The panel renders in TRIPLE_FULL
// anti-tearing mode, where every invalidation repaints all 480x480 pixels, so
// rewriting an unchanged label once a second kept the whole screen redrawing.
String liveClockText;
String playbackDateText;
String playbackTimeText;
bool touchPressed = false;
uint16_t touchX = 0;
uint16_t touchY = 0;

class LvglLockGuard {
 public:
  LvglLockGuard() : locked_(espDisplayStackLock()) {}
  ~LvglLockGuard() {
    if (locked_) {
      espDisplayStackUnlock();
    }
  }

  bool locked() const { return locked_; }

 private:
  bool locked_;
};

String provisioningSsid;
String provisioningPassword;
String provisioningAddress;

struct CachedImage {
  String path;
  // Resident PSRAM slot, allocated once at startup and reused for every image
  // that lands in this cache entry. Allocating and freeing 450 KB on each page
  // change fragmented the PSRAM heap and added the allocator walk to the swipe
  // critical path.
  uint8_t* pixels = nullptr;
  bool decoderOpen = false;
  lv_img_decoder_dsc_t decoder{};
  lv_img_dsc_t descriptor{};
  uint32_t lastUsed = 0;
};

CachedImage imageCache[IMAGE_CACHE_SLOTS];
String activeCachedImagePath;
uint32_t imageCacheClock = 0;
uint32_t lastPreloadAt = 0;

bool isImagePage(size_t pageIndex);
bool isPlayablePage(size_t pageIndex);
bool containsUtf8(const String& text);
bool containsCjk(const String& text);
bool containsEmoji(const String& text);
String normalizeEmojiText(const String& text);
void renderDeviceSettings();
void renderSystemPage();

// Screen swaps requested from an LVGL event callback. Rebuilding a screen from
// inside the callback would delete the screen that owns the button currently
// being dispatched, so the work is handed to displayLoop instead.
enum class ScreenRequest : uint8_t {
  None,
  DeviceSettings,
  SystemPage,
};

ScreenRequest pendingScreenRequest = ScreenRequest::None;

enum class SettingsSection : uint8_t {
  Display,
  Time,
  System,
};

enum class SettingsEditor : uint8_t {
  None,
  DateTime,
  ScreenOffStart,
  ScreenOffEnd,
  WifiReset,
};

struct DateTimeDraft {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
};

struct TimeOfDayDraft {
  uint8_t hour;
  uint8_t minute;
};

SettingsSection activeSettingsSection = SettingsSection::Display;
SettingsEditor activeSettingsEditor = SettingsEditor::None;
DateTimeDraft dateTimeDraft{2026, 1, 1, 12, 0};
TimeOfDayDraft timeOfDayDraft{0, 0};

constexpr int16_t TIMEZONE_OPTIONS[] = {
    -720, -690, -660, -630, -600, -570, -540, -510, -480,
    -450, -420, -390, -360, -330, -300, -270, -240, -210,
    -180, -150, -120, -90,  -60,  -30,  0,    30,   60,
    90,   120,  150,  180,  210,  240,  270,  300,  330,
    345,  360,  390,  420,  450,  480,  510,  540,  570,
    600,  630,  660,  690,  720,  750,  780,  810,  840,
};
constexpr size_t TIMEZONE_OPTION_COUNT =
    sizeof(TIMEZONE_OPTIONS) / sizeof(TIMEZONE_OPTIONS[0]);

enum class SettingsAction : uint8_t {
  Open,
  Back,
  ShowDisplay,
  ShowTime,
  ShowSystem,
  SetChinese,
  SetEnglish,
  TimezonePrevious,
  TimezoneNext,
  SyncTime,
  EditDateTime,
  ToggleClock,
  ToggleWeather,
  ToggleScreenOff,
  EditScreenOffStart,
  EditScreenOffEnd,
  CancelEditor,
  SaveDateTime,
  SaveTimeEditor,
  RescanSd,
  OpenWifiReset,
  ConfirmWifiReset,
};

bool useChineseUi() {
  return appConfig.language() == InterfaceLanguage::Chinese;
}

const char* uiText(const char* chinese, const char* english) {
  return useChineseUi() ? chinese : english;
}

String formatStorageSize(uint64_t bytes) {
  constexpr uint64_t KIB = 1024ULL;
  constexpr uint64_t MIB = 1024ULL * KIB;
  constexpr uint64_t GIB = 1024ULL * MIB;
  if (bytes >= GIB) {
    return String(static_cast<double>(bytes) / GIB, 1) + " GB";
  }
  if (bytes >= MIB) {
    return String(static_cast<double>(bytes) / MIB, 1) + " MB";
  }
  if (bytes >= KIB) {
    return String(static_cast<double>(bytes) / KIB, 1) + " KB";
  }
  return String(static_cast<unsigned long long>(bytes)) + " B";
}

bool getConfiguredLocalTime(struct tm& localTime) {
  const time_t epoch = time(nullptr);
  const int64_t localWallTime =
      static_cast<int64_t>(epoch) +
      static_cast<int64_t>(appConfig.timezoneOffsetMinutes()) * 60LL;
  if (localWallTime < LOCAL_TIME_MIN_EPOCH ||
      localWallTime >= LOCAL_TIME_MAX_EPOCH_EXCLUSIVE) {
    return false;
  }
  const time_t configuredEpoch = static_cast<time_t>(localWallTime);
  gmtime_r(&configuredEpoch, &localTime);
  return true;
}

String formatTimezoneOffset(int16_t offsetMinutes) {
  const char sign = offsetMinutes < 0 ? '-' : '+';
  const uint16_t absoluteMinutes = abs(offsetMinutes);
  char output[12];
  snprintf(output, sizeof(output), "UTC%c%02u:%02u", sign,
           absoluteMinutes / 60, absoluteMinutes % 60);
  return String(output);
}

String formatMinuteOfDay(uint16_t minuteOfDay) {
  char output[6];
  snprintf(output, sizeof(output), "%02u:%02u", minuteOfDay / 60,
           minuteOfDay % 60);
  return String(output);
}

bool screenOffWindowActive() {
  if (!appConfig.screenOffEnabled()) {
    return false;
  }
  struct tm localTime {};
  if (!getConfiguredLocalTime(localTime)) {
    return false;
  }
  const uint16_t nowMinutes = localTime.tm_hour * 60 + localTime.tm_min;
  const uint16_t start = appConfig.screenOffStartMinutes();
  const uint16_t end = appConfig.screenOffEndMinutes();
  if (start == end) {
    return false;
  }
  return start < end ? nowMinutes >= start && nowMinutes < end
                     : nowMinutes >= start || nowMinutes < end;
}

uint16_t easeOutCubic(uint16_t progress) {
  progress = min<uint16_t>(progress, EASING_SCALE);
  const uint64_t inverse = EASING_SCALE - progress;
  const uint64_t inverseCubed = inverse * inverse * inverse;
  return EASING_SCALE -
         static_cast<uint16_t>(inverseCubed /
                               (static_cast<uint64_t>(EASING_SCALE) *
                                EASING_SCALE));
}

uint16_t transitionProgress(uint32_t elapsed, uint32_t duration) {
  if (elapsed >= duration) {
    return EASING_SCALE;
  }
  return static_cast<uint16_t>(
      (static_cast<uint64_t>(elapsed) * EASING_SCALE) / duration);
}

uint8_t easedPercent(uint32_t elapsed, uint32_t duration) {
  const uint16_t eased = easeOutCubic(transitionProgress(elapsed, duration));
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(eased) * 100U + EASING_SCALE / 2U) /
      EASING_SCALE);
}

// The panel backlight used to be a bare on/off GPIO, so displaySetBrightness()
// could only ever drive it fully on and the web controller's brightness slider
// did nothing. LEDC gives it a real duty cycle.
void backlightBegin() {
  if (!ledcAttach(BACKLIGHT_PIN, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS)) {
    Serial.printf("[backlight] LEDC attach failed on GPIO %u\n",
                  static_cast<unsigned>(BACKLIGHT_PIN));
    return;
  }
  Serial.printf("[backlight] GPIO %u PWM at %u Hz, %u-bit\n",
                static_cast<unsigned>(BACKLIGHT_PIN),
                static_cast<unsigned>(BACKLIGHT_PWM_HZ),
                static_cast<unsigned>(BACKLIGHT_PWM_BITS));
  ledcWrite(BACKLIGHT_PIN, 0);
}

void backlightApply() {
  const bool forcedOff =
      storageBlankActive || backlightResumeAt != 0;
  const uint32_t configuredPercent = constrain(appConfig.brightness(), 5, 100);
  const uint32_t transitionPercent = scheduledBacklightFade.effectivePercent(
      scheduledScreenOff, pageTransitionPercent);
  const uint32_t percent =
      forcedOff ? 0 : (configuredPercent * transitionPercent) / 100U;
  const uint32_t duty = (BACKLIGHT_PWM_MAX * percent) / 100U;
  static uint32_t appliedDuty = UINT32_MAX;
  if (duty != appliedDuty) {
    appliedDuty = duty;
    if (forcedOff ||
        (!scheduledBacklightFade.active() &&
         pageTransitionPhase == PageTransitionPhase::Idle)) {
      Serial.printf("[backlight] %u%% -> duty %u/%u\n",
                    static_cast<unsigned>(percent),
                    static_cast<unsigned>(duty),
                    static_cast<unsigned>(BACKLIGHT_PWM_MAX));
    }
  }
  ledcWrite(BACKLIGHT_PIN, duty);
}

void cancelPageTransition() {
  pageTransitionPhase = PageTransitionPhase::Idle;
  pageTransitionDirection = 0;
  pageTransitionStartedAt = 0;
  pageTransitionLastStepAt = 0;
  pageTransitionPercent = 100;
  backlightApply();
}

void refreshScheduledBacklight() {
  const uint32_t now = millis();
  const bool wakeOverrideActive =
      wakeOverrideUntil != 0 &&
      static_cast<int32_t>(now - wakeOverrideUntil) < 0;
  if (!wakeOverrideActive) {
    wakeOverrideUntil = 0;
  }
  const bool shouldBeOff =
      screenOffWindowActive() && !wakeOverrideActive;
  if (shouldBeOff == scheduledScreenOff) {
    return;
  }
  if (!shouldBeOff) {
    scheduledScreenOff = false;
    scheduledBacklightFade.cancel();
    cancelPageTransition();
  } else {
    const bool alreadyBlank = storageBlankActive || backlightResumeAt != 0;
    const uint8_t visiblePercent = alreadyBlank ? 0 : pageTransitionPercent;
    scheduledBacklightFade.begin(now, visiblePercent);
    scheduledScreenOff = true;
    backlightApply();
  }
}

void registerBlankedTap(uint32_t releasedAt, int16_t dx, int16_t dy,
                        uint32_t pressDuration) {
  if (abs(dx) > TAP_MAX_MOVEMENT || abs(dy) > TAP_MAX_MOVEMENT ||
      pressDuration > TAP_MAX_DURATION_MS) {
    lastBlankedTapAt = 0;
    return;
  }

  if (lastBlankedTapAt != 0 &&
      releasedAt - lastBlankedTapAt <= DOUBLE_TAP_WINDOW_MS) {
    lastBlankedTapAt = 0;
    wakeOverrideUntil = releasedAt + TEMPORARY_WAKE_IDLE_MS;
    refreshScheduledBacklight();
    return;
  }
  lastBlankedTapAt = releasedAt;
}

// Keeps a temporary wake alive while the screen is being used. Without this the
// panel blanked mid-swipe, because the deadline was fixed at the moment of the
// waking double tap instead of tracking the last interaction.
void noteWakeActivity(uint32_t now) {
  if (wakeOverrideUntil == 0) {
    return;
  }
  wakeOverrideUntil = now + TEMPORARY_WAKE_IDLE_MS;
  if (wakeOverrideUntil == 0) {
    wakeOverrideUntil = 1;
  }
}

// Writing a label always invalidates it, and an invalidation on this panel
// costs a full-screen repaint. Every clock label therefore keeps its last
// value and only touches LVGL when the rendered text actually differs.
void applyClockLabel(lv_obj_t* label, String& cache, const String& text,
                     const lv_font_t* asciiFont,
                     lv_obj_t* shadowLabel = nullptr) {
  if (!label || cache == text) {
    return;
  }
  const lv_font_t* selectedFont =
      containsCjk(text) ? &ui_font_16_zh : asciiFont;
  if (shadowLabel) {
    lv_obj_set_style_text_font(shadowLabel, selectedFont, 0);
    lv_label_set_text(shadowLabel, text.c_str());
  }
  lv_obj_set_style_text_font(label, selectedFont, 0);
  lv_label_set_text(label, text.c_str());
  cache = text;
}

void updateLiveClock() {
  if (!liveClockLabel && !playbackDateLabel && !playbackTimeLabel &&
      !playbackDateShadowLabel && !playbackTimeShadowLabel) {
    return;
  }
  struct tm localTime {};
  if (!getConfiguredLocalTime(localTime)) {
    applyClockLabel(liveClockLabel, liveClockText,
                    uiText("等待校时", "Waiting for time"),
                    &lv_font_montserrat_20);
    applyClockLabel(playbackDateLabel, playbackDateText,
                    uiText("等待校时", "Time sync"), &lv_font_montserrat_24,
                    playbackDateShadowLabel);
    applyClockLabel(playbackTimeLabel, playbackTimeText, "--:--",
                    &lv_font_montserrat_48, playbackTimeShadowLabel);
    return;
  }

  // Minute resolution only: a seconds field would repaint the screen once a
  // second for no useful information.
  char output[32];
  strftime(output, sizeof(output), "%Y-%m-%d  %H:%M", &localTime);
  applyClockLabel(liveClockLabel, liveClockText, output,
                  &lv_font_montserrat_20);

  char date[16];
  strftime(date, sizeof(date), "%Y-%m-%d", &localTime);
  applyClockLabel(playbackDateLabel, playbackDateText, date,
                  &lv_font_montserrat_24, playbackDateShadowLabel);

  char clock[8];
  strftime(clock, sizeof(clock), "%H:%M", &localTime);
  applyClockLabel(playbackTimeLabel, playbackTimeText, clock,
                  &lv_font_montserrat_48, playbackTimeShadowLabel);
}

String withLeadingSlash(const char* path) {
  String normalized(path);
  if (!normalized.startsWith("/")) {
    normalized = "/" + normalized;
  }
  return normalized;
}

// Animated sources are streamed by their own decoder and never enter this
// cache, which only holds single decoded frames.
bool cacheableImagePath(const String& path) {
  return path.endsWith(RAW_IMAGE_EXTENSION) || path.endsWith(".png");
}

CachedImage* findCachedImage(const String& path) {
  for (CachedImage& entry : imageCache) {
    if (entry.path == path && entry.descriptor.data) {
      entry.lastUsed = ++imageCacheClock;
      return &entry;
    }
  }
  return nullptr;
}

// Probe without touching lastUsed: asking whether a neighbour is cached must
// not make it look freshly used, or the next eviction picks the wrong slot.
bool cachedImagePresent(const String& path) {
  for (const CachedImage& entry : imageCache) {
    if (entry.path == path && entry.descriptor.data) {
      return true;
    }
  }
  return false;
}

void clearCachedImage(CachedImage& entry) {
  if (entry.descriptor.data) {
    lv_img_cache_invalidate_src(&entry.descriptor);
  }
  if (entry.decoderOpen) {
    lv_img_decoder_close(&entry.decoder);
  }
  entry.path = "";
  entry.decoderOpen = false;
  entry.decoder = {};
  entry.descriptor = {};
  entry.lastUsed = 0;
  // entry.pixels stays: the slot is reused, not returned to the heap.
}

void clearMediaCache() {
  lv_img_cache_invalidate_src(nullptr);
  for (CachedImage& entry : imageCache) {
    clearCachedImage(entry);
  }
  activeCachedImagePath = "";
}

bool imageCacheBegin() {
  bool allocated = true;
  for (CachedImage& entry : imageCache) {
    if (entry.pixels) {
      continue;
    }
    entry.pixels = static_cast<uint8_t*>(heap_caps_malloc(
        RAW_IMAGE_PIXEL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!entry.pixels) {
      allocated = false;
    }
  }
  return allocated;
}

bool pageReferencesImage(const String& path) {
  for (size_t i = 0; i < appConfig.pageCount(); ++i) {
    const ContentPage& page = appConfig.page(i);
    if (page.type == PageType::Image && page.imagePath == path) {
      return true;
    }
  }
  return false;
}

// Content edits can delete a page and free its file while the cache still holds
// the decoded pixels under that path. Upload paths start with a millis() stamp
// that restarts at zero on every boot, so a later upload can land on the same
// path and would then be served from the stale slot.
void dropUnreferencedCachedImages() {
  for (CachedImage& entry : imageCache) {
    if (entry.path.isEmpty() || pageReferencesImage(entry.path)) {
      continue;
    }
    if (entry.path == activeCachedImagePath) {
      activeCachedImagePath = "";
    }
    clearCachedImage(entry);
  }
}

CachedImage* selectCacheEntry() {
  for (CachedImage& entry : imageCache) {
    if (entry.path.isEmpty()) {
      return &entry;
    }
  }

  CachedImage* selected = nullptr;
  for (CachedImage& entry : imageCache) {
    if (entry.path == activeCachedImagePath) {
      continue;
    }
    if (!selected || entry.lastUsed < selected->lastUsed) {
      selected = &entry;
    }
  }
  if (selected) {
    clearCachedImage(*selected);
  }
  return selected;
}

CachedImage* loadCachedImage(const String& path) {
  if (CachedImage* cached = findCachedImage(path)) {
    return cached;
  }
  if (!cacheableImagePath(path)) {
    return nullptr;
  }

  CachedImage* entry = selectCacheEntry();
  if (!entry) {
    return nullptr;
  }

  if (path.endsWith(RAW_IMAGE_EXTENSION)) {
    if (!entry->pixels) {
      imageCacheBegin();
    }
    if (!entry->pixels ||
        !rawImageLoadPixels(path, entry->pixels, RAW_IMAGE_PIXEL_BYTES)) {
      clearCachedImage(*entry);
      return nullptr;
    }
    entry->descriptor.header.always_zero = 0;
    entry->descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    entry->descriptor.header.w = RAW_IMAGE_WIDTH;
    entry->descriptor.header.h = RAW_IMAGE_HEIGHT;
    entry->descriptor.data_size = RAW_IMAGE_PIXEL_BYTES;
    entry->descriptor.data = entry->pixels;
  } else {
    const String source = "S:" + path;
    lv_img_decoder_dsc_t decoder{};
    if (lv_img_decoder_open(&decoder, source.c_str(), lv_color_white(), 0) !=
            LV_RES_OK ||
        !decoder.img_data || decoder.header.w <= 0 || decoder.header.h <= 0) {
      if (decoder.decoder) {
        lv_img_decoder_close(&decoder);
      }
      clearCachedImage(*entry);
      return nullptr;
    }
    entry->decoder = decoder;
    entry->decoderOpen = true;
    entry->descriptor.header = decoder.header;
    entry->descriptor.data_size = lv_img_buf_get_img_size(
        decoder.header.w, decoder.header.h,
        static_cast<lv_img_cf_t>(decoder.header.cf));
    entry->descriptor.data = decoder.img_data;
  }

  entry->path = path;
  entry->lastUsed = ++imageCacheClock;
  Serial.printf("[image-cache] loaded %s (%u bytes)\n", path.c_str(),
                static_cast<unsigned>(entry->descriptor.data_size));
  return entry;
}

// The page being rendered is loaded on demand by renderImagePage. Its two
// neighbours are only needed for the *next* swipe, so they are fetched later
// from displayLoop instead of on the critical path: a 450 KB LittleFS read plus
// the PSRAM writes it feeds used to run under the LVGL lock before the new
// screen was even built, which froze the worker and starved the RGB bounce
// buffer refill for the whole transition.
size_t nextImagePreloadPage(size_t centerPage) {
  const size_t pageCount = appConfig.pageCount();
  if (pageCount == 0) {
    return 0;
  }
  if (centerPage == 0 || centerPage > pageCount) {
    centerPage = min(resumeContentPage, pageCount);
  }
  const size_t neighbours[] = {centerPage >= pageCount ? 1 : centerPage + 1,
                               centerPage <= 1 ? pageCount : centerPage - 1};
  for (const size_t pageIndex : neighbours) {
    if (pageIndex == centerPage || !isImagePage(pageIndex)) {
      continue;
    }
    const String& path = appConfig.page(pageIndex - 1).imagePath;
    // A path that cannot be read -- a card that was pulled, a deleted file --
    // would otherwise be retried on every preload tick.
    if (cacheableImagePath(path) && !cachedImagePresent(path) &&
        mediaExists(path)) {
      return pageIndex;
    }
  }
  return 0;
}

bool preloadOneNeighbourImage(size_t centerPage) {
  const size_t pageIndex = nextImagePreloadPage(centerPage);
  if (pageIndex == 0) {
    return false;
  }
  loadCachedImage(appConfig.page(pageIndex - 1).imagePath);
  return true;
}

void updateTouchState() {
  touchPressed = espDisplayStackReadTouch(touchX, touchY);
}

void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  updateTouchState();
  if (touchPressed) {
    // This panel's mounted GT911 already reports coordinates in display
    // orientation. Inverting them here reverses horizontal swipe direction.
    const int16_t x = constrain(touchX, 0, SCREEN_WIDTH - 1);
    const int16_t y = constrain(touchY, 0, SCREEN_HEIGHT - 1);
    if (!touchWasPressed) {
      touchWasPressed = true;
      touchBeganWhileBlanked = scheduledScreenOff;
      touchStartAt = millis();
      touchStartX = x;
      touchStartY = y;
    }
    data->state = touchBeganWhileBlanked ? LV_INDEV_STATE_RELEASED
                                         : LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
    lastTouchX = x;
    lastTouchY = y;
    noteWakeActivity(millis());
    if (!touchBeganWhileBlanked && !provisioningScreen) {
      lastTouchAt = millis();
      if (currentPage != 0) {
        lastPageChangeAt = lastTouchAt;
      }
    }
    return;
  }

  data->state = LV_INDEV_STATE_RELEASED;
  data->point.x = lastTouchX;
  data->point.y = lastTouchY;
  if (touchWasPressed) {
    const int16_t endX = lastTouchX;
    const int16_t endY = lastTouchY;
    const int16_t dx = endX - touchStartX;
    const int16_t dy = endY - touchStartY;
    const uint32_t releasedAt = millis();
    noteWakeActivity(releasedAt);
    if (touchBeganWhileBlanked) {
      registerBlankedTap(releasedAt, dx, dy, releasedAt - touchStartAt);
      touchWasPressed = false;
      touchBeganWhileBlanked = false;
      return;
    }
    if (!provisioningScreen) {
      lastTouchAt = releasedAt;
      if (currentPage != 0) {
        lastPageChangeAt = releasedAt;
      }
    }
    // Sliders and rollers own their drags while settings are open. Treating
    // those gestures as global page swipes would destroy the active control.
    if (!provisioningScreen && !deviceSettingsScreen &&
        releasedAt - lastSwipeAt > 350) {
      if (dy > SWIPE_THRESHOLD && abs(dy) > abs(dx)) {
        if (!mediaStoreSdMounted()) {
          sdRescanRequested = true;
        }
        pendingScreenRequest = ScreenRequest::DeviceSettings;
        pendingSystemPage = false;
        pendingSwipe = 0;
        lastSwipeAt = releasedAt;
      } else if (abs(dx) > SWIPE_THRESHOLD && abs(dx) > abs(dy)) {
        pendingSwipe = dx < 0 ? 1 : -1;
        pendingSystemPage = false;
        lastSwipeAt = releasedAt;
      }
    }
  }
  touchWasPressed = false;
  touchBeganWhileBlanked = false;
}

// The 'S' drive covers both backing stores: uploaded media in LittleFS and
// anything under /sd/ on the TF card.
struct LvglFileHandle {
  File file;
  bool sdBacked = false;
  bool failed = false;
};

void* fsOpen(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
  const String normalized = withLeadingSlash(path);
  const char* openMode = (mode & LV_FS_MODE_WR) ? FILE_WRITE : FILE_READ;
  File file = mediaOpen(normalized, openMode);
  if (!file) {
    return nullptr;
  }
  return new LvglFileHandle{file, mediaIsSdPath(normalized), false};
}

lv_fs_res_t fsClose(lv_fs_drv_t*, void* filePointer) {
  LvglFileHandle* handle = static_cast<LvglFileHandle*>(filePointer);
  handle->file.close();
  delete handle;
  return LV_FS_RES_OK;
}

lv_fs_res_t fsRead(lv_fs_drv_t*, void* filePointer, void* buffer,
                   uint32_t bytesToRead, uint32_t* bytesRead) {
  LvglFileHandle* handle = static_cast<LvglFileHandle*>(filePointer);
  if (handle->failed) {
    *bytesRead = 0;
    return LV_FS_RES_UNKNOWN;
  }
  errno = 0;
  *bytesRead =
      handle->file.read(static_cast<uint8_t*>(buffer), bytesToRead);
  const int errorCode = errno;
  if (handle->sdBacked && errorCode != 0) {
    handle->failed = true;
    mediaStoreReportSdIoError(errorCode);
    return LV_FS_RES_UNKNOWN;
  }
  return LV_FS_RES_OK;
}

lv_fs_res_t fsSeek(lv_fs_drv_t*, void* filePointer, uint32_t position,
                   lv_fs_whence_t whence) {
  LvglFileHandle* handle = static_cast<LvglFileHandle*>(filePointer);
  if (handle->failed) {
    return LV_FS_RES_UNKNOWN;
  }
  SeekMode mode = SeekSet;
  if (whence == LV_FS_SEEK_CUR) {
    mode = SeekCur;
  } else if (whence == LV_FS_SEEK_END) {
    mode = SeekEnd;
  }
  errno = 0;
  if (handle->file.seek(position, mode)) {
    return LV_FS_RES_OK;
  }
  if (handle->sdBacked) {
    handle->failed = true;
    mediaStoreReportSdIoError(errno);
  }
  return LV_FS_RES_UNKNOWN;
}

lv_fs_res_t fsTell(lv_fs_drv_t*, void* filePointer, uint32_t* position) {
  LvglFileHandle* handle = static_cast<LvglFileHandle*>(filePointer);
  if (handle->failed) {
    return LV_FS_RES_UNKNOWN;
  }
  errno = 0;
  const size_t current = handle->file.position();
  if (current == static_cast<size_t>(-1)) {
    if (handle->sdBacked) {
      handle->failed = true;
      mediaStoreReportSdIoError(errno);
    }
    return LV_FS_RES_UNKNOWN;
  }
  *position = current;
  return LV_FS_RES_OK;
}

// A GIF holds roughly four bytes of PSRAM per pixel plus an open file for as
// long as it is on screen. Screens are built before the outgoing one is
// deleted, so the outgoing player is retired here instead: that keeps a
// GIF-to-GIF transition from having to fit two decoders at once.
void releaseActiveAnimation() {
  if (!activeAnimation) {
    return;
  }
  lv_obj_del(activeAnimation);
  activeAnimation = nullptr;
}

lv_obj_t* createScreen(uint32_t background) {
  releaseActiveAnimation();
  liveClockLabel = nullptr;
  playbackClockCard = nullptr;
  playbackDateShadowLabel = nullptr;
  playbackDateLabel = nullptr;
  playbackTimeShadowLabel = nullptr;
  playbackTimeLabel = nullptr;
  brightnessValueLabel = nullptr;
  editorYearRoller = nullptr;
  editorMonthRoller = nullptr;
  editorDayRoller = nullptr;
  editorHourRoller = nullptr;
  editorMinuteRoller = nullptr;
  liveClockText = "";
  playbackDateText = "";
  playbackTimeText = "";
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(background), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  return screen;
}

bool containsUtf8(const String& text) {
  for (size_t i = 0; i < text.length(); ++i) {
    if (static_cast<uint8_t>(text[i]) >= 0x80) {
      return true;
    }
  }
  return false;
}

bool containsCjk(const String& text) {
  uint32_t byteIndex = 0;
  while (byteIndex < text.length()) {
    const uint32_t codepoint = _lv_txt_encoded_next(text.c_str(), &byteIndex);
    if ((codepoint >= 0x2E80 && codepoint <= 0x9FFF) ||
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
        (codepoint >= 0xFF00 && codepoint <= 0xFFEF)) {
      return true;
    }
  }
  return false;
}

bool isEmojiCodepoint(uint32_t codepoint) {
  return codepoint == 0x00A9 || codepoint == 0x00AE ||
         codepoint == 0x203C || codepoint == 0x2049 ||
         codepoint == 0x20E3 || codepoint == 0x2122 || codepoint == 0x2139 ||
         (codepoint >= 0x2194 && codepoint <= 0x21FF) ||
         (codepoint >= 0x2300 && codepoint <= 0x23FF) ||
         codepoint == 0x24C2 ||
         (codepoint >= 0x25AA && codepoint <= 0x27BF) ||
         (codepoint >= 0x2934 && codepoint <= 0x2935) ||
         (codepoint >= 0x2B05 && codepoint <= 0x2B55) ||
         codepoint == 0x3030 || codepoint == 0x303D ||
         codepoint == 0x3297 || codepoint == 0x3299 ||
         (codepoint >= 0x1F000 && codepoint <= 0x1FAFF);
}

bool containsEmoji(const String& text) {
  uint32_t byteIndex = 0;
  while (byteIndex < text.length()) {
    if (isEmojiCodepoint(_lv_txt_encoded_next(text.c_str(), &byteIndex))) {
      return true;
    }
  }
  return false;
}

String normalizeEmojiText(const String& text) {
  String normalized;
  normalized.reserve(text.length());
  uint32_t byteIndex = 0;
  while (byteIndex < text.length()) {
    const uint32_t start = byteIndex;
    const uint32_t codepoint = _lv_txt_encoded_next(text.c_str(), &byteIndex);
    if (codepoint == 0xFE0F || codepoint == 0x200D) {
      continue;
    }
    for (uint32_t i = start; i < byteIndex; ++i) {
      normalized += text[i];
    }
  }
  return normalized;
}

lv_obj_t* addLabel(lv_obj_t* parent, const String& text,
                   const lv_font_t* font, uint32_t color, int width,
                   lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text.c_str());
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, width);
  const lv_font_t* selectedFont = font;
  // Montserrat already contains Latin-1 marks such as °. ui_font_16_zh only
  // covers the device's Chinese UI strings, so a UTF-8 check here turned °C
  // into a missing-glyph box.
  if (containsCjk(text) && font != &lv_font_simsun_16_cjk &&
      font != &ui_font_emoji_32) {
    selectedFont = &ui_font_16_zh;
  }
  lv_obj_set_style_text_font(label, selectedFont, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(label, alignment, 0);
  return label;
}

void alignPlaybackClockCard(lv_obj_t* card) {
  if (!card) {
    return;
  }

  switch (playbackClockCorner) {
    case PlaybackClockCorner::TopLeft:
      lv_obj_align(card, LV_ALIGN_TOP_LEFT, PLAYBACK_CLOCK_INSET,
                   PLAYBACK_CLOCK_INSET);
      break;
    case PlaybackClockCorner::TopRight:
      lv_obj_align(card, LV_ALIGN_TOP_RIGHT, -PLAYBACK_CLOCK_INSET,
                   PLAYBACK_CLOCK_INSET);
      break;
    case PlaybackClockCorner::BottomLeft:
      lv_obj_align(card, LV_ALIGN_BOTTOM_LEFT, PLAYBACK_CLOCK_INSET,
                   -PLAYBACK_CLOCK_INSET);
      break;
    case PlaybackClockCorner::BottomRight:
      lv_obj_align(card, LV_ALIGN_BOTTOM_RIGHT, -PLAYBACK_CLOCK_INSET,
                   -PLAYBACK_CLOCK_INSET);
      break;
  }
}

const lv_img_dsc_t* weatherIconFor(uint8_t code, bool isDay) {
  if (code == 95) {
    return isDay ? &weather_thunderstorms_day
                 : &weather_thunderstorms_night;
  }
  if (code == 96 || code == 99) {
    return &weather_strong_thunderstorms;
  }
  if (code == 75 || code == 86) {
    return &weather_blizzard;
  }
  if (code == 71 || code == 73 || code == 77 || code == 85) {
    return isDay ? &weather_scattered_snow_showers_day
                 : &weather_scattered_snow_showers_night;
  }
  if (code == 51 || code == 53 || code == 55) {
    return &weather_drizzle;
  }
  if (code == 56 || code == 57 || code == 66 || code == 67) {
    return &weather_flurries;
  }
  if (code == 61 || code == 63) {
    return &weather_rain_showers;
  }
  if (code == 65 || code == 82) {
    return &weather_heavy_rain;
  }
  if (code == 80 || code == 81) {
    return isDay ? &weather_scattered_rain_showers_day
                 : &weather_scattered_rain_showers_night;
  }
  if (code == 45 || code == 48) {
    return &weather_haze_fog;
  }
  if (code == 0) {
    return isDay ? &weather_clear_day : &weather_clear_night;
  }
  if (code == 1) {
    return isDay ? &weather_partly_cloudy_day
                 : &weather_partly_cloudy_night;
  }
  if (code == 2) {
    return isDay ? &weather_mostly_cloudy_day
                 : &weather_mostly_cloudy_night;
  }
  if (code == 3) {
    return &weather_cloudy;
  }
  return &weather_not_available;
}

void styleOverlayBox(lv_obj_t* obj) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void addPlaybackDateTime(lv_obj_t* parent, uint32_t textColor, int width,
                         int dateY, int timeY) {
  playbackDateShadowLabel =
      addLabel(parent, "", &lv_font_montserrat_24, 0x000000, width);
  lv_obj_set_height(playbackDateShadowLabel, 27);
  lv_obj_align(playbackDateShadowLabel, LV_ALIGN_TOP_MID, 2, dateY + 2);
  lv_obj_set_style_text_opa(playbackDateShadowLabel, LV_OPA_50, 0);

  playbackDateLabel =
      addLabel(parent, "", &lv_font_montserrat_24, textColor, width);
  lv_obj_set_height(playbackDateLabel, 27);
  lv_obj_align(playbackDateLabel, LV_ALIGN_TOP_MID, 0, dateY);
  lv_obj_set_style_text_opa(playbackDateLabel, LV_OPA_70, 0);

  playbackTimeShadowLabel =
      addLabel(parent, "", &lv_font_montserrat_48, 0x000000, width);
  lv_obj_set_height(playbackTimeShadowLabel, 52);
  lv_obj_align(playbackTimeShadowLabel, LV_ALIGN_TOP_MID, 2, timeY + 2);
  lv_obj_set_style_text_opa(playbackTimeShadowLabel, LV_OPA_50, 0);

  playbackTimeLabel =
      addLabel(parent, "", &lv_font_montserrat_48, textColor, width);
  lv_obj_set_height(playbackTimeLabel, 52);
  lv_obj_align(playbackTimeLabel, LV_ALIGN_TOP_MID, 0, timeY);
}

void addPlaybackWeatherCluster(lv_obj_t* parent, bool available,
                               const WeatherSnapshot& snapshot,
                               uint32_t textColor) {
  lv_obj_t* icon = lv_img_create(parent);
  lv_img_set_src(icon, available ? weatherIconFor(snapshot.weatherCode,
                                                  snapshot.isDay)
                                 : &weather_not_available);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);

  char temperature[12] = "--\xC2\xB0" "C";
  if (available) {
    snprintf(temperature, sizeof(temperature), "%d\xC2\xB0" "C",
             static_cast<int>(snapshot.temperatureCelsius));
  }
  lv_obj_t* shadow =
      addLabel(parent, temperature, &lv_font_montserrat_16, 0x000000, 56);
  lv_obj_set_style_text_opa(shadow, LV_OPA_50, 0);
  lv_obj_align(shadow, LV_ALIGN_TOP_MID, 1, 58);
  lv_obj_t* label =
      addLabel(parent, temperature, &lv_font_montserrat_16, textColor, 56);
  lv_obj_align(label, LV_ALIGN_TOP_MID, -1, 56);
}

void addPlaybackClock(lv_obj_t* parent, bool onImage,
                      uint32_t contentColor = 0xFFFFFF) {
  const bool showClock = appConfig.showDateTime();
  WeatherSnapshot snapshot;
  const bool showWeather =
      onImage && appConfig.showWeather() && weatherGetSnapshot(snapshot);
  if (!showClock && !showWeather) {
    return;
  }

  lv_obj_t* card = lv_obj_create(parent);
  playbackClockCard = card;
  if (showClock && !showWeather) {
    lv_obj_set_size(card, 184, 104);
  } else if (showClock) {
    lv_obj_set_size(card, 220, 104);
  } else {
    lv_obj_set_size(card, 72, 82);
  }
  if (!playbackClockMoveTimerStarted) {
    playbackClockCorner = static_cast<PlaybackClockCorner>(
        esp_random() % PLAYBACK_CLOCK_CORNER_COUNT);
    playbackClockLastMoveAt = millis();
    playbackClockMoveTimerStarted = true;
  }
  alignPlaybackClockCard(card);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_radius(card, 18, 0);
  if (onImage) {
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101619), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_50, 0);
  } else {
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  }
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  const uint32_t textColor = onImage ? 0xFFFFFF : contentColor;
  if (showClock && !showWeather) {
    playbackDateShadowLabel =
        addLabel(card, "", &lv_font_montserrat_24, 0x000000, 184);
    lv_obj_set_height(playbackDateShadowLabel, 27);
    lv_obj_align(playbackDateShadowLabel, LV_ALIGN_TOP_MID, 2, 10);
    lv_obj_set_style_text_opa(playbackDateShadowLabel, LV_OPA_50, 0);

    playbackDateLabel = addLabel(card, "", &lv_font_montserrat_24, textColor,
                                 184);
    lv_obj_set_height(playbackDateLabel, 27);
    lv_obj_align(playbackDateLabel, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_opa(playbackDateLabel, LV_OPA_70, 0);

    playbackTimeShadowLabel =
        addLabel(card, "", &lv_font_montserrat_48, 0x000000, 184);
    lv_obj_set_height(playbackTimeShadowLabel, 52);
    lv_obj_align(playbackTimeShadowLabel, LV_ALIGN_TOP_MID, 2, 45);
    lv_obj_set_style_text_opa(playbackTimeShadowLabel, LV_OPA_50, 0);

    playbackTimeLabel = addLabel(card, "", &lv_font_montserrat_48, textColor,
                                 184);
    lv_obj_set_height(playbackTimeLabel, 52);
    lv_obj_align(playbackTimeLabel, LV_ALIGN_TOP_MID, 0, 43);
    updateLiveClock();
    return;
  }

  lv_obj_t* row = lv_obj_create(card);
  styleOverlayBox(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);

  if (showClock) {
    lv_obj_t* timeCol = lv_obj_create(row);
    styleOverlayBox(timeCol);
    lv_obj_set_size(timeCol, 142, 88);
    addPlaybackDateTime(timeCol, textColor, 142, 0, 35);
    updateLiveClock();
  }

  lv_obj_t* weatherCol = lv_obj_create(row);
  styleOverlayBox(weatherCol);
  lv_obj_set_size(weatherCol, 52, 76);
  addPlaybackWeatherCluster(weatherCol, true, snapshot, textColor);
}

void updatePlaybackClockPosition(uint32_t now) {
  if (!playbackClockMoveTimerStarted ||
      now - playbackClockLastMoveAt < PLAYBACK_CLOCK_MOVE_INTERVAL_MS) {
    return;
  }
  if (!playbackClockCard || provisioningScreen ||
      deviceSettingsScreen || currentPage == 0 ||
      pageTransitionPhase != PageTransitionPhase::Idle || scheduledScreenOff ||
      storageBlankActive || backlightResumeAt != 0) {
    return;
  }

  const uint8_t current = static_cast<uint8_t>(playbackClockCorner);
  const uint8_t offset = static_cast<uint8_t>(
                             esp_random() %
                             (PLAYBACK_CLOCK_CORNER_COUNT - 1)) +
                         1;
  playbackClockCorner = static_cast<PlaybackClockCorner>(
      (current + offset) % PLAYBACK_CLOCK_CORNER_COUNT);
  alignPlaybackClockCard(playbackClockCard);
  lv_obj_fade_in(playbackClockCard, PLAYBACK_CLOCK_FADE_IN_MS, 0);
  playbackClockLastMoveAt = now;
}

void addCornerMark(lv_obj_t* parent, uint32_t color) {
  lv_obj_t* mark = lv_obj_create(parent);
  lv_obj_set_size(mark, 34, 8);
  lv_obj_align(mark, LV_ALIGN_TOP_LEFT, 24, 24);
  lv_obj_set_style_radius(mark, 4, 0);
  lv_obj_set_style_bg_color(mark, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(mark, 0, 0);
}

String escapeWifiQr(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char character = value[i];
    if (character == '\\' || character == ';' || character == ',' ||
        character == ':' || character == '"') {
      escaped += '\\';
    }
    escaped += character;
  }
  return escaped;
}

void addQr(lv_obj_t* parent, const String& payload, int size, int y) {
  lv_obj_t* frame = lv_obj_create(parent);
  lv_obj_set_size(frame, size + 28, size + 28);
  lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(frame, 20, 0);
  lv_obj_set_style_border_width(frame, 0, 0);
  lv_obj_set_style_pad_all(frame, 14, 0);

  lv_obj_t* qr = lv_qrcode_create(frame, size, lv_color_hex(0x101619),
                                  lv_color_white());
  lv_qrcode_update(qr, payload.c_str(), payload.length());
  lv_obj_center(qr);
}

void persistDeviceSettings(bool rerender = true) {
  // LVGL event callbacks run in the Espressif adapter worker. Defer the flash
  // write to Arduino's loop task so the worker can be fully paused first.
  settingsSavePending = true;
  settingsSaveDueAt = millis() + SETTINGS_SAVE_DEBOUNCE_MS;
  settingsSaveRetryCount = 0;
  if (rerender) {
    deviceSettingsDirty = true;
  }
}

size_t timezoneOptionIndex(int16_t offsetMinutes) {
  size_t closest = 0;
  int closestDistance = abs(static_cast<int>(offsetMinutes) -
                            static_cast<int>(TIMEZONE_OPTIONS[0]));
  for (size_t i = 0; i < TIMEZONE_OPTION_COUNT; ++i) {
    if (TIMEZONE_OPTIONS[i] == offsetMinutes) {
      return i;
    }
    const int distance = abs(static_cast<int>(offsetMinutes) -
                             static_cast<int>(TIMEZONE_OPTIONS[i]));
    if (distance < closestDistance) {
      closest = i;
      closestDistance = distance;
    }
  }
  return closest;
}

void moveTimezoneOption(int delta) {
  const int current =
      static_cast<int>(timezoneOptionIndex(appConfig.timezoneOffsetMinutes()));
  const int next = constrain(current + delta, 0,
                             static_cast<int>(TIMEZONE_OPTION_COUNT) - 1);
  appConfig.setTimezoneOffsetMinutes(TIMEZONE_OPTIONS[next]);
}

bool isLeapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(int year, int month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }
  return month == 2 && isLeapYear(year) ? 29 : DAYS[month - 1];
}

String numberedOptions(int first, int last, bool padTwo = true) {
  String options;
  options.reserve((last - first + 1) * (padTwo ? 3 : 5));
  char value[12];
  for (int number = first; number <= last; ++number) {
    if (!options.isEmpty()) {
      options += '\n';
    }
    snprintf(value, sizeof(value), padTwo ? "%02d" : "%d", number);
    options += value;
  }
  return options;
}

void beginDateTimeDraft() {
  struct tm localTime {};
  if (!getConfiguredLocalTime(localTime)) {
    localTime.tm_year = 2026 - 1900;
    localTime.tm_mon = 0;
    localTime.tm_mday = 1;
    localTime.tm_hour = 12;
    localTime.tm_min = 0;
  }
  const int year = constrain(localTime.tm_year + 1900, 2020, 2100);
  const int month = constrain(localTime.tm_mon + 1, 1, 12);
  dateTimeDraft = {
      static_cast<uint16_t>(year),
      static_cast<uint8_t>(month),
      static_cast<uint8_t>(constrain(
          localTime.tm_mday, 1, static_cast<int>(daysInMonth(year, month)))),
      static_cast<uint8_t>(constrain(localTime.tm_hour, 0, 23)),
      static_cast<uint8_t>(constrain(localTime.tm_min, 0, 59)),
  };
}

void beginTimeOfDayDraft(uint16_t minuteOfDay) {
  minuteOfDay = min<uint16_t>(minuteOfDay, 1439);
  timeOfDayDraft = {static_cast<uint8_t>(minuteOfDay / 60),
                    static_cast<uint8_t>(minuteOfDay % 60)};
}

void updateDateTimeDraftFromRollers() {
  if (!editorYearRoller || !editorMonthRoller || !editorDayRoller ||
      !editorHourRoller || !editorMinuteRoller) {
    return;
  }
  dateTimeDraft.year = 2020 + lv_roller_get_selected(editorYearRoller);
  dateTimeDraft.month = 1 + lv_roller_get_selected(editorMonthRoller);
  dateTimeDraft.day = 1 + lv_roller_get_selected(editorDayRoller);
  dateTimeDraft.hour = lv_roller_get_selected(editorHourRoller);
  dateTimeDraft.minute = lv_roller_get_selected(editorMinuteRoller);
}

void refreshDateTimeDayOptions() {
  if (!editorYearRoller || !editorMonthRoller || !editorDayRoller) {
    return;
  }
  const int year = 2020 + lv_roller_get_selected(editorYearRoller);
  const int month = 1 + lv_roller_get_selected(editorMonthRoller);
  const uint16_t selectedDay = lv_roller_get_selected(editorDayRoller) + 1;
  const uint8_t dayCount = daysInMonth(year, month);
  const String options = numberedOptions(1, dayCount);
  lv_roller_set_options(editorDayRoller, options.c_str(),
                        LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(editorDayRoller,
                         min<uint16_t>(selectedDay, dayCount) - 1,
                         LV_ANIM_OFF);
}

void handleDateRollerChanged(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t* target = lv_event_get_target(event);
    if (target == editorYearRoller || target == editorMonthRoller) {
      refreshDateTimeDayOptions();
    }
    updateDateTimeDraftFromRollers();
    lastTouchAt = millis();
  }
}

void updateTimeOfDayDraftFromRollers() {
  if (!editorHourRoller || !editorMinuteRoller) {
    return;
  }
  timeOfDayDraft.hour = lv_roller_get_selected(editorHourRoller);
  timeOfDayDraft.minute = lv_roller_get_selected(editorMinuteRoller);
}

void handleTimeRollerChanged(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    updateTimeOfDayDraftFromRollers();
    lastTouchAt = millis();
  }
}

// Gregorian civil date to days since 1970-01-01. Keeping this conversion
// independent of libc's process timezone makes the fixed UTC-offset behavior
// identical to the browser implementation.
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear =
      (153 * shiftedMonth + 2) / 5 + day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

bool applyDateTimeEditor() {
  if (!editorYearRoller || !editorMonthRoller || !editorDayRoller ||
      !editorHourRoller || !editorMinuteRoller) {
    return false;
  }
  updateDateTimeDraftFromRollers();
  const int year = dateTimeDraft.year;
  const unsigned month = dateTimeDraft.month;
  const unsigned day = dateTimeDraft.day;
  const unsigned hour = dateTimeDraft.hour;
  const unsigned minute = dateTimeDraft.minute;
  if (day > daysInMonth(year, month)) {
    return false;
  }

  const int64_t localWallTime =
      daysFromCivil(year, month, day) * 86400LL +
      static_cast<int64_t>(hour) * 3600LL +
      static_cast<int64_t>(minute) * 60LL;
  if (localWallTime < LOCAL_TIME_MIN_EPOCH ||
      localWallTime >= LOCAL_TIME_MAX_EPOCH_EXCLUSIVE) {
    Serial.println("[settings] manual date/time is outside supported range");
    return false;
  }
  const int64_t epoch =
      localWallTime -
      static_cast<int64_t>(appConfig.timezoneOffsetMinutes()) * 60LL;
  struct timeval value {
    static_cast<time_t>(epoch), 0
  };
  return settimeofday(&value, nullptr) == 0;
}

bool applyTimeEditor() {
  if (!editorHourRoller || !editorMinuteRoller) {
    return false;
  }
  updateTimeOfDayDraftFromRollers();
  const uint16_t minuteOfDay =
      timeOfDayDraft.hour * 60U + timeOfDayDraft.minute;
  if (activeSettingsEditor == SettingsEditor::ScreenOffStart) {
    appConfig.setScreenOffWindow(minuteOfDay,
                                 appConfig.screenOffEndMinutes());
    return true;
  }
  if (activeSettingsEditor == SettingsEditor::ScreenOffEnd) {
    appConfig.setScreenOffWindow(appConfig.screenOffStartMinutes(),
                                 minuteOfDay);
    return true;
  }
  return false;
}

void handleBrightnessValueChanged(lv_event_t* event) {
  const lv_event_code_t eventCode = lv_event_get_code(event);
  if (eventCode == LV_EVENT_RELEASED || eventCode == LV_EVENT_PRESS_LOST) {
    persistDeviceSettings(false);
    lastTouchAt = millis();
    return;
  }
  if (eventCode != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  lv_obj_t* brightnessSlider = lv_event_get_target(event);
  const uint8_t brightness = static_cast<uint8_t>(
      constrain(lv_slider_get_value(brightnessSlider), 5, 100));
  appConfig.setBrightness(brightness);
  if (brightnessValueLabel) {
    lv_label_set_text_fmt(brightnessValueLabel, "%u%%",
                          static_cast<unsigned>(brightness));
  }
  backlightApply();
  lastTouchAt = millis();
}

void handleSettingsAction(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  const SettingsAction action = static_cast<SettingsAction>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  lastTouchAt = millis();

  if (action == SettingsAction::Open) {
    if (!mediaStoreSdMounted()) {
      sdRescanRequested = true;
    }
    pendingScreenRequest = ScreenRequest::DeviceSettings;
    return;
  }
  if (action == SettingsAction::Back) {
    activeSettingsEditor = SettingsEditor::None;
    pendingScreenRequest = ScreenRequest::SystemPage;
    return;
  }

  switch (action) {
    case SettingsAction::ShowDisplay:
      activeSettingsSection = SettingsSection::Display;
      activeSettingsEditor = SettingsEditor::None;
      deviceSettingsDirty = true;
      return;
    case SettingsAction::ShowTime:
      activeSettingsSection = SettingsSection::Time;
      activeSettingsEditor = SettingsEditor::None;
      deviceSettingsDirty = true;
      return;
    case SettingsAction::ShowSystem:
      activeSettingsSection = SettingsSection::System;
      activeSettingsEditor = SettingsEditor::None;
      if (!mediaStoreSdMounted()) {
        sdRescanRequested = true;
      }
      deviceSettingsDirty = true;
      return;
    case SettingsAction::SetChinese:
      if (appConfig.language() == InterfaceLanguage::Chinese) {
        return;
      }
      appConfig.setLanguage(InterfaceLanguage::Chinese);
      break;
    case SettingsAction::SetEnglish:
      if (appConfig.language() == InterfaceLanguage::English) {
        return;
      }
      appConfig.setLanguage(InterfaceLanguage::English);
      break;
    case SettingsAction::TimezonePrevious:
      moveTimezoneOption(-1);
      break;
    case SettingsAction::TimezoneNext:
      moveTimezoneOption(1);
      break;
    case SettingsAction::SyncTime:
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      deviceSettingsDirty = true;
      return;
    case SettingsAction::EditDateTime:
      beginDateTimeDraft();
      activeSettingsEditor = SettingsEditor::DateTime;
      deviceSettingsDirty = true;
      return;
    case SettingsAction::ToggleClock:
      appConfig.setShowDateTime(!appConfig.showDateTime());
      break;
    case SettingsAction::ToggleWeather:
      appConfig.setShowWeather(!appConfig.showWeather());
      break;
    case SettingsAction::ToggleScreenOff:
      appConfig.setScreenOffEnabled(!appConfig.screenOffEnabled());
      break;
    case SettingsAction::EditScreenOffStart:
      if (!appConfig.screenOffEnabled()) {
        return;
      }
      beginTimeOfDayDraft(appConfig.screenOffStartMinutes());
      activeSettingsEditor = SettingsEditor::ScreenOffStart;
      deviceSettingsDirty = true;
      return;
    case SettingsAction::EditScreenOffEnd:
      if (!appConfig.screenOffEnabled()) {
        return;
      }
      beginTimeOfDayDraft(appConfig.screenOffEndMinutes());
      activeSettingsEditor = SettingsEditor::ScreenOffEnd;
      deviceSettingsDirty = true;
      return;
    case SettingsAction::CancelEditor:
      activeSettingsEditor = SettingsEditor::None;
      deviceSettingsDirty = true;
      return;
    case SettingsAction::SaveDateTime:
      if (applyDateTimeEditor()) {
        activeSettingsEditor = SettingsEditor::None;
        deviceSettingsDirty = true;
      }
      return;
    case SettingsAction::SaveTimeEditor:
      if (!appConfig.screenOffEnabled()) {
        activeSettingsEditor = SettingsEditor::None;
        activeSettingsSection = SettingsSection::Display;
        deviceSettingsDirty = true;
        return;
      }
      if (applyTimeEditor()) {
        activeSettingsEditor = SettingsEditor::None;
        persistDeviceSettings();
      }
      return;
    case SettingsAction::RescanSd:
      sdRescanRequested = true;
      return;
    case SettingsAction::OpenWifiReset:
      activeSettingsEditor = SettingsEditor::WifiReset;
      deviceSettingsDirty = true;
      return;
    case SettingsAction::ConfirmWifiReset:
      wifiResetInProgress = true;
      wifiResetRequested = true;
      deviceSettingsDirty = true;
      return;
    default:
      return;
  }
  persistDeviceSettings();
}

lv_obj_t* addSettingsButton(
    lv_obj_t* parent, const String& text, int x, int y, int width, int height,
    SettingsAction action, bool accent = false,
    const lv_font_t* labelFont = &lv_font_montserrat_14) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_radius(button, height / 2, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, accent ? 0 : 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x4A5557), 0);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(accent ? 0xE7FF54 : 0x20282A), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(
      button, handleSettingsAction, LV_EVENT_CLICKED,
      reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
  lv_obj_t* label = addLabel(button, text, labelFont,
                             accent ? 0x101619 : 0xF4EFE6, width - 12);
  lv_obj_center(label);
  return button;
}

// Every screen swap is an atomic cut. LVGL 8.3 cannot be re-entered while a
// screen-load animation is running: lv_scr_load_anim() reads lv_scr_act(),
// which already points at the incoming screen once the animation has started,
// and then deletes it as if it were the outgoing one. disp->act_scr is left
// dangling and the following lv_obj_set_pos() writes into freed memory, which
// showed up as leftover pixels from the previous page. A second swipe inside
// the 260 ms window, or a web-side content edit landing in it, was enough to
// hit that. Loading immediately keeps disp->scr_to_load NULL at all times, so
// the re-entrant path can never be taken. It also avoids animating a
// full-refresh 480x480 display, where every animation step redraws the whole
// screen. Content playback smoothness is provided separately by fading the
// panel backlight through black around this atomic load.
void loadScreen(lv_obj_t* screen) {
  lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

void renderProvisioning() {
  provisioningScreen = true;
  deviceSettingsScreen = false;
  lv_obj_t* screen = createScreen(0xE7FF54);
  addCornerMark(screen, 0x101619);

  lv_obj_t* eyebrow =
      addLabel(screen, uiText("首次启动 / Wi-Fi", "First light / Wi-Fi"),
               &lv_font_montserrat_14,
               0x101619, 420, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(eyebrow, LV_ALIGN_TOP_LEFT, 72, 20);

  lv_obj_t* title = addLabel(screen,
                             uiText("扫码配置 Wi-Fi",
                                    "Scan to set up Wi-Fi"),
                             &lv_font_montserrat_24, 0x101619, 430);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 53);

  String qrPayload = "WIFI:T:WPA;S:" + escapeWifiQr(provisioningSsid) +
                     ";P:" + escapeWifiQr(provisioningPassword) + ";;";
  addQr(screen, qrPayload, 230, 94);

  lv_obj_t* helper =
      addLabel(screen,
               String(uiText("连接后打开设置页\n",
                             "Connect, then open the setup page\n")) +
                   provisioningAddress,
               &lv_font_montserrat_16, 0x101619, 420);
  lv_obj_align(helper, LV_ALIGN_BOTTOM_MID, 0, -27);

  loadScreen(screen);
}

void renderSystemPage() {
  provisioningScreen = false;
  deviceSettingsScreen = false;
  lv_obj_t* screen = createScreen(0x101619);
  addCornerMark(screen, 0xE7FF54);

  const String ip = WiFi.localIP().toString();
  const String url = "http://" + ip + "/";

  lv_obj_t* eyebrow =
      addLabel(screen, uiText("ScreenDeck / 在线", "ScreenDeck / Online"),
               &lv_font_montserrat_14,
               0xE7FF54, 420, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(eyebrow, LV_ALIGN_TOP_LEFT, 72, 20);

  lv_obj_t* title =
      addLabel(screen, uiText("控制此屏幕", "Control this screen"),
               &lv_font_montserrat_24,
               0xF4EFE6, 430);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 53);

  addQr(screen, url, 230, 94);

  lv_obj_t* ipLabel =
      addLabel(screen, ip, &lv_font_montserrat_28, 0xF4EFE6, 420);
  lv_obj_align(ipLabel, LV_ALIGN_BOTTOM_MID, 0, -61);

  const String hintText =
      appConfig.pageCount() == 0
          ? uiText("在此地址添加页面", "Add pages from this address")
          : uiText("10 秒后返回播放", "Returns to playback after 10 seconds");
  lv_obj_t* hint = addLabel(screen, hintText, &lv_font_montserrat_14,
                            0x93A0A5, 290, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 24, -24);
  addSettingsButton(screen, uiText("设置", "Settings"), 344, 426, 116, 36,
                    SettingsAction::Open, true);

  loadScreen(screen);
}

lv_obj_t* addSettingsList(lv_obj_t* screen) {
  lv_obj_t* list = lv_obj_create(screen);
  lv_obj_set_pos(list, 0, 110);
  lv_obj_set_size(list, SCREEN_WIDTH, SCREEN_HEIGHT - 110);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_bottom(list, 24, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  return list;
}

lv_obj_t* addSettingsPanel(lv_obj_t* parent, int y, int height = 52) {
  lv_obj_t* panel = lv_obj_create(parent);
  lv_obj_set_pos(panel, 20, y);
  lv_obj_set_size(panel, 440, height);
  lv_obj_set_style_radius(panel, 16, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x354043), 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x20282A), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  return panel;
}

lv_obj_t* addSettingsRoller(lv_obj_t* parent, const String& options,
                            uint16_t selected, int x, int y, int width,
                            int height, uint8_t visibleRows = 5) {
  lv_obj_t* roller = lv_roller_create(parent);
  lv_roller_set_options(roller, options.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
  lv_roller_set_visible_row_count(roller, visibleRows);
  lv_obj_set_pos(roller, x, y);
  lv_obj_set_size(roller, width, height);
  lv_obj_set_style_radius(roller, 16, LV_PART_MAIN);
  lv_obj_set_style_border_width(roller, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(roller, lv_color_hex(0x354043), LV_PART_MAIN);
  lv_obj_set_style_bg_color(roller, lv_color_hex(0x20282A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(roller, lv_color_hex(0x93A0A5), LV_PART_MAIN);
  lv_obj_set_style_text_font(roller, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_bg_color(roller, lv_color_hex(0xE7FF54),
                            LV_PART_SELECTED);
  lv_obj_set_style_text_color(roller, lv_color_hex(0x101619),
                              LV_PART_SELECTED);
  lv_obj_set_style_text_font(roller, &lv_font_montserrat_20,
                             LV_PART_SELECTED);
  return roller;
}

void styleDangerButton(lv_obj_t* button) {
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x7D2D2D), 0);
}

void renderSettingsHeader(lv_obj_t* screen) {
  addSettingsButton(screen, uiText("二维码", "QR"), 20, 16, 68, 36,
                    SettingsAction::Back);
  lv_obj_t* title = addLabel(screen, uiText("设备设置", "DEVICE SETTINGS"),
                             &lv_font_montserrat_20, 0xF4EFE6, 340,
                             LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(title, 108, 22);

  addSettingsButton(screen, uiText("显示", "Display"), 20, 64, 138, 38,
                    SettingsAction::ShowDisplay,
                    activeSettingsSection == SettingsSection::Display);
  addSettingsButton(screen, uiText("时间", "Time"), 171, 64, 138, 38,
                    SettingsAction::ShowTime,
                    activeSettingsSection == SettingsSection::Time);
  addSettingsButton(screen, uiText("系统", "System"), 322, 64, 138, 38,
                    SettingsAction::ShowSystem,
                    activeSettingsSection == SettingsSection::System);
}

void renderDisplaySettings(lv_obj_t* screen) {
  const lv_font_t* settingsRowFont = &lv_font_montserrat_16;
  lv_obj_t* list = addSettingsList(screen);
  int y = 0;
  lv_obj_t* brightnessPanel = addSettingsPanel(list, y, 78);
  lv_obj_t* brightnessLabel =
      addLabel(brightnessPanel, uiText("屏幕亮度", "Brightness"),
               &lv_font_montserrat_14, 0xF4EFE6, 220, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(brightnessLabel, 16, 10);
  brightnessValueLabel =
      addLabel(brightnessPanel,
               String(static_cast<unsigned>(appConfig.brightness())) + "%",
               &lv_font_montserrat_20, 0xE7FF54, 80, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_pos(brightnessValueLabel, 338, 7);

  lv_obj_t* brightnessSlider = lv_slider_create(brightnessPanel);
  lv_obj_set_pos(brightnessSlider, 18, 51);
  lv_obj_set_size(brightnessSlider, 404, 16);
  lv_obj_set_ext_click_area(brightnessSlider, 10);
  lv_slider_set_range(brightnessSlider, 5, 100);
  lv_slider_set_value(brightnessSlider, appConfig.brightness(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0x354043),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0xE7FF54),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0xE7FF54),
                            LV_PART_KNOB);
  lv_obj_set_style_pad_all(brightnessSlider, 7, LV_PART_KNOB);
  lv_obj_add_event_cb(brightnessSlider, handleBrightnessValueChanged,
                      LV_EVENT_ALL, nullptr);

  y += 86;
  lv_obj_t* clockPanel = addSettingsPanel(list, y, 48);
  lv_obj_t* clockLabel = addLabel(
      clockPanel, uiText("页面日期时间", "Date + time on pages"),
      &lv_font_montserrat_14, 0xF4EFE6, 275, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(clockLabel, 16, 17);
  addSettingsButton(
      clockPanel,
      appConfig.showDateTime() ? uiText("开启", "On")
                               : uiText("关闭", "Off"),
      318, 6, 100, 36, SettingsAction::ToggleClock,
      appConfig.showDateTime());

  y += 56;
  const bool weatherEnabled = appConfig.showWeather();
  WeatherSnapshot weatherSnapshot;
  const bool weatherLocated =
      weatherEnabled && weatherGetSnapshot(weatherSnapshot) &&
      weatherSnapshot.location[0] != '\0';
  lv_obj_t* weatherPanel = addSettingsPanel(list, y, 48);
  const int weatherTitleWidth = weatherEnabled
                                    ? (useChineseUi() ? 96 : 170)
                                    : 275;
  lv_obj_t* weatherLabel = addLabel(
      weatherPanel, uiText("图片页天气", "Weather on image pages"),
      &lv_font_montserrat_14, 0xF4EFE6, weatherTitleWidth, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(weatherLabel, 16, 17);
  if (weatherEnabled) {
    const char* locationText =
        weatherLocated ? weatherSnapshot.location : uiText("定位中", "Locating");
    const lv_font_t* locationFont = containsCjk(locationText)
                                        ? &lv_font_simsun_16_cjk
                                        : &lv_font_montserrat_14;
    lv_obj_t* locationLabel = addLabel(
        weatherPanel, locationText, locationFont, 0x93A0A5,
        302 - (16 + weatherTitleWidth + 8), LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(locationLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_height(locationLabel, 20);
    lv_obj_set_pos(locationLabel, 16 + weatherTitleWidth + 8, 17);
  }
  addSettingsButton(
      weatherPanel,
      weatherEnabled ? uiText("开启", "On") : uiText("关闭", "Off"),
      318, 6, 100, 36, SettingsAction::ToggleWeather, weatherEnabled);

  y += 56;
  lv_obj_t* screenOffPanel = addSettingsPanel(list, y, 48);
  lv_obj_t* screenOffLabel = addLabel(
      screenOffPanel, uiText("自动息屏", "Scheduled sleep"), settingsRowFont,
      0xF4EFE6, 250, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(screenOffLabel, 16, 17);
  addSettingsButton(
      screenOffPanel,
      appConfig.screenOffEnabled() ? uiText("开启", "On")
                                   : uiText("关闭", "Off"),
      318, 6, 100, 36, SettingsAction::ToggleScreenOff,
      appConfig.screenOffEnabled(), settingsRowFont);

  if (appConfig.screenOffEnabled()) {
    y += 56;
    lv_obj_t* startPanel = addSettingsPanel(list, y, 48);
    lv_obj_t* startLabel =
        addLabel(startPanel, uiText("息屏时间", "Screen off"), settingsRowFont,
                 0xF4EFE6, 250, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(startLabel, 16, 17);
    addSettingsButton(startPanel,
                      formatMinuteOfDay(appConfig.screenOffStartMinutes()), 318,
                      6, 100, 36, SettingsAction::EditScreenOffStart, true,
                      settingsRowFont);

    y += 56;
    lv_obj_t* endPanel = addSettingsPanel(list, y, 48);
    lv_obj_t* endLabel =
        addLabel(endPanel, uiText("恢复时间", "Resume at"), settingsRowFont,
                 0xF4EFE6, 250, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(endLabel, 16, 17);
    addSettingsButton(endPanel,
                      formatMinuteOfDay(appConfig.screenOffEndMinutes()), 318,
                      6, 100, 36, SettingsAction::EditScreenOffEnd, true,
                      settingsRowFont);
  } else {
    lv_obj_t* hint = addLabel(
        list,
        uiText("天气使用网络定位，每 30 分钟自动更新",
               "Weather uses IP location and updates every 30 min"),
        &lv_font_montserrat_14, 0x93A0A5, 420);
    lv_obj_set_pos(hint, 30, y + 56);
  }
}

void renderTimeSettings(lv_obj_t* screen) {
  const lv_font_t* settingsRowFont = &lv_font_montserrat_16;
  lv_obj_t* list = addSettingsList(screen);
  lv_obj_t* currentPanel = addSettingsPanel(list, 0, 70);
  liveClockLabel = addLabel(currentPanel, "", &lv_font_montserrat_16,
                            0xE7FF54, 236, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(liveClockLabel, 16, 25);
  addSettingsButton(currentPanel, uiText("手动", "Set"), 266, 17, 70, 36,
                    SettingsAction::EditDateTime);
  addSettingsButton(currentPanel, uiText("校时", "Sync"), 348, 17, 70, 36,
                    SettingsAction::SyncTime, true);
  updateLiveClock();

  lv_obj_t* timezonePanel = addSettingsPanel(list, 78, 58);
  lv_obj_t* timezoneLabel =
      addLabel(timezonePanel, uiText("时区", "Timezone"), settingsRowFont,
               0xF4EFE6, 80, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(timezoneLabel, 16, 21);
  lv_obj_t* timezoneValue =
      addLabel(timezonePanel,
               formatTimezoneOffset(appConfig.timezoneOffsetMinutes()),
               settingsRowFont, 0xF4EFE6, 165);
  lv_obj_set_pos(timezoneValue, 108, 16);
  addSettingsButton(timezonePanel, "-", 286, 11, 60, 36,
                    SettingsAction::TimezonePrevious, false, settingsRowFont);
  addSettingsButton(timezonePanel, "+", 358, 11, 60, 36,
                    SettingsAction::TimezoneNext, true, settingsRowFont);
}

void renderSystemSettings(lv_obj_t* screen) {
  lv_obj_t* list = addSettingsList(screen);
  lv_obj_t* languagePanel = addSettingsPanel(list, 0, 58);
  lv_obj_t* languageLabel = addLabel(
      languagePanel, uiText("界面语言", "Language"), &lv_font_montserrat_14,
      0xF4EFE6, 190, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(languageLabel, 16, 20);
  addSettingsButton(languagePanel, "中文", 266, 11, 72, 36,
                    SettingsAction::SetChinese, useChineseUi());
  addSettingsButton(languagePanel, "EN", 350, 11, 68, 36,
                    SettingsAction::SetEnglish, !useChineseUi());

  const bool sdMounted = mediaStoreSdMounted();
  const MediaStoreSdStatus cardStatus = mediaStoreSdStatus();
  lv_obj_t* sdPanel = addSettingsPanel(list, 66, 86);
  lv_obj_t* sdLabel =
      addLabel(sdPanel, uiText("TF 卡", "TF card"), &lv_font_montserrat_14,
               0x93A0A5, 280, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(sdLabel, 16, 8);
  String sdCapacity = uiText("未找到", "Not detected");
  uint32_t sdCapacityColor = 0xF4EFE6;
  if (sdMounted && cardStatus == MediaStoreSdStatus::Ready) {
    sdCapacity = String(uiText("容量 ", "Capacity ")) +
                 formatStorageSize(mediaStoreSdTotalBytes()) + " · " +
                 uiText("已用 ", "Used ") +
                 formatStorageSize(mediaStoreSdUsedBytes());
    sdCapacityColor = 0xE7FF54;
  } else if (cardStatus == MediaStoreSdStatus::UnsupportedFilesystem ||
             cardStatus == MediaStoreSdStatus::UnreadableFilesystem) {
    sdCapacity = uiText("无法使用", "Unavailable");
    sdCapacityColor = 0xFFB36B;
  }
  lv_obj_t* sdCapacityLabel =
      addLabel(sdPanel, sdCapacity, &lv_font_montserrat_14, sdCapacityColor, 300,
               LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(sdCapacityLabel, 16, 43);
  addSettingsButton(sdPanel, uiText("扫描", "Scan"), 330, 24, 88, 38,
                    SettingsAction::RescanSd, sdMounted);

  lv_obj_t* networkPanel = addSettingsPanel(list, 160, 72);
  lv_obj_t* networkLabel =
      addLabel(networkPanel, uiText("当前 Wi-Fi", "Current Wi-Fi"),
               &lv_font_montserrat_14, 0x93A0A5, 390, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(networkLabel, 16, 10);
  const String ssid = WiFi.SSID().isEmpty()
                          ? String(uiText("未连接", "Not connected"))
                          : WiFi.SSID();
  lv_obj_t* ssidLabel = addLabel(networkPanel, ssid, &lv_font_montserrat_16,
                                 0xF4EFE6, 390, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(ssidLabel, 16, 39);

  lv_obj_t* firmwarePanel = addSettingsPanel(list, 240, 72);
  const char* firmwareTitle = uiText("固件", "Firmware");
  const lv_font_t* firmwareTitleFont =
      containsCjk(firmwareTitle) ? &lv_font_simsun_16_cjk
                                 : &lv_font_montserrat_14;
  lv_obj_t* firmwareLabel =
      addLabel(firmwarePanel, firmwareTitle, firmwareTitleFont, 0x93A0A5, 390,
               LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(firmwareLabel, 16, 10);
  String firmwareDetail = SCREENDECK_VERSION;
  if (SCREENDECK_BUILD_TIME[0] != '\0' &&
      strcmp(SCREENDECK_BUILD_TIME, "unknown") != 0) {
    firmwareDetail += " · ";
    firmwareDetail += String(SCREENDECK_BUILD_TIME).substring(0, 10);
  }
  lv_obj_t* firmwareValue =
      addLabel(firmwarePanel, firmwareDetail, &lv_font_montserrat_16, 0xE7FF54,
               390, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_pos(firmwareValue, 16, 39);

  lv_obj_t* resetButton = addSettingsButton(
      list, uiText("清除 Wi-Fi 并重启", "Clear Wi-Fi and restart"), 30, 324,
      380, 48, SettingsAction::OpenWifiReset);
  styleDangerButton(resetButton);
  lv_obj_t* warning = addLabel(
      list,
      uiText("设备重启后会重新显示配网二维码",
             "The setup QR returns after the device restarts"),
      &lv_font_montserrat_14, 0x93A0A5, 400);
  lv_obj_set_pos(warning, 20, 380);
}

void renderEditorTitle(lv_obj_t* screen, const String& title,
                       SettingsAction saveAction) {
  addSettingsButton(screen, uiText("取消", "Cancel"), 20, 18, 86, 38,
                    SettingsAction::CancelEditor);
  lv_obj_t* titleLabel = addLabel(screen, title, &lv_font_montserrat_20,
                                  0xF4EFE6, 250);
  lv_obj_set_pos(titleLabel, 115, 27);
  addSettingsButton(screen, uiText("保存", "Save"), 376, 18, 84, 38,
                    saveAction, true);
}

void addEditorColumnLabel(lv_obj_t* screen, const String& text, int x,
                          int width) {
  lv_obj_t* label = addLabel(screen, text, &lv_font_montserrat_14, 0x93A0A5,
                             width);
  lv_obj_set_pos(label, x, 82);
}

void renderDateTimeEditor(lv_obj_t* screen) {
  renderEditorTitle(screen, uiText("设置日期时间", "Set date and time"),
                    SettingsAction::SaveDateTime);
  const int year = constrain(static_cast<int>(dateTimeDraft.year), 2020, 2100);
  const int month = constrain(static_cast<int>(dateTimeDraft.month), 1, 12);
  const int day = constrain(static_cast<int>(dateTimeDraft.day), 1,
                            static_cast<int>(daysInMonth(year, month)));
  const int hour = constrain(static_cast<int>(dateTimeDraft.hour), 0, 23);
  const int minute = constrain(static_cast<int>(dateTimeDraft.minute), 0, 59);

  addEditorColumnLabel(screen, uiText("年", "Y"), 20, 100);
  addEditorColumnLabel(screen, uiText("月", "M"), 125, 72);
  addEditorColumnLabel(screen, uiText("日", "D"), 202, 72);
  addEditorColumnLabel(screen, uiText("时", "H"), 279, 72);
  addEditorColumnLabel(screen, uiText("分", "M"), 356, 72);

  editorYearRoller = addSettingsRoller(
      screen, numberedOptions(2020, 2100, false), year - 2020, 20, 108, 100,
      280);
  editorMonthRoller = addSettingsRoller(
      screen, numberedOptions(1, 12), month - 1, 125, 108, 72, 280);
  editorDayRoller = addSettingsRoller(
      screen, numberedOptions(1, daysInMonth(year, month)), day - 1, 202, 108,
      72, 280);
  editorHourRoller = addSettingsRoller(
      screen, numberedOptions(0, 23), hour, 279, 108, 72, 280);
  editorMinuteRoller = addSettingsRoller(
      screen, numberedOptions(0, 59), minute, 356, 108, 72, 280);
  lv_obj_add_event_cb(editorYearRoller, handleDateRollerChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(editorMonthRoller, handleDateRollerChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(editorDayRoller, handleDateRollerChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(editorHourRoller, handleDateRollerChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(editorMinuteRoller, handleDateRollerChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
}

void renderTimeEditor(lv_obj_t* screen) {
  const bool editingStart =
      activeSettingsEditor == SettingsEditor::ScreenOffStart;
  renderEditorTitle(
      screen,
      editingStart ? uiText("设置息屏时间", "Set screen-off time")
                   : uiText("设置恢复时间", "Set resume time"),
      SettingsAction::SaveTimeEditor);
  addEditorColumnLabel(screen, uiText("小时", "Hour"), 88, 130);
  addEditorColumnLabel(screen, uiText("分钟", "Minute"), 262, 130);
  editorHourRoller = addSettingsRoller(
      screen, numberedOptions(0, 23), timeOfDayDraft.hour, 88, 110, 130, 292);
  editorMinuteRoller = addSettingsRoller(
      screen, numberedOptions(0, 59), timeOfDayDraft.minute, 262, 110, 130,
      292);
  lv_obj_add_event_cb(editorHourRoller, handleTimeRollerChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(editorMinuteRoller, handleTimeRollerChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t* colon = addLabel(screen, ":", &lv_font_montserrat_28, 0xE7FF54,
                             32);
  lv_obj_set_pos(colon, 224, 230);
}

void renderWifiResetConfirmation(lv_obj_t* screen) {
  lv_obj_t* title =
      addLabel(screen, uiText("清除 Wi-Fi？", "Clear Wi-Fi?"),
               &lv_font_montserrat_28, 0xF4EFE6, 410);
  lv_obj_set_pos(title, 35, 82);
  lv_obj_t* body = addLabel(
      screen,
      uiText("保存的网络名称和密码将被删除。设备会重启并返回配网二维码。",
             "The saved network and password will be removed. The device will restart in setup mode."),
      &lv_font_montserrat_16, 0x93A0A5, 400);
  lv_obj_set_pos(body, 40, 150);

  if (wifiResetInProgress) {
    lv_obj_t* pending =
        addLabel(screen, uiText("正在清除并重启…", "Clearing and restarting…"),
                 &lv_font_montserrat_20, 0xE7FF54, 400);
    lv_obj_set_pos(pending, 40, 300);
    return;
  }

  addSettingsButton(screen, uiText("取消", "Cancel"), 40, 348, 180, 54,
                    SettingsAction::CancelEditor);
  lv_obj_t* confirm = addSettingsButton(
      screen, uiText("确认清除", "Clear now"), 260, 348, 180, 54,
      SettingsAction::ConfirmWifiReset);
  styleDangerButton(confirm);
}

void renderDeviceSettings() {
  provisioningScreen = false;
  deviceSettingsScreen = true;
  deviceSettingsDirty = false;
  if ((activeSettingsEditor == SettingsEditor::ScreenOffStart ||
       activeSettingsEditor == SettingsEditor::ScreenOffEnd) &&
      !appConfig.screenOffEnabled()) {
    activeSettingsEditor = SettingsEditor::None;
    activeSettingsSection = SettingsSection::Display;
  }
  lv_obj_t* screen = createScreen(0x101619);

  if (activeSettingsEditor == SettingsEditor::DateTime) {
    renderDateTimeEditor(screen);
  } else if (activeSettingsEditor == SettingsEditor::ScreenOffStart ||
             activeSettingsEditor == SettingsEditor::ScreenOffEnd) {
    renderTimeEditor(screen);
  } else if (activeSettingsEditor == SettingsEditor::WifiReset) {
    renderWifiResetConfirmation(screen);
  } else {
    renderSettingsHeader(screen);
    switch (activeSettingsSection) {
      case SettingsSection::Display:
        renderDisplaySettings(screen);
        break;
      case SettingsSection::Time:
        renderTimeSettings(screen);
        break;
      case SettingsSection::System:
        renderSystemSettings(screen);
        break;
    }
  }

  loadScreen(screen);
  lastClockRefreshAt = millis();
}

void renderTextPage(const ContentPage& page) {
  provisioningScreen = false;
  deviceSettingsScreen = false;
  lv_obj_t* screen = createScreen(page.background);
  addCornerMark(screen, page.foreground);

  const bool hasEmoji = containsEmoji(page.text);
  const String renderedText = hasEmoji ? normalizeEmojiText(page.text) : page.text;
  const lv_font_t* font = &lv_font_montserrat_32;
  if (hasEmoji) {
    font = &ui_font_emoji_32;
  } else if (containsUtf8(renderedText)) {
    font = &lv_font_simsun_16_cjk;
  } else if (renderedText.length() > 180) {
    font = &lv_font_montserrat_20;
  } else if (renderedText.length() > 80) {
    font = &lv_font_montserrat_24;
  } else if (renderedText.length() < 36) {
    font = &lv_font_montserrat_40;
  }

  lv_obj_t* label =
      addLabel(screen, renderedText, font, page.foreground, 410);
  lv_obj_set_style_text_line_space(label, 10, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -8);

  addPlaybackClock(screen, false, page.foreground);
  loadScreen(screen);
}

// GIF playback decodes into a single 4-byte-per-pixel PSRAM buffer that the
// widget owns for as long as the page is on screen. Refusing up front beats
// letting gifdec fail its allocation, which would leave a silent black page.
bool animationFitsInMemory(uint16_t width, uint16_t height) {
  const size_t required = 4u * width * height + 32768u;
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if (largest >= required) {
    return true;
  }
  Serial.printf("[gif] %ux%u needs %u bytes, largest PSRAM block is %u\n",
                width, height, static_cast<unsigned>(required),
                static_cast<unsigned>(largest));
  return false;
}

void renderAnimatedPage(lv_obj_t* screen, const ContentPage& page) {
  uint16_t width = 0;
  uint16_t height = 0;
  if (!mediaInspectGif(page.imagePath, width, height) ||
      width > SCREEN_WIDTH || height > SCREEN_HEIGHT ||
      !animationFitsInMemory(width, height)) {
    Serial.printf("[gif] cannot play %s\n", page.imagePath.c_str());
    lv_obj_t* error = addLabel(
        screen,
        uiText("动图无法播放\n请使用不超过 480 x 480 的 GIF",
               "This GIF cannot be played\nUse a GIF up to 480 x 480"),
        &lv_font_montserrat_20, 0xF4EFE6, 410);
    lv_obj_center(error);
    return;
  }

  const String source = "S:" + page.imagePath;
  lv_obj_t* animation = lv_gif_create(screen);
  lv_gif_set_src(animation, source.c_str());
  lv_obj_center(animation);
  activeAnimation = animation;
}

void renderStillImage(lv_obj_t* screen, const ContentPage& page) {
  const String imageSource = "S:" + page.imagePath;
  CachedImage* cached = loadCachedImage(page.imagePath);
  lv_img_header_t header{};
  lv_res_t decodeResult = LV_RES_INV;
  if (cached) {
    header = cached->descriptor.header;
    decodeResult = LV_RES_OK;
  } else {
    decodeResult = lv_img_decoder_get_info(imageSource.c_str(), &header);
  }
  if (decodeResult == LV_RES_OK &&
      header.w > 0 && header.h > 0 && header.w <= 1024 && header.h <= 1024) {
    lv_obj_t* image = lv_img_create(screen);
    if (cached) {
      lv_img_set_src(image, &cached->descriptor);
    } else {
      lv_img_set_src(image, imageSource.c_str());
    }
    const uint32_t zoomX = (SCREEN_WIDTH * 256UL) / header.w;
    const uint32_t zoomY = (SCREEN_HEIGHT * 256UL) / header.h;
    // Cover the square panel and let the screen clip the longer edge. New web
    // uploads are already cropped to 480 x 480; this also fixes older images.
    lv_img_set_zoom(image, min<uint32_t>(max<uint32_t>(zoomX, zoomY), 65535));
    lv_obj_center(image);
  } else {
    Serial.printf("[image] cannot decode %s\n", page.imagePath.c_str());
    lv_obj_t* error =
        addLabel(screen,
                 uiText("图片无法解码\n请使用不超过 1024 x 1024 的 PNG/JPG",
                        "This image could not be decoded\nUse a PNG or JPG up "
                        "to 1024 x 1024"),
                 &lv_font_montserrat_20, 0xF4EFE6, 410);
    lv_obj_center(error);
  }
  activeCachedImagePath = cached ? cached->path : "";
}

void renderImagePage(const ContentPage& page) {
  provisioningScreen = false;
  deviceSettingsScreen = false;
  lv_obj_t* screen = createScreen(0x050505);

  if (mediaIsAnimatedPath(page.imagePath)) {
    activeCachedImagePath = "";
    renderAnimatedPage(screen, page);
  } else {
    renderStillImage(screen, page);
  }

  addPlaybackClock(screen, true);
  loadScreen(screen);
}

bool isImagePage(size_t pageIndex) {
  if (pageIndex == 0 || pageIndex > appConfig.pageCount()) {
    return false;
  }
  const ContentPage& page = appConfig.page(pageIndex - 1);
  return page.type == PageType::Image;
}

bool isPlayablePage(size_t pageIndex) {
  if (pageIndex == 0 || pageIndex > appConfig.pageCount()) {
    return false;
  }
  const ContentPage& page = appConfig.page(pageIndex - 1);
  return page.type != PageType::Image || mediaExists(page.imagePath);
}

size_t adjacentPage(size_t pageIndex, int8_t direction, size_t pageCount) {
  if (direction > 0) {
    return pageIndex >= pageCount ? 1 : pageIndex + 1;
  }
  return pageIndex <= 1 ? pageCount : pageIndex - 1;
}

// Missing files remain in the saved playlist so reinserting the TF card can
// restore them. Playback treats them as temporarily unavailable and searches
// at most one full cycle for the next page it can actually show.
size_t findPlayablePage(size_t startPage, int8_t direction,
                        bool includeStart) {
  const size_t pageCount = appConfig.pageCount();
  if (pageCount == 0) {
    return 0;
  }
  if (startPage == 0 || startPage > pageCount) {
    startPage = direction > 0 ? 1 : pageCount;
    includeStart = true;
  }

  size_t candidate =
      includeStart ? startPage : adjacentPage(startPage, direction, pageCount);
  for (size_t checked = 0; checked < pageCount; ++checked) {
    if (isPlayablePage(candidate)) {
      return candidate;
    }
    candidate = adjacentPage(candidate, direction, pageCount);
  }
  return 0;
}

uint32_t currentPageDwellMs() {
  if (currentPage == 0 || currentPage > appConfig.pageCount()) {
    return AUTO_ADVANCE_INTERVAL_MS;
  }
  const ContentPage& page = appConfig.page(currentPage - 1);
  return page.type == PageType::Image && mediaIsAnimatedPath(page.imagePath)
             ? ANIMATED_ADVANCE_INTERVAL_MS
             : AUTO_ADVANCE_INTERVAL_MS;
}

void renderCurrent() {
  const size_t total = appConfig.pageCount() + 1;
  if (currentPage >= total) {
    currentPage = total - 1;
  }
  if (currentPage == 0) {
    renderSystemPage();
    activeCachedImagePath = "";
    return;
  }

  const size_t playablePage = findPlayablePage(currentPage, 1, true);
  if (playablePage == 0) {
    playableContentUnavailable = true;
    currentPage = 0;
    renderSystemPage();
    activeCachedImagePath = "";
    return;
  }
  playableContentUnavailable = false;
  currentPage = playablePage;
  resumeContentPage = currentPage;

  const ContentPage& page = appConfig.page(currentPage - 1);
  if (page.type == PageType::Image) {
    renderImagePage(page);
  } else {
    renderTextPage(page);
    activeCachedImagePath = "";
  }
}

void showSystemPage() {
  const size_t pageCount = appConfig.pageCount();
  if (currentPage == 0) {
    if (deviceSettingsScreen) {
      deviceSettingsScreen = false;
      renderSystemPage();
    }
    lastTouchAt = millis();
    return;
  }
  if (pageCount == 0) {
    return;
  }

  resumeContentPage = min(currentPage, pageCount);
  currentPage = 0;
  renderCurrent();
  lastTouchAt = millis();
}

void resumePlayback() {
  const size_t pageCount = appConfig.pageCount();
  if (pageCount == 0) {
    return;
  }

  currentPage = resumeContentPage;
  if (currentPage == 0 || currentPage > pageCount) {
    currentPage = 1;
  }
  renderCurrent();
  lastPageChangeAt = millis();
}

void commitPageChange(int8_t direction) {
  const size_t pageCount = appConfig.pageCount();
  if (pageCount == 0) {
    lastPageChangeAt = millis();
    return;
  }

  size_t playablePage = 0;
  if (currentPage == 0) {
    currentPage = resumeContentPage;
    if (currentPage == 0 || currentPage > pageCount) {
      currentPage = 1;
    }
    playablePage = findPlayablePage(currentPage, direction, true);
  } else {
    playablePage = findPlayablePage(currentPage, direction, false);
  }
  if (playablePage == 0) {
    playableContentUnavailable = true;
    currentPage = 0;
    renderCurrent();
    return;
  }
  currentPage = playablePage;
  resumeContentPage = currentPage;
  renderCurrent();
}

void changePage(int8_t direction, uint32_t startedAt) {
  if (appConfig.pageCount() == 0) {
    lastPageChangeAt = startedAt;
    return;
  }
  if (pageTransitionPhase != PageTransitionPhase::Idle) {
    return;
  }

  pageTransitionDirection = direction > 0 ? 1 : -1;
  pageTransitionPhase = PageTransitionPhase::FadingOut;
  pageTransitionStartedAt = startedAt;
  pageTransitionLastStepAt = pageTransitionStartedAt;
  pageTransitionPercent = 100;
  backlightApply();
}

void updatePageTransition(uint32_t now) {
  if (pageTransitionPhase == PageTransitionPhase::Idle) {
    return;
  }

  if (pageTransitionPhase == PageTransitionPhase::WaitingForFrame) {
    if (now - pageTransitionStartedAt < PAGE_FRAME_SETTLE_MS) {
      return;
    }
    pageTransitionPhase = PageTransitionPhase::FadingIn;
    pageTransitionStartedAt = now;
    pageTransitionLastStepAt = now;
    pageTransitionPercent = 0;
    backlightApply();
    return;
  }

  const uint32_t elapsed = now - pageTransitionStartedAt;
  const uint32_t duration =
      pageTransitionPhase == PageTransitionPhase::FadingOut
          ? PAGE_FADE_OUT_MS
          : PAGE_FADE_IN_MS;
  const bool phaseComplete = elapsed >= duration;
  if (!phaseComplete && now - pageTransitionLastStepAt < PAGE_FADE_STEP_MS) {
    return;
  }
  pageTransitionLastStepAt = now;

  if (pageTransitionPhase == PageTransitionPhase::FadingOut) {
    pageTransitionPercent =
        phaseComplete ? 0 : 100 - easedPercent(elapsed, PAGE_FADE_OUT_MS);
    backlightApply();
    if (!phaseComplete) {
      return;
    }

    commitPageChange(pageTransitionDirection);
    if (!espDisplayStackRefreshNow()) {
      Serial.println("[display] unable to publish page before fade-in");
    }
    pageTransitionPhase = PageTransitionPhase::WaitingForFrame;
    pageTransitionStartedAt = millis();
    pageTransitionLastStepAt = pageTransitionStartedAt;
    return;
  }

  pageTransitionPercent =
      phaseComplete ? 100 : easedPercent(elapsed, PAGE_FADE_IN_MS);
  if (phaseComplete) {
    pageTransitionPhase = PageTransitionPhase::Idle;
    pageTransitionDirection = 0;
    pageTransitionStartedAt = 0;
    pageTransitionLastStepAt = 0;
    lastPageChangeAt = now;
  }
  backlightApply();
}
}  // namespace

bool displayTakeSdRescanRequest() {
  LvglLockGuard guard;
  if (!guard.locked() || !sdRescanRequested || !deviceSettingsScreen) {
    return false;
  }
  sdRescanRequested = false;
  clearMediaCache();
  return true;
}

bool displayTakeWifiResetRequest() {
  LvglLockGuard guard;
  if (!guard.locked() || !wifiResetRequested) {
    return false;
  }
  wifiResetRequested = false;
  return true;
}

void displayReportWifiResetFailure() {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  wifiResetInProgress = false;
  deviceSettingsDirty = true;
}

bool displayBegin() {
  Serial.println("[display] starting ESP-IDF ST7701 display stack");
  Serial.println(
      "[display] RGB pixel clock: 16 MHz, triple-buffered anti-tearing");
  backlightBegin();
  // The three RGB framebuffers need large aligned PSRAM blocks, so let the
  // panel claim them before the image cache takes its share.
  if (!espDisplayStackBegin()) {
    Serial.println("[display] ESP-IDF display stack initialization failed");
    return false;
  }
  if (!imageCacheBegin()) {
    Serial.println("[display] resident image cache allocation failed");
  }

  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readTouch;
  lv_indev_drv_register(&inputDriver);

  lv_fs_drv_init(&fsDriver);
  fsDriver.letter = 'S';
  fsDriver.open_cb = fsOpen;
  fsDriver.close_cb = fsClose;
  fsDriver.read_cb = fsRead;
  fsDriver.seek_cb = fsSeek;
  fsDriver.tell_cb = fsTell;
  lv_fs_drv_register(&fsDriver);

  lv_png_init();
  lv_split_jpeg_init();
  lv_img_cache_set_size(1);
  if (!espDisplayStackStart()) {
    Serial.println("[display] ESP-IDF LVGL worker failed to start");
    return false;
  }
  backlightApply();
  return true;
}

void displaySetBrightness(uint8_t percent) {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  (void)percent;  // The committed value is read back from appConfig.
  refreshScheduledBacklight();
  backlightApply();
}

bool displayGetImageSize(const String& path, uint16_t& width,
                         uint16_t& height) {
  if (path.endsWith(RAW_IMAGE_EXTENSION)) {
    return rawImageInspect(path, width, height);
  }
  if (mediaIsAnimatedPath(path)) {
    return mediaInspectGif(path, width, height);
  }
  LvglLockGuard guard;
  if (!guard.locked()) {
    return false;
  }
  const String source = "S:" + path;
  lv_img_header_t header{};
  if (lv_img_decoder_get_info(source.c_str(), &header) != LV_RES_OK ||
      header.w <= 0 || header.h <= 0 || header.w > 1024 ||
      header.h > 1024) {
    return false;
  }
  width = header.w;
  height = header.h;
  return true;
}

void displayShowBootMessage(const String& title, const String& detail) {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  provisioningScreen = true;
  deviceSettingsScreen = false;
  lv_obj_t* screen = createScreen(0x101619);
  addCornerMark(screen, 0xE7FF54);
  lv_obj_t* titleLabel =
      addLabel(screen, title, &lv_font_montserrat_32, 0xF4EFE6, 410);
  lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -24);
  lv_obj_t* detailLabel =
      addLabel(screen, detail, &lv_font_montserrat_16, 0x93A0A5, 410);
  lv_obj_align(detailLabel, LV_ALIGN_CENTER, 0, 34);
  loadScreen(screen);
}

void displayShowProvisioning(const String& ssid, const String& password,
                             const String& address) {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  provisioningSsid = ssid;
  provisioningPassword = password;
  provisioningAddress = address;
  renderProvisioning();
}

void displayShowContent(size_t pageIndex) {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  if (appConfig.pageCount() == 0) {
    currentPage = 0;
  } else if (pageIndex == 0) {
    currentPage = 1;
  } else {
    currentPage = min(pageIndex, appConfig.pageCount());
  }
  if (currentPage != 0) {
    resumeContentPage = currentPage;
  }
  deviceSettingsScreen = false;
  pendingScreenRequest = ScreenRequest::None;
  cancelPageTransition();
  renderCurrent();
  lastPageChangeAt = millis();
  lastTouchAt = lastPageChangeAt;
}

void displayMarkContentDirty() {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  contentDirty = true;
}

void displayMarkSettingsDirty() {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  if (deviceSettingsScreen) {
    deviceSettingsDirty = true;
  }
}

// A displayed GIF or JPEG keeps its source file open for as long as the page is
// on screen. Unmounting the card underneath those handles would leave the
// decoders reading through a filesystem that no longer exists, so callers that
// are about to disturb a backing store move the panel to a media-free screen
// first.
void displayReleaseMedia() {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  if (!provisioningScreen) {
    currentPage = 0;
    deviceSettingsScreen = false;
    pendingScreenRequest = ScreenRequest::None;
    cancelPageTransition();
    renderSystemPage();
    lastTouchAt = millis();
  }
  clearMediaCache();
}

void displayBeginStorageWrite(bool blankBacklight) {
  storageWriteActive = true;
  if (blankBacklight) {
    storageBlankActive = true;
    backlightResumeAt = 0;
  }
  cancelPageTransition();
  if (blankBacklight) {
    backlightApply();
  }
  if (!espDisplayStackPause()) {
    Serial.println("[display] unable to pause LVGL worker for storage write");
  }
}

void displayEndStorageWrite() {
  storageWriteActive = false;
  if (storageBlankActive) {
    storageBlankActive = false;
    backlightResumeAt = millis() + STORAGE_REVEAL_DELAY_MS;
    if (backlightResumeAt == 0) {
      backlightResumeAt = 1;
    }
  } else {
    backlightApply();
  }
  if (!espDisplayStackResume()) {
    Serial.println("[display] unable to resume LVGL worker after storage write");
  }
}

void displayLoop() {
  if (storageWriteActive) {
    return;
  }

  bool saveSettings = false;
  {
    LvglLockGuard guard;
    if (!guard.locked()) {
      return;
    }
    if (settingsSavePending &&
        static_cast<int32_t>(millis() - settingsSaveDueAt) >= 0) {
      settingsSavePending = false;
      saveSettings = true;
    }
  }
  if (saveSettings) {
    // A settings commit is a single small file. Keeping the panel lit for it
    // is what removes the black flash that used to follow every tap.
    displayBeginStorageWrite(false);
    const bool saved = appConfig.save();
    displayEndStorageWrite();
    if (!saved) {
      Serial.println("[settings] unable to save display settings");
      LvglLockGuard retryGuard;
      if (retryGuard.locked() && !settingsSavePending &&
          settingsSaveRetryCount < SETTINGS_SAVE_MAX_RETRIES) {
        ++settingsSaveRetryCount;
        settingsSavePending = true;
        settingsSaveDueAt = millis() + SETTINGS_SAVE_RETRY_MS;
      }
    }
  }

  // esp_lvgl_adapter owns the LVGL tick and timer handler. Application-side
  // mutations are serialized with the same recursive mutex.
  LvglLockGuard guard;
  if (!guard.locked()) {
    return;
  }
  const uint32_t now = millis();

  bool contentWasRendered = false;
  if (contentDirty && !provisioningScreen) {
    contentDirty = false;
    // A storage or playlist change may make previously skipped pages usable.
    // Let the next playback render re-evaluate availability even if settings
    // currently owns the screen.
    playableContentUnavailable = false;
    lv_img_cache_invalidate_src(nullptr);
    dropUnreferencedCachedImages();
    const size_t pageCount = appConfig.pageCount();
    if (pageCount == 0) {
      currentPage = 0;
      resumeContentPage = 1;
    } else {
      if (currentPage == 0 || currentPage > pageCount) {
        currentPage = min(resumeContentPage, pageCount);
        if (currentPage == 0) {
          currentPage = 1;
        }
      }
      resumeContentPage = currentPage;
    }
    cancelPageTransition();
    if (deviceSettingsScreen) {
      renderDeviceSettings();
    } else {
      renderCurrent();
    }
    lastPageChangeAt = millis();
    contentWasRendered = true;
  }

  if (pendingScreenRequest != ScreenRequest::None) {
    const ScreenRequest request = pendingScreenRequest;
    pendingScreenRequest = ScreenRequest::None;
    if (!provisioningScreen) {
      deviceSettingsDirty = false;
      cancelPageTransition();
      if (request == ScreenRequest::DeviceSettings) {
        renderDeviceSettings();
      } else {
        renderSystemPage();
      }
      lastTouchAt = millis();
      lastPageChangeAt = lastTouchAt;
      contentWasRendered = true;
    }
  }

  if (deviceSettingsDirty && !provisioningScreen) {
    deviceSettingsDirty = false;
    cancelPageTransition();
    if (deviceSettingsScreen) {
      renderDeviceSettings();
    } else {
      renderCurrent();
    }
    lastPageChangeAt = millis();
    contentWasRendered = true;
  }

  if (pendingSystemPage && !provisioningScreen) {
    pendingSystemPage = false;
    pendingSwipe = 0;
    cancelPageTransition();
    showSystemPage();
    contentWasRendered = true;
  } else if (pendingSwipe != 0 && !provisioningScreen) {
    const int8_t direction = pendingSwipe;
    pendingSwipe = 0;
    changePage(direction, now);
  }

  updatePageTransition(now);
  if (scheduledScreenOff && scheduledBacklightFade.update(now)) {
    backlightApply();
  }

  if (!contentWasRendered && !provisioningScreen &&
      pageTransitionPhase == PageTransitionPhase::Idle &&
      deviceSettingsScreen &&
      now - lastTouchAt >= SETTINGS_IDLE_TIMEOUT_MS) {
    deviceSettingsScreen = false;
    activeSettingsEditor = SettingsEditor::None;
    if (appConfig.pageCount() > 0) {
      resumePlayback();
    } else {
      renderSystemPage();
    }
    contentWasRendered = true;
  } else if (!contentWasRendered && !provisioningScreen &&
             pageTransitionPhase == PageTransitionPhase::Idle &&
             !playableContentUnavailable &&
             appConfig.pageCount() > 0 && currentPage == 0 &&
             now - lastTouchAt >= SYSTEM_PAGE_IDLE_TIMEOUT_MS) {
    resumePlayback();
    contentWasRendered = true;
  } else if (!contentWasRendered && !provisioningScreen &&
             pageTransitionPhase == PageTransitionPhase::Idle &&
             appConfig.pageCount() > 0 && currentPage != 0 &&
             now - lastPageChangeAt >= currentPageDwellMs()) {
    changePage(1, now);
  }

  if (now - lastClockRefreshAt >= CLOCK_REFRESH_INTERVAL_MS) {
    lastClockRefreshAt = now;
    updateLiveClock();
    refreshScheduledBacklight();
  }

  // Warm the neighbouring pages only once the current one has settled. This
  // still runs under the LVGL lock -- the cache is shared with the render
  // path -- but by then the frame is static, so blocking the worker for the
  // duration of one 450 KB read costs nothing visible. Signed comparisons
  // because the branches above may have moved lastPageChangeAt past `now`.
  const uint32_t preloadNow = millis();
  if (!contentWasRendered && !provisioningScreen && pendingSwipe == 0 &&
      !pendingSystemPage &&
      pageTransitionPhase == PageTransitionPhase::Idle &&
      static_cast<int32_t>(preloadNow - lastPageChangeAt) >=
          static_cast<int32_t>(IMAGE_PRELOAD_IDLE_DELAY_MS) &&
      static_cast<int32_t>(preloadNow - lastPreloadAt) >=
          static_cast<int32_t>(IMAGE_PRELOAD_MIN_INTERVAL_MS) &&
      preloadOneNeighbourImage(currentPage)) {
    lastPreloadAt = millis();
  }

  if (!scheduledScreenOff && !storageBlankActive && backlightResumeAt != 0 &&
      static_cast<int32_t>(now - backlightResumeAt) >= 0) {
    backlightResumeAt = 0;
    backlightApply();
  }

  if (!contentWasRendered) {
    updatePlaybackClockPosition(millis());
  }
}

size_t displayCurrentPage() {
  LvglLockGuard guard;
  if (!guard.locked()) {
    return 0;
  }
  return currentPage;
}
