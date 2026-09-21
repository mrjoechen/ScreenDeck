#include "web_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "display_ui.h"
#include "llm_narration.h"
#include "media_store.h"
#include "raw_image.h"
#include "screendeck_version.h"
#include "web_ui.h"

namespace {
constexpr uint16_t DNS_PORT = 53;
constexpr size_t MAX_UPLOAD_BYTES = 2 * 1024 * 1024;
constexpr int64_t LOCAL_TIME_MIN_EPOCH = 1577836800LL;
constexpr int64_t LOCAL_TIME_MAX_EPOCH_EXCLUSIVE = 4133980800LL;

WebServer server(80);
DNSServer dnsServer;
uint8_t* uploadBuffer = nullptr;
String uploadFilename;
String uploadPath;
bool uploadValid = false;
size_t uploadBytes = 0;
bool uploadDisplayPaused = false;
char llmSettingsToken[17] = "";
uint32_t llmConfigRevision = 1;

void releaseUploadBuffer() {
  if (!uploadBuffer) {
    return;
  }
  heap_caps_free(uploadBuffer);
  uploadBuffer = nullptr;
}

void finishUploadDisplay() {
  if (!uploadDisplayPaused) {
    return;
  }
  uploadDisplayPaused = false;
  displayEndStorageWrite();
}

bool commitUploadBuffer() {
  if (!uploadValid || !uploadBuffer || uploadBytes == 0 ||
      uploadFilename.isEmpty()) {
    releaseUploadBuffer();
    return false;
  }

  // Network reception only touches PSRAM. Pause playback for the much shorter
  // transactional commit to managed SD storage or its LittleFS fallback.
  displayBeginStorageWrite();
  uploadDisplayPaused = true;
  uploadPath =
      mediaStoreCommitUpload(uploadFilename, uploadBuffer, uploadBytes);
  uploadValid = !uploadPath.isEmpty();
  releaseUploadBuffer();
  return uploadValid;
}

void sendJson(int status, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.send(status, "application/json; charset=utf-8", body);
}

void sendOk(const String& extra = "") {
  sendJson(200, extra.isEmpty() ? "{\"ok\":true}" : extra);
}

void sendError(int status, const String& message) {
  DynamicJsonDocument doc(512);
  doc["ok"] = false;
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  sendJson(status, body);
}

void sendLocalizedError(int status, const char* chinese,
                        const char* english) {
  sendError(status, appConfig.language() == InterfaceLanguage::Chinese
                        ? chinese
                        : english);
}

uint32_t parseColor(String value, uint32_t fallback) {
  value.trim();
  if (value.startsWith("#")) {
    value.remove(0, 1);
  }
  if (value.length() != 6) {
    return fallback;
  }
  char* end = nullptr;
  const uint32_t parsed = strtoul(value.c_str(), &end, 16);
  return end && *end == '\0' ? parsed : fallback;
}

String safeUploadFilename(String original) {
  original.replace("\\", "/");
  const int slash = original.lastIndexOf('/');
  if (slash >= 0) {
    original = original.substring(slash + 1);
  }
  original.toLowerCase();
  const int dot = original.lastIndexOf('.');
  if (dot < 0) {
    return "";
  }
  String extension = original.substring(dot);
  if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" &&
      extension != ".gif" && extension != RAW_IMAGE_EXTENSION) {
    return "";
  }
  // LVGL 8.3's split-JPEG decoder recognizes .jpg but not .jpeg.
  if (extension == ".jpeg") {
    extension = ".jpg";
  }

  String stem;
  for (int i = 0; i < dot && stem.length() < 32; ++i) {
    const char c = original[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
      stem += c;
    } else if (c == ' ') {
      stem += '-';
    }
  }
  if (stem.isEmpty()) {
    stem = "image";
  }
  return String(millis(), HEX) + "-" + stem + extension;
}

bool hasExpectedImageSignature(const String& path) {
  if (path.endsWith(RAW_IMAGE_EXTENSION)) {
    uint16_t width = 0;
    uint16_t height = 0;
    return rawImageInspect(path, width, height);
  }
  if (mediaIsAnimatedPath(path)) {
    uint16_t width = 0;
    uint16_t height = 0;
    return mediaInspectGif(path, width, height);
  }
  File file = mediaOpen(path);
  if (!file || file.size() < 8) {
    if (file) {
      file.close();
    }
    return false;
  }
  uint8_t signature[8] = {};
  const size_t read = file.read(signature, sizeof(signature));
  file.close();
  if (read != sizeof(signature)) {
    return false;
  }
  if (path.endsWith(".png")) {
    static constexpr uint8_t PNG_SIGNATURE[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return memcmp(signature, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) == 0;
  }
  return path.endsWith(".jpg") && signature[0] == 0xFF &&
         signature[1] == 0xD8 && signature[2] == 0xFF;
}

String mimeForPath(const String& path) {
  String lowered(path);
  lowered.toLowerCase();
  if (lowered.endsWith(".png")) {
    return "image/png";
  }
  if (lowered.endsWith(".jpg") || lowered.endsWith(".jpeg")) {
    return "image/jpeg";
  }
  if (lowered.endsWith(".gif")) {
    return "image/gif";
  }
  return "application/octet-stream";
}

bool serveMedia(const String& uri) {
  if (!uri.startsWith("/media/")) {
    return false;
  }
  String filename = uri.substring(7);
  filename.replace("..", "");
  const String path = "/img/" + filename;
  if (!LittleFS.exists(path)) {
    return false;
  }
  File file = LittleFS.open(path, FILE_READ);
  server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server.streamFile(file, mimeForPath(path));
  file.close();
  return true;
}

// TF-card media is addressed by its full page path instead of a filename, so
// it arrives as a query argument rather than a URL segment.
bool isSafeSdPath(const String& path) {
  return mediaIsSdPath(path) && path.indexOf("..") < 0 &&
         mediaIsSupportedExtension(path);
}

bool containsWhitespaceOrControl(const String& value) {
  for (size_t index = 0; index < value.length(); ++index) {
    if (static_cast<uint8_t>(value[index]) <= 0x20) {
      return true;
    }
  }
  return false;
}

class ScopedStringWipe {
 public:
  explicit ScopedStringWipe(String& value) : value_(value) {}
  ~ScopedStringWipe() {
    volatile char* bytes = value_.begin();
    for (size_t index = 0; bytes && index < value_.length(); ++index) {
      bytes[index] = '\0';
    }
    value_.remove(0);
  }

  ScopedStringWipe(const ScopedStringWipe&) = delete;
  ScopedStringWipe& operator=(const ScopedStringWipe&) = delete;

 private:
  String& value_;
};
}  // namespace

WebPortal webPortal;

void WebPortal::begin(bool provisioningMode) {
  provisioning_ = provisioningMode;
  snprintf(llmSettingsToken, sizeof(llmSettingsToken), "%08X%08X",
           static_cast<unsigned>(esp_random()),
           static_cast<unsigned>(esp_random()));

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
  });

  server.on("/api/status", HTTP_GET, [this]() {
    mediaStoreEnsureSdMounted();
    // Include the full UTF-8 prompt as well as the existing status fields.
    DynamicJsonDocument doc(4096 + MAX_LLM_NARRATION_PROMPT_BYTES);
    doc["ok"] = true;
    doc["mode"] = provisioning_ ? "provisioning" : "online";
    doc["firmwareVersion"] = SCREENDECK_VERSION;
    doc["firmwareBuildTime"] = SCREENDECK_BUILD_TIME;
    doc["firmwareChannel"] = SCREENDECK_CHANNEL;
    doc["ssid"] = provisioning_ ? WiFi.softAPSSID() : WiFi.SSID();
    const String ip =
        provisioning_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["ip"] = ip;
    doc["url"] = "http://" + ip + "/";
    doc["rssi"] = provisioning_ ? 0 : WiFi.RSSI();
    doc["brightness"] = appConfig.brightness();
    doc["language"] = appConfig.languageCode();
    doc["pageTransition"] = appConfig.pageTransitionStyleCode();
    doc["timezoneOffsetMinutes"] = appConfig.timezoneOffsetMinutes();
    doc["showDateTime"] = appConfig.showDateTime();
    doc["showWeather"] = appConfig.showWeather();
    doc["screenOffEnabled"] = appConfig.screenOffEnabled();
    doc["screenOffStartMinutes"] = appConfig.screenOffStartMinutes();
    doc["screenOffEndMinutes"] = appConfig.screenOffEndMinutes();
    doc["llmBaseUrl"] = appConfig.llmBaseUrl();
    doc["llmModel"] = appConfig.llmModel();
    doc["llmNarrationPrompt"] = appConfig.llmNarrationPrompt();
    doc["llmApiKeyConfigured"] = !appConfig.llmApiKey().isEmpty();
    doc["llmConfigured"] = appConfig.llmConfigured();
    doc["imageNarrationEnabled"] = appConfig.imageNarrationEnabled();
    doc["llmSettingsToken"] = llmSettingsToken;
    doc["llmConfigRevision"] = llmConfigRevision;
    doc["epoch"] = static_cast<uint64_t>(time(nullptr));
    doc["storageTotal"] = LittleFS.totalBytes();
    doc["storageUsed"] = LittleFS.usedBytes();
    doc["pageCount"] = appConfig.pageCount();
    doc["sdMounted"] = mediaStoreSdMounted();
    doc["sdStatus"] = mediaStoreSdStatusCode();
    doc["sdFilesystem"] = mediaStoreSdFilesystemName();
    doc["sdSupportedFilesystems"] = mediaStoreSdSupportedFilesystems();
    doc["sdTotal"] = mediaStoreSdTotalBytes();
    doc["sdUsed"] = mediaStoreSdUsedBytes();
    doc["sdClockHz"] = mediaStoreSdClockHz();
    String body;
    serializeJson(doc, body);
    sendJson(200, body);
  });

  server.on("/api/pages", HTTP_GET, []() {
    sendJson(200, "{\"ok\":true," +
                      appConfig.toJson().substring(1));
  });

  server.on("/api/pages/text", HTTP_POST, []() {
    const String text = server.arg("text");
    const uint32_t background =
        parseColor(server.arg("background"), 0x111719);
    const uint32_t foreground =
        parseColor(server.arg("foreground"), 0xF4EFE6);
    displayBeginStorageWrite(false);
    const bool added = appConfig.addText(text, background, foreground);
    displayEndStorageWrite();
    if (!added) {
      sendLocalizedError(400, "文字为空、过长，或页面数量已达到上限",
                         "Text is empty, too long, or the page limit was reached");
      return;
    }
    displayMarkContentDirty();
    sendOk();
  });

  server.on("/api/pages/delete", HTTP_POST, []() {
    if (llmNarrationMediaReadInProgress()) {
      sendLocalizedError(409, "正在读取图片生成旁白，请稍后再试",
                         "An image is being prepared for narration; try again shortly");
      return;
    }
    displayBeginStorageWrite(false);
    const bool removed = appConfig.removePage(server.arg("id").toInt());
    displayEndStorageWrite();
    if (!removed) {
      sendLocalizedError(404, "未找到页面", "Page not found");
      return;
    }
    displayMarkContentDirty();
    sendOk();
  });

  server.on("/api/pages/move", HTTP_POST, []() {
    displayBeginStorageWrite(false);
    const bool moved = appConfig.movePage(server.arg("id").toInt(),
                                          server.arg("direction").toInt());
    displayEndStorageWrite();
    if (!moved) {
      sendLocalizedError(400, "页面已经位于边界",
                         "The page is already at the edge");
      return;
    }
    displayMarkContentDirty();
    sendOk();
  });

  server.on(
      "/api/upload", HTTP_POST,
      []() {
        if (!uploadValid || uploadPath.isEmpty() ||
            !mediaExists(uploadPath)) {
          finishUploadDisplay();
          sendLocalizedError(
              400, "上传失败，仅支持不超过 2 MB 的 RGB565/PNG/JPG/GIF",
              "Upload failed; use an RGB565, PNG, JPG, or GIF file up to 2 MB");
          return;
        }
        uint16_t imageWidth = 0;
        uint16_t imageHeight = 0;
        if (!hasExpectedImageSignature(uploadPath) ||
            !displayGetImageSize(uploadPath, imageWidth, imageHeight)) {
          mediaRemove(uploadPath);
          uploadValid = false;
          finishUploadDisplay();
          sendLocalizedError(
              400, "图片无法解码，请通过管理网页上传 PNG/JPG/GIF",
              "The image could not be decoded; upload a PNG, JPG, or GIF here");
          return;
        }
        // Animations are played at their native size, so anything larger than
        // the panel would be cropped and would also cost more PSRAM than the
        // decoder can be given.
        if (mediaIsAnimatedPath(uploadPath) &&
            (imageWidth > 480 || imageHeight > 480)) {
          mediaRemove(uploadPath);
          uploadValid = false;
          finishUploadDisplay();
          sendLocalizedError(400, "动图尺寸请不要超过 480 x 480",
                             "Animated GIFs must be 480 x 480 or smaller");
          return;
        }
        if (!appConfig.addImage(uploadPath)) {
          mediaRemove(uploadPath);
          finishUploadDisplay();
          sendLocalizedError(400,
                             "无法添加图片，页面数量可能已达到上限",
                             "The image could not be added; the page limit may "
                             "have been reached");
          return;
        }
        Serial.printf("[upload] image accepted: %s (%ux%u, %u bytes)\n",
                      uploadPath.c_str(), imageWidth, imageHeight,
                      static_cast<unsigned>(uploadBytes));
        displayMarkContentDirty();
        finishUploadDisplay();
        sendOk();
      },
      []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          finishUploadDisplay();
          releaseUploadBuffer();
          uploadFilename = safeUploadFilename(upload.filename);
          uploadPath = "";
          uploadBytes = 0;
          uploadValid = !uploadFilename.isEmpty() &&
                        appConfig.pageCount() < MAX_CONTENT_PAGES;
          if (uploadValid) {
            uploadBuffer = static_cast<uint8_t*>(heap_caps_malloc(
                MAX_UPLOAD_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            uploadValid = uploadBuffer != nullptr;
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (upload.currentSize > MAX_UPLOAD_BYTES - uploadBytes) {
            uploadValid = false;
          } else if (uploadValid && uploadBuffer) {
            memcpy(uploadBuffer + uploadBytes, upload.buf, upload.currentSize);
            uploadBytes += upload.currentSize;
          }
        } else if (upload.status == UPLOAD_FILE_END ||
                   upload.status == UPLOAD_FILE_ABORTED) {
          if (upload.status == UPLOAD_FILE_ABORTED || !uploadValid) {
            releaseUploadBuffer();
            uploadValid = false;
            finishUploadDisplay();
          } else {
            uploadValid = commitUploadBuffer();
          }
        }
      });

  server.on("/api/brightness", HTTP_POST, []() {
    const int value = constrain(server.arg("value").toInt(), 5, 100);
    // While the slider is being dragged the level is applied to the backlight
    // only. Persisting every intermediate value would hammer flash and make
    // the preview lag behind the thumb.
    const bool persist = server.arg("preview") != "1";
    appConfig.setBrightness(value);
    displaySetBrightness(value);
    if (!persist) {
      sendOk();
      return;
    }
    displayBeginStorageWrite(false);
    const bool saved = appConfig.save();
    displayEndStorageWrite();
    if (!saved) {
      sendLocalizedError(500, "亮度设置保存失败",
                         "Unable to save the brightness setting");
      return;
    }
    displayMarkSettingsDirty();
    sendOk();
  });

  server.on("/api/language", HTTP_POST, []() {
    const String language = server.arg("language");
    if (language != "zh" && language != "en") {
      sendLocalizedError(400, "不支持的界面语言",
                         "Unsupported interface language");
      return;
    }

    const InterfaceLanguage previous = appConfig.language();
    displayBeginStorageWrite(false);
    appConfig.setLanguage(language == "en" ? InterfaceLanguage::English
                                            : InterfaceLanguage::Chinese);
    const bool saved = appConfig.save();
    displayEndStorageWrite();
    if (!saved) {
      appConfig.setLanguage(previous);
      sendLocalizedError(500, "语言设置保存失败",
                         "Unable to save the language setting");
      return;
    }
    displayMarkContentDirty();
    sendOk();
  });

  server.on("/api/page-transition", HTTP_POST, []() {
    const String style = server.arg("pageTransition");
    if (style != "fade" && style != "slide") {
      sendLocalizedError(400, "不支持的切换动画",
                         "Unsupported page animation");
      return;
    }

    const PageTransitionStyle previous = appConfig.pageTransitionStyle();
    displayBeginStorageWrite(false);
    appConfig.setPageTransitionStyle(style == "slide"
                                         ? PageTransitionStyle::SlideHorizontal
                                         : PageTransitionStyle::FadeThroughBlack);
    const bool saved = appConfig.save();
    displayEndStorageWrite();
    if (!saved) {
      appConfig.setPageTransitionStyle(previous);
      sendLocalizedError(500, "切换动画保存失败",
                         "Unable to save the page animation");
      return;
    }
    displayMarkSettingsDirty();
    sendOk();
  });

  server.on("/api/llm", HTTP_POST, []() {
    if (server.arg("csrfToken") != llmSettingsToken) {
      sendLocalizedError(403, "设置页面已过期，请刷新后重试",
                         "The settings page expired; refresh and try again");
      return;
    }
    const String previousBaseUrl = appConfig.llmBaseUrl();
    String previousApiKey = appConfig.llmApiKey();
    const String previousModel = appConfig.llmModel();
    const String previousNarrationPrompt = appConfig.llmNarrationPrompt();
    ScopedStringWipe previousApiKeyWipe(previousApiKey);

    String baseUrl;
    String apiKey;
    String model;
    String narrationPrompt = previousNarrationPrompt;
    ScopedStringWipe apiKeyWipe(apiKey);
    if (server.arg("clear") != "1") {
      baseUrl = server.arg("baseUrl");
      apiKey = server.arg("apiKey");
      model = server.arg("model");
      if (server.hasArg("narrationPrompt")) {
        narrationPrompt = server.arg("narrationPrompt");
      }
      baseUrl.trim();
      model.trim();
      narrationPrompt.trim();
      while (baseUrl.endsWith("/")) {
        baseUrl.remove(baseUrl.length() - 1);
      }
      if (apiKey.isEmpty()) {
        if (baseUrl != previousBaseUrl || model != previousModel) {
          sendLocalizedError(
              400, "修改 Base URL 或模型时必须重新输入 API Key",
              "Re-enter the API key when changing the Base URL or model");
          return;
        }
        apiKey = previousApiKey;
      }
      const bool validUrl =
          baseUrl.startsWith("https://") ||
          (baseUrl.startsWith("http://") &&
           server.arg("allowInsecureHttp") == "1");
      if (!validUrl || baseUrl.length() >= MAX_LLM_BASE_URL_BYTES ||
          apiKey.isEmpty() || apiKey.length() >= MAX_LLM_API_KEY_BYTES ||
          model.isEmpty() || model.length() >= MAX_LLM_MODEL_BYTES ||
          containsWhitespaceOrControl(baseUrl) ||
          containsWhitespaceOrControl(apiKey) ||
          containsWhitespaceOrControl(model)) {
        sendLocalizedError(
            400,
            "LLM 配置无效，请填写 http(s) Base URL、API Key 和模型",
            "Invalid LLM settings; enter an HTTP(S) Base URL, API key, and model");
        return;
      }
      if (!validLlmNarrationPrompt(narrationPrompt)) {
        sendLocalizedError(
            400, "图片摘要提示词不能为空或过长",
            "The image summary prompt cannot be empty or too long");
        return;
      }
    }

    displayBeginStorageWrite(false);
    bool changed = false;
    if (server.arg("clear") == "1") {
      appConfig.clearLlmConfig();
      changed = true;
    } else {
      changed = appConfig.setLlmConfig(baseUrl, apiKey, model);
      changed = changed && appConfig.setLlmNarrationPrompt(narrationPrompt);
    }
    const bool saved = changed && appConfig.save();
    displayEndStorageWrite();
    if (!saved) {
      // Restore the complete in-memory snapshot if the single persistence
      // write fails. clearLlmConfig also resets the prompt before restoration.
      appConfig.clearLlmConfig();
      if (!previousBaseUrl.isEmpty() && !previousApiKey.isEmpty() &&
          !previousModel.isEmpty()) {
        appConfig.setLlmConfig(previousBaseUrl, previousApiKey, previousModel);
      }
      appConfig.setLlmNarrationPrompt(previousNarrationPrompt);
      sendLocalizedError(500, "LLM 设置保存失败",
                         "Unable to save the LLM settings");
      return;
    }
    displayMarkContentDirty();
    ++llmConfigRevision;
    if (llmConfigRevision == 0) {
      llmConfigRevision = 1;
    }
    DynamicJsonDocument doc(128);
    doc["ok"] = true;
    doc["llmConfigRevision"] = llmConfigRevision;
    String body;
    serializeJson(doc, body);
    sendJson(200, body);
  });

  server.on("/api/llm/test", HTTP_POST, []() {
    if (server.arg("csrfToken") != llmSettingsToken) {
      sendLocalizedError(403, "设置页面已过期，请刷新后重试",
                         "The settings page expired; refresh and try again");
      return;
    }

    const String requestToken = server.arg("requestToken");
    if (!llmConfigTestRequestTokenValid(requestToken.c_str())) {
      sendLocalizedError(400, "测试请求标识无效，请重新测试",
                         "Invalid test request identifier; run the test again");
      return;
    }

    // Treat an exact token replay as the same request. This keeps a retried
    // POST from creating another job after the first response was lost.
    LlmConfigTestStatus existingStatus;
    if (llmNarrationTestStatusByRequestToken(requestToken, existingStatus)) {
      DynamicJsonDocument doc(256);
      doc["ok"] = true;
      doc["id"] = existingStatus.id;
      doc["state"] = llmConfigTestStateCode(existingStatus.state);
      doc["reason"] = llmConfigTestFailureCode(existingStatus.failure);
      doc["httpStatus"] = existingStatus.httpStatus;
      String body;
      serializeJson(doc, body);
      sendJson(200, body);
      return;
    }

    // A test deliberately uses the form's current values without changing the
    // saved triple. An omitted key can only refer to the stored secret while
    // the public parts of that triple remain unchanged.
    const String previousBaseUrl = appConfig.llmBaseUrl();
    String previousApiKey = appConfig.llmApiKey();
    const String previousModel = appConfig.llmModel();
    const String previousNarrationPrompt = appConfig.llmNarrationPrompt();
    ScopedStringWipe previousApiKeyWipe(previousApiKey);
    String baseUrl = server.arg("baseUrl");
    String apiKey = server.arg("apiKey");
    String model = server.arg("model");
    String narrationPrompt = previousNarrationPrompt;
    if (server.hasArg("narrationPrompt")) {
      narrationPrompt = server.arg("narrationPrompt");
    }
    ScopedStringWipe apiKeyWipe(apiKey);
    baseUrl.trim();
    model.trim();
    narrationPrompt.trim();
    while (baseUrl.endsWith("/")) {
      baseUrl.remove(baseUrl.length() - 1);
    }
    if (apiKey.isEmpty()) {
      if (baseUrl != previousBaseUrl || model != previousModel) {
        sendLocalizedError(
            400, "修改 Base URL 或模型时必须重新输入 API Key",
            "Re-enter the API key when changing the Base URL or model");
        return;
      }
      apiKey = previousApiKey;
    }

    const bool validUrl =
        baseUrl.startsWith("https://") ||
        (baseUrl.startsWith("http://") &&
         server.arg("allowInsecureHttp") == "1");
    if (!validUrl || baseUrl.length() >= MAX_LLM_BASE_URL_BYTES ||
        apiKey.isEmpty() || apiKey.length() >= MAX_LLM_API_KEY_BYTES ||
        model.isEmpty() || model.length() >= MAX_LLM_MODEL_BYTES ||
        containsWhitespaceOrControl(baseUrl) ||
        containsWhitespaceOrControl(apiKey) ||
        containsWhitespaceOrControl(model)) {
      sendLocalizedError(
          400, "LLM 配置无效，请填写 http(s) Base URL、API Key 和模型",
          "Invalid LLM settings; enter an HTTP(S) Base URL, API key, and model");
      return;
    }
    if (!validLlmNarrationPrompt(narrationPrompt)) {
      sendLocalizedError(
          400, "图片摘要提示词不能为空或过长",
          "The image summary prompt cannot be empty or too long");
      return;
    }

    if (llmNarrationTestBusy()) {
      sendLocalizedError(409, "已有 LLM 配置测试正在进行，请稍候",
                         "An LLM configuration test is already in progress");
      return;
    }
    uint32_t testId = 0;
    if (!llmNarrationTestStart(baseUrl, apiKey, model, narrationPrompt,
                               requestToken, testId)) {
      sendLocalizedError(503, "设备暂时无法启动 LLM 配置测试，请稍后重试",
                         "The device cannot start the LLM configuration test; try again shortly");
      return;
    }

    DynamicJsonDocument doc(192);
    doc["ok"] = true;
    doc["id"] = testId;
    doc["state"] = llmConfigTestStateCode(LlmConfigTestState::Queued);
    String body;
    serializeJson(doc, body);
    sendJson(202, body);
  });

  server.on("/api/llm/test", HTTP_GET, []() {
    const String requestToken = server.arg("requestToken");
    const String idArgument = server.arg("id");
    const bool tokenQuery = !requestToken.isEmpty();
    const bool validToken =
        tokenQuery && llmConfigTestRequestTokenValid(requestToken.c_str());
    bool validId = !idArgument.isEmpty();
    for (size_t index = 0; validId && index < idArgument.length(); ++index) {
      validId = idArgument[index] >= '0' && idArgument[index] <= '9';
    }
    char* idEnd = nullptr;
    const uint64_t parsedId =
        validId ? strtoull(idArgument.c_str(), &idEnd, 10) : 0;
    validId = validId && idEnd && *idEnd == '\0' && parsedId != 0 &&
              parsedId <= UINT32_MAX;
    const uint32_t testId = static_cast<uint32_t>(parsedId);

    LlmConfigTestStatus status;
    bool available = false;
    if (validToken) {
      available =
          llmNarrationTestStatusByRequestToken(requestToken, status);
      if (available && !idArgument.isEmpty()) {
        available = validId && status.id == testId;
      }
    } else if (!tokenQuery && validId) {
      available = llmNarrationTestStatus(testId, status);
    }

    if (!available) {
      // Keep polling failures inside the same fixed, non-sensitive schema.
      status.id = !tokenQuery && validId ? testId : 0;
      status.state = LlmConfigTestState::Failed;
      status.failure = LlmConfigTestFailure::Internal;
      status.httpStatus = 0;
      DynamicJsonDocument doc(256);
      doc["ok"] = false;
      doc["id"] = status.id;
      doc["state"] = llmConfigTestStateCode(status.state);
      doc["reason"] = llmConfigTestFailureCode(status.failure);
      doc["httpStatus"] = status.httpStatus;
      String body;
      serializeJson(doc, body);
      sendJson(tokenQuery && !validToken ? 400 : 404, body);
      return;
    }

    DynamicJsonDocument doc(256);
    doc["ok"] = true;
    doc["id"] = status.id;
    doc["state"] = llmConfigTestStateCode(status.state);
    doc["reason"] = llmConfigTestFailureCode(status.failure);
    doc["httpStatus"] = status.httpStatus;
    String body;
    serializeJson(doc, body);
    sendJson(200, body);
  });

  server.on("/api/llm/narration", HTTP_POST, []() {
    if (server.arg("csrfToken") != llmSettingsToken) {
      sendLocalizedError(403, "设置页面已过期，请刷新后重试",
                         "The settings page expired; refresh and try again");
      return;
    }

    const String enabledArgument = server.arg("enabled");
    if (enabledArgument != "0" && enabledArgument != "1") {
      sendLocalizedError(400, "图片摘要开关值无效",
                         "Invalid image summary setting");
      return;
    }

    const bool previous = appConfig.imageNarrationEnabled();
    const bool enabled = enabledArgument == "1";
    displayBeginStorageWrite(false);
    appConfig.setImageNarrationEnabled(enabled);
    const bool saved = appConfig.save();
    displayEndStorageWrite();
    if (!saved) {
      appConfig.setImageNarrationEnabled(previous);
      sendLocalizedError(500, "图片摘要设置保存失败",
                         "Unable to save the image summary setting");
      return;
    }
    displayMarkContentDirty();
    sendOk();
  });

  server.on("/api/llm/narration/reset", HTTP_POST, []() {
    if (server.arg("csrfToken") != llmSettingsToken) {
      sendLocalizedError(403, "设置页面已过期，请刷新后重试",
                         "The settings page expired; refresh and try again");
      return;
    }
    if (server.arg("confirmed") != "1") {
      sendLocalizedError(400, "请先确认重置所有图片摘要",
                         "Confirm resetting all image summaries first");
      return;
    }
    displayBeginStorageWrite(false);
    const bool saved = appConfig.clearImageNarrations();
    if (saved) {
      llmNarrationInvalidateAll();
    }
    displayEndStorageWrite();
    if (!saved) {
      sendLocalizedError(500, "图片摘要重置失败，原摘要已保留",
                         "Unable to reset image summaries; the cache was kept");
      return;
    }
    displayMarkContentDirty();
    sendOk();
  });

  server.on("/api/settings", HTTP_POST, []() {
    const int timezoneOffset = server.arg("timezoneOffsetMinutes").toInt();
    const int screenOffStart = server.arg("screenOffStartMinutes").toInt();
    const int screenOffEnd = server.arg("screenOffEndMinutes").toInt();
    if (!server.hasArg("timezoneOffsetMinutes") || timezoneOffset < -720 ||
        timezoneOffset > 840 || !server.hasArg("screenOffStartMinutes") ||
        screenOffStart < 0 || screenOffStart > 1439 ||
        !server.hasArg("screenOffEndMinutes") || screenOffEnd < 0 ||
        screenOffEnd > 1439) {
      sendLocalizedError(400, "时间或时区设置无效",
                         "The time or timezone setting is invalid");
      return;
    }

    const String epochArgument = server.arg("epoch");
    if (!epochArgument.isEmpty()) {
      char* end = nullptr;
      const uint64_t epoch = strtoull(epochArgument.c_str(), &end, 10);
      if (!end || *end != '\0' || epoch > UINT32_MAX) {
        sendLocalizedError(400, "日期时间超出支持范围",
                           "The date and time are outside the supported "
                           "range");
        return;
      }
      const int64_t localWallTime =
          static_cast<int64_t>(epoch) +
          static_cast<int64_t>(timezoneOffset) * 60LL;
      if (localWallTime < LOCAL_TIME_MIN_EPOCH ||
          localWallTime >= LOCAL_TIME_MAX_EPOCH_EXCLUSIVE) {
        sendLocalizedError(400, "日期时间超出支持范围",
                           "The date and time are outside the supported "
                           "range");
        return;
      }
      struct timeval value {
        static_cast<time_t>(epoch), 0
      };
      settimeofday(&value, nullptr);
    }

    if (server.hasArg("pageTransition")) {
      const String style = server.arg("pageTransition");
      if (style != "fade" && style != "slide") {
        sendLocalizedError(400, "不支持的切换动画",
                           "Unsupported page animation");
        return;
      }
      appConfig.setPageTransitionStyle(
          style == "slide" ? PageTransitionStyle::SlideHorizontal
                           : PageTransitionStyle::FadeThroughBlack);
    }

    displayBeginStorageWrite(false);
    appConfig.setTimezoneOffsetMinutes(timezoneOffset);
    appConfig.setShowDateTime(server.arg("showDateTime") == "1");
    appConfig.setShowWeather(server.arg("showWeather") == "1");
    appConfig.setScreenOffEnabled(server.arg("screenOffEnabled") == "1");
    appConfig.setScreenOffWindow(screenOffStart, screenOffEnd);
    const bool saved = appConfig.save();
    displayEndStorageWrite();
    if (!saved) {
      sendLocalizedError(500, "设置保存失败", "Unable to save settings");
      return;
    }
    displayMarkContentDirty();
    sendOk();
  });

  server.on("/api/sd", HTTP_GET, []() {
    mediaStoreEnsureSdMounted();
    const bool mounted = mediaStoreSdMounted();
    String body = "{\"ok\":true,\"mounted\":";
    body += mounted ? "true" : "false";
    body += ",\"status\":\"";
    body += mediaStoreSdStatusCode();
    body += "\",\"filesystem\":\"";
    body += mediaStoreSdFilesystemName();
    body += "\",\"supportedFilesystems\":\"";
    body += mediaStoreSdSupportedFilesystems();
    body += "\",\"files\":";
    body += mounted ? mediaScanSdJson() : "[]";
    body += "}";
    sendJson(200, body);
  });

  server.on("/api/sd/rescan", HTTP_POST, []() {
    if (llmNarrationMediaReadInProgress()) {
      sendLocalizedError(409, "正在读取卡内图片生成旁白，请稍后再试",
                         "A card image is being prepared for narration; try again shortly");
      return;
    }
    // Playback may be holding a file open on the card being remounted.
    displayReleaseMedia();
    mediaStoreUnmountSd();
    const bool mounted = mediaStoreMountSd();
    // Pages that were unplayable while the card was missing can render now;
    // pages that just lost their card are skipped by playback.
    displayMarkContentDirty();
    if (!mounted) {
      const MediaStoreSdStatus status = mediaStoreSdStatus();
      if (status == MediaStoreSdStatus::UnsupportedFilesystem) {
        const String filesystem = mediaStoreSdFilesystemName();
        const String supported = mediaStoreSdSupportedFilesystems();
        sendError(415, appConfig.language() == InterfaceLanguage::Chinese
                           ? "检测到 TF 卡，但 " + filesystem +
                                 " 文件系统不受支持。请使用 " + supported + "。"
                           : "TF card detected, but " + filesystem +
                                 " is not supported. Use " + supported + ".");
        return;
      }
      if (status == MediaStoreSdStatus::UnreadableFilesystem) {
        const String supported = mediaStoreSdSupportedFilesystems();
        sendError(422, appConfig.language() == InterfaceLanguage::Chinese
                           ? "检测到 TF 卡，但文件系统无法读取。请检查卡，或使用 " +
                                 supported + "。"
                           : "TF card detected, but its filesystem is unreadable. "
                             "Check the card or use " +
                                 supported + ".");
        return;
      }
      sendLocalizedError(404, "没有检测到 TF 卡",
                         "No TF card was detected");
      return;
    }
    sendOk();
  });

  // Adds a page that plays straight off the card. The file stays where it is;
  // only its path is stored, so a 12 MB content partition is no longer the
  // limit for how much media a device can show.
  server.on("/api/pages/sd", HTTP_POST, []() {
    const String path = server.arg("path");
    if (!isSafeSdPath(path)) {
      sendLocalizedError(400, "TF 卡文件路径无效",
                         "That TF card path is not valid");
      return;
    }
    if (!mediaStoreSdMounted() || !mediaExists(path)) {
      sendLocalizedError(404, "TF 卡上找不到该文件",
                         "That file is no longer on the card");
      return;
    }
    uint16_t imageWidth = 0;
    uint16_t imageHeight = 0;
    if (!hasExpectedImageSignature(path) ||
        !displayGetImageSize(path, imageWidth, imageHeight)) {
      sendLocalizedError(400, "该文件无法解码，请使用 PNG/JPG/GIF/RGB565",
                         "This file could not be decoded; use PNG, JPG, GIF, "
                         "or RGB565");
      return;
    }
    displayBeginStorageWrite(false);
    const bool added = appConfig.addImage(path);
    displayEndStorageWrite();
    if (!added) {
      sendLocalizedError(400, "无法添加页面，页面数量可能已达到上限",
                         "The page could not be added; the page limit may have "
                         "been reached");
      return;
    }
    Serial.printf("[sd] page added: %s (%ux%u)\n", path.c_str(), imageWidth,
                  imageHeight);
    displayMarkContentDirty();
    sendOk();
  });

  server.on("/sdmedia", HTTP_GET, []() {
    const String path = server.arg("path");
    if (!isSafeSdPath(path) || !mediaStoreSdMounted()) {
      server.send(404, "text/plain; charset=utf-8", "Not found");
      return;
    }
    File file = mediaOpen(path);
    if (!file) {
      server.send(404, "text/plain; charset=utf-8", "Not found");
      return;
    }
    server.sendHeader("Cache-Control", "public, max-age=300");
    server.streamFile(file, mimeForPath(path));
    file.close();
  });

  server.on("/api/wifi/scan", HTTP_GET, []() {
    const int count = WiFi.scanNetworks(false, true);
    DynamicJsonDocument doc(4096);
    doc["ok"] = true;
    JsonArray networks = doc.createNestedArray("networks");
    for (int i = 0; i < count && i < 24; ++i) {
      if (WiFi.SSID(i).isEmpty()) {
        continue;
      }
      bool duplicate = false;
      for (JsonObject existing : networks) {
        duplicate |= existing["ssid"].as<String>() == WiFi.SSID(i);
      }
      if (duplicate) {
        continue;
      }
      JsonObject item = networks.createNestedObject();
      item["ssid"] = WiFi.SSID(i);
      item["rssi"] = WiFi.RSSI(i);
      item["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    String body;
    serializeJson(doc, body);
    sendJson(200, body);
  });

  server.on("/api/wifi", HTTP_POST, [this]() {
    const String ssid = server.arg("ssid");
    const String password = server.arg("password");
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64) {
      sendLocalizedError(400, "Wi-Fi 名称或密码无效",
                         "The Wi-Fi name or password is invalid");
      return;
    }
    Preferences preferences;
    displayBeginStorageWrite(false);
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
    displayEndStorageWrite();
    sendOk();
    restartAt_ = millis() + 1800;
  });

  server.on("/api/wifi/reset", HTTP_POST, [this]() {
    // The shared helper brackets its NVS write with
    // displayBeginStorageWrite()/displayEndStorageWrite().
    if (!clearWifiAndRequestRestart()) {
      sendLocalizedError(500, "无法清除 Wi-Fi 配置",
                         "Unable to clear the Wi-Fi settings");
      return;
    }
    sendOk();
  });

  const char* captivePaths[] = {"/generate_204", "/hotspot-detect.html",
                                "/connecttest.txt", "/ncsi.txt"};
  for (const char* path : captivePaths) {
    server.on(path, HTTP_ANY, []() {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    });
  }

  server.onNotFound([this]() {
    if (serveMedia(server.uri())) {
      return;
    }
    if (provisioning_) {
      server.send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
      return;
    }
    server.send(404, "text/plain; charset=utf-8", "Not found");
  });

  server.begin();
  if (provisioning_) {
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  }
  Serial.printf("[web] listening on http://%s/\n",
                (provisioning_ ? WiFi.softAPIP() : WiFi.localIP())
                    .toString()
                    .c_str());
}

void WebPortal::loop() {
  if (provisioning_) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
}

bool WebPortal::clearWifiAndRequestRestart() {
  Preferences preferences;
  displayBeginStorageWrite(false);
  const bool opened = preferences.begin("wifi", false);
  const bool cleared = opened && preferences.clear();
  if (opened) {
    preferences.end();
  }
  displayEndStorageWrite();

  if (!cleared) {
    return false;
  }
  restartAt_ = millis() + 1200;
  return true;
}

bool WebPortal::restartRequested() const {
  return restartAt_ != 0 &&
         static_cast<int32_t>(millis() - restartAt_) >= 0;
}
