#include "llm_narration.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_jpeg_enc.h>
#include <esp_random.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <mbedtls/base64.h>
#include <sys/socket.h>

#include "app_config.h"
#include "media_store.h"
#include "raw_image.h"

namespace {
constexpr uint32_t HTTP_TIMEOUT_MS = 30000;
constexpr uint32_t HTTP_REQUEST_BUDGET_MS = 45000;
constexpr uint32_t HTTP_DEADLINE_TASK_STACK_BYTES = 3072;
constexpr uint32_t FAILURE_RETRY_INTERVAL_MS = 60U * 1000U;
constexpr uint32_t RESULT_SAVE_RETRY_MS = 5000;
constexpr uint32_t NARRATION_TASK_STACK_BYTES = 20480;
constexpr size_t MAX_HTTP_RESPONSE_BYTES = 8192;
constexpr size_t MAX_DIRECT_IMAGE_BYTES = 2U * 1024U * 1024U;
constexpr size_t MAX_JPEG_OUTPUT_BYTES = 512U * 1024U;
constexpr size_t BASE64_INPUT_CHUNK_BYTES = 1536;
constexpr size_t BASE64_OUTPUT_CHUNK_BYTES = 2048;
constexpr size_t JOB_PATH_BYTES = 512;
constexpr size_t JOB_BASE_URL_BYTES = 384;
constexpr size_t JOB_API_KEY_BYTES = 512;
constexpr size_t JOB_MODEL_BYTES = 128;
constexpr size_t RESULT_TEXT_BYTES = 4 * LLM_NARRATION_MAX_CODEPOINTS + 1;
constexpr size_t FAILURE_SLOT_COUNT = 16;
// This output contract is deliberately firmware-owned. The configurable
// prompt can describe the desired narration, but cannot alter the response
// shape, language, safety/style rules, or length limit expected by the parser.
constexpr char NARRATION_OUTPUT_CONSTRAINTS[] =
    "\n\n输出约束：只输出简体中文旁白正文，不要标题、引号、Markdown、解释或 "
    "emoji；避免生僻字；最多 64 个 Unicode 字符。";

// A deterministic 64 x 64 PNG with a yellow circle on a blue field. The
// configuration test therefore exercises the same multimodal data-URL path as
// real narration without reading or persisting any user media.
constexpr uint8_t CONFIG_TEST_IMAGE[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x40, 0x08, 0x02, 0x00, 0x00, 0x00, 0x25, 0x0b, 0xe6, 0x89,
    0x00, 0x00, 0x00, 0xa3, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed,
    0xd9, 0xc1, 0x11, 0x80, 0x30, 0x08, 0x05, 0xd1, 0x5f, 0x85, 0x5d,
    0xd8, 0x88, 0x25, 0x59, 0xa1, 0x0d, 0x79, 0xb7, 0x85, 0x84, 0x60,
    0x02, 0x64, 0x67, 0x28, 0x60, 0xdf, 0x2d, 0x01, 0x1d, 0xd7, 0x9d,
    0x7a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xc0, 0x6b, 0xde, 0xe7, 0x6c, 0x99, 0x88, 0x80, 0xc6, 0x74, 0x77,
    0x86, 0x96, 0xa4, 0x3b, 0x32, 0xb4, 0xb6, 0x7e, 0xdc, 0xa0, 0xb5,
    0xe9, 0xe3, 0x0c, 0x05, 0xa9, 0x37, 0x1b, 0x14, 0xa7, 0xde, 0x66,
    0xd8, 0x0f, 0xf0, 0x6b, 0xbd, 0xc1, 0xa0, 0x68, 0xf5, 0xbd, 0x86,
    0x9d, 0x00, 0xd3, 0xea, 0xbb, 0x0c, 0xdb, 0x00, 0x26, 0xd7, 0xb7,
    0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0x9c, 0x4e, 0x01, 0xe0, 0x4b, 0x19, 0x00, 0x90, 0x7e, 0x2b,
    0xc1, 0x62, 0x2b, 0x06, 0x20, 0xfd, 0x6e, 0xb4, 0xc2, 0x76, 0xba,
    0xc2, 0x7d, 0xa0, 0xc8, 0x85, 0xa6, 0xc2, 0x8d, 0xac, 0xc8, 0x95,
    0x92, 0x43, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
    0xe1, 0x7c, 0x8d, 0xe0, 0xcb, 0xdf, 0xb7, 0x79, 0x3b, 0x6a, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

enum class JobKind : uint8_t {
  Narration,
  ConfigTest,
};

struct NarrationJob {
  JobKind kind = JobKind::Narration;
  uint32_t testId = 0;
  char requestToken[LLM_CONFIG_TEST_REQUEST_TOKEN_BYTES] = "";
  uint32_t pageId = 0;
  char imagePath[JOB_PATH_BYTES] = "";
  char baseUrl[JOB_BASE_URL_BYTES] = "";
  char apiKey[JOB_API_KEY_BYTES] = "";
  char model[JOB_MODEL_BYTES] = "";
  char narrationPrompt[MAX_LLM_NARRATION_PROMPT_BYTES] = "";
  uint8_t* rgb565Alpha = nullptr;
  size_t rgb565AlphaBytes = 0;
  uint16_t frameWidth = 0;
  uint16_t frameHeight = 0;
};

struct NarrationResult {
  uint32_t pageId = 0;
  char imagePath[JOB_PATH_BYTES] = "";
  char narration[RESULT_TEXT_BYTES] = "";
};

struct FailureSlot {
  uint32_t pathHash = 0;
  uint32_t retryAt = 0;
};

struct ImagePayload {
  uint8_t* data = nullptr;
  size_t size = 0;
  const char* mime = nullptr;
};

struct HttpDeadlineGuard {
  EventGroupHandle_t events = nullptr;
  StaticEventGroup_t eventStorage{};
  SemaphoreHandle_t socketMutex = nullptr;
  StaticSemaphore_t socketMutexStorage{};
  int socketFd = -1;
  uint32_t timeoutMs = 0;
};

constexpr EventBits_t HTTP_REQUEST_FINISHED_BIT = BIT0;
constexpr EventBits_t HTTP_WATCHDOG_FINISHED_BIT = BIT1;
constexpr EventBits_t HTTP_TIMED_OUT_BIT = BIT2;

portMUX_TYPE narrationMux = portMUX_INITIALIZER_UNLOCKED;
bool initialized = false;
bool fetchRunning = false;
bool resultPending = false;
uint32_t resultRetryAt = 0;
bool mediaReadInProgress = false;
uint32_t activePathHash = 0;
char activeImagePath[JOB_PATH_BYTES] = "";
bool activeInvalidated = false;
bool activeRegeneration = false;
NarrationResult pendingResult;
FailureSlot failures[FAILURE_SLOT_COUNT];
NarrationJob* queuedConfigTestJob = nullptr;
NarrationJob* queuedRegenerationJob = nullptr;
LlmConfigTestStatus configTestStatus;
LlmConfigTestStatus previousConfigTestStatus;

bool configTestBusy() {
  return configTestStatus.state == LlmConfigTestState::Queued ||
         configTestStatus.state == LlmConfigTestState::Running;
}

uint32_t hashPath(const char* path) {
  uint32_t hash = 2166136261U;
  if (!path) {
    return hash;
  }
  while (*path) {
    hash ^= static_cast<uint8_t>(*path++);
    hash *= 16777619U;
  }
  return hash == 0 ? 1 : hash;
}

void copyText(char* destination, size_t capacity, const String& source) {
  if (!destination || capacity == 0) {
    return;
  }
  snprintf(destination, capacity, "%s", source.c_str());
}

void copyText(char* destination, size_t capacity, const char* source) {
  if (!destination || capacity == 0) {
    return;
  }
  snprintf(destination, capacity, "%s", source ? source : "");
}

void wipeString(String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    value.setCharAt(i, '\0');
  }
  value = "";
}

void wipeMemory(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size-- > 0) {
    *bytes++ = 0;
  }
}

bool retryAllowed(uint32_t pathHash, uint32_t now) {
  for (const FailureSlot& slot : failures) {
    if (slot.pathHash == pathHash &&
        static_cast<int32_t>(now - slot.retryAt) < 0) {
      return false;
    }
  }
  return true;
}

void rememberFailure(uint32_t pathHash, uint32_t now) {
  FailureSlot* selected = &failures[0];
  for (FailureSlot& slot : failures) {
    if (slot.pathHash == pathHash || slot.pathHash == 0 ||
        static_cast<int32_t>(now - slot.retryAt) >= 0) {
      selected = &slot;
      break;
    }
    if (static_cast<int32_t>(slot.retryAt - selected->retryAt) < 0) {
      selected = &slot;
    }
  }
  selected->pathHash = pathHash;
  selected->retryAt = now + FAILURE_RETRY_INTERVAL_MS;
}

void forgetFailure(uint32_t pathHash) {
  for (FailureSlot& slot : failures) {
    if (slot.pathHash == pathHash) {
      slot = {};
    }
  }
}

String jsonQuoted(const char* value) {
  DynamicJsonDocument document(MAX_LLM_NARRATION_PROMPT_BYTES +
                               sizeof(NARRATION_OUTPUT_CONSTRAINTS) + 512);
  document.set(value ? value : "");
  String output;
  serializeJson(document, output);
  return output;
}

String chatCompletionsUrl(String baseUrl) {
  baseUrl.trim();
  while (baseUrl.endsWith("/")) {
    baseUrl.remove(baseUrl.length() - 1);
  }
  if (baseUrl.endsWith("/chat/completions")) {
    return baseUrl;
  }
  return baseUrl + "/chat/completions";
}

const char* mimeForImagePath(const String& path) {
  String lowered(path);
  lowered.toLowerCase();
  if (lowered.endsWith(".png")) {
    return "image/png";
  }
  if (lowered.endsWith(".gif")) {
    return "image/gif";
  }
  return "image/jpeg";
}

bool readImageFile(const String& path, ImagePayload& payload) {
  const size_t size = mediaSize(path);
  if (size == 0 || size > MAX_DIRECT_IMAGE_BYTES) {
    Serial.printf("[narration] image is empty or exceeds %u bytes: %s\n",
                  static_cast<unsigned>(MAX_DIRECT_IMAGE_BYTES), path.c_str());
    return false;
  }
  uint8_t* data = static_cast<uint8_t*>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!data) {
    Serial.println("[narration] unable to allocate image payload");
    return false;
  }
  File file = mediaOpen(path);
  if (!file) {
    heap_caps_free(data);
    return false;
  }
  size_t totalRead = 0;
  while (totalRead < size) {
    const size_t count = file.read(data + totalRead, size - totalRead);
    if (count == 0) {
      break;
    }
    totalRead += count;
  }
  file.close();
  if (totalRead != size) {
    heap_caps_free(data);
    return false;
  }
  payload.data = data;
  payload.size = size;
  payload.mime = mimeForImagePath(path);
  return true;
}

bool encodeRawImage(const String& path, ImagePayload& payload) {
  uint8_t* pixels = static_cast<uint8_t*>(
      jpeg_calloc_align(RAW_IMAGE_PIXEL_BYTES, 16));
  if (!pixels) {
    Serial.println("[narration] unable to allocate aligned RGB565 buffer");
    return false;
  }
  if (!rawImageLoadPixels(path, pixels, RAW_IMAGE_PIXEL_BYTES)) {
    jpeg_free_align(pixels);
    return false;
  }

  uint8_t* jpeg = static_cast<uint8_t*>(heap_caps_malloc(
      MAX_JPEG_OUTPUT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpeg) {
    jpeg_free_align(pixels);
    Serial.println("[narration] unable to allocate JPEG output buffer");
    return false;
  }

  jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
  config.width = RAW_IMAGE_WIDTH;
  config.height = RAW_IMAGE_HEIGHT;
  config.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
  config.subsampling = JPEG_SUBSAMPLE_420;
  config.quality = 60;
  config.rotate = JPEG_ROTATE_0D;
  config.task_enable = false;

  jpeg_enc_handle_t encoder = nullptr;
  int jpegSize = 0;
  jpeg_error_t error = jpeg_enc_open(&config, &encoder);
  if (error == JPEG_ERR_OK) {
    error = jpeg_enc_process(encoder, pixels, RAW_IMAGE_PIXEL_BYTES, jpeg,
                             MAX_JPEG_OUTPUT_BYTES, &jpegSize);
  }
  if (encoder) {
    jpeg_enc_close(encoder);
  }
  jpeg_free_align(pixels);
  if (error != JPEG_ERR_OK || jpegSize <= 0) {
    heap_caps_free(jpeg);
    Serial.printf("[narration] RGB565 JPEG encode failed: %d\n",
                  static_cast<int>(error));
    return false;
  }

  payload.data = jpeg;
  payload.size = static_cast<size_t>(jpegSize);
  payload.mime = "image/jpeg";
  return true;
}

bool encodeRgb565AlphaFrame(const NarrationJob& job, ImagePayload& payload) {
  if (!job.rgb565Alpha || job.frameWidth == 0 || job.frameHeight == 0 ||
      job.frameWidth > 480 || job.frameHeight > 480) {
    return false;
  }
  const size_t pixelCount =
      static_cast<size_t>(job.frameWidth) * job.frameHeight;
  if (job.rgb565AlphaBytes != pixelCount * 3U) {
    return false;
  }

  uint8_t* pixels =
      static_cast<uint8_t*>(jpeg_calloc_align(pixelCount * 2U, 16));
  if (!pixels) {
    Serial.println("[narration] unable to allocate GIF still buffer");
    return false;
  }
  for (size_t i = 0; i < pixelCount; ++i) {
    pixels[i * 2U] = job.rgb565Alpha[i * 3U];
    pixels[i * 2U + 1U] = job.rgb565Alpha[i * 3U + 1U];
  }

  uint8_t* jpeg = static_cast<uint8_t*>(heap_caps_malloc(
      MAX_JPEG_OUTPUT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpeg) {
    jpeg_free_align(pixels);
    Serial.println("[narration] unable to allocate GIF JPEG output buffer");
    return false;
  }

  jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
  config.width = job.frameWidth;
  config.height = job.frameHeight;
  config.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
  config.subsampling = JPEG_SUBSAMPLE_420;
  config.quality = 60;
  config.rotate = JPEG_ROTATE_0D;
  config.task_enable = false;

  jpeg_enc_handle_t encoder = nullptr;
  int jpegSize = 0;
  jpeg_error_t error = jpeg_enc_open(&config, &encoder);
  if (error == JPEG_ERR_OK) {
    error = jpeg_enc_process(encoder, pixels, pixelCount * 2U, jpeg,
                             MAX_JPEG_OUTPUT_BYTES, &jpegSize);
  }
  if (encoder) {
    jpeg_enc_close(encoder);
  }
  jpeg_free_align(pixels);
  if (error != JPEG_ERR_OK || jpegSize <= 0) {
    heap_caps_free(jpeg);
    Serial.printf("[narration] GIF still JPEG encode failed: %d\n",
                  static_cast<int>(error));
    return false;
  }

  payload.data = jpeg;
  payload.size = static_cast<size_t>(jpegSize);
  payload.mime = "image/jpeg";
  return true;
}

bool loadImagePayload(const String& path, ImagePayload& payload) {
  String lowered(path);
  lowered.toLowerCase();
  return lowered.endsWith(RAW_IMAGE_EXTENSION)
             ? encodeRawImage(path, payload)
             : readImageFile(path, payload);
}

bool loadConfigTestImage(ImagePayload& payload) {
  uint8_t* copy = static_cast<uint8_t*>(
      heap_caps_malloc(sizeof(CONFIG_TEST_IMAGE), MALLOC_CAP_8BIT));
  if (!copy) {
    return false;
  }
  memcpy(copy, CONFIG_TEST_IMAGE, sizeof(CONFIG_TEST_IMAGE));
  payload.data = copy;
  payload.size = sizeof(CONFIG_TEST_IMAGE);
  payload.mime = "image/png";
  return true;
}

void httpDeadlineTask(void* argument) {
  HttpDeadlineGuard* guard = static_cast<HttpDeadlineGuard*>(argument);
  const EventBits_t bits = xEventGroupWaitBits(
      guard->events, HTTP_REQUEST_FINISHED_BIT, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(guard->timeoutMs));
  if ((bits & HTTP_REQUEST_FINISHED_BIT) == 0) {
    xEventGroupSetBits(guard->events, HTTP_TIMED_OUT_BIT);
    // The request task remains the sole owner of esp_http_client. Shutting
    // down only a published socket safely interrupts blocking TLS/header/body
    // I/O without racing the client's parser or cleanup state across cores.
    // The synchronous resolver and TLS setup cannot expose a socket early;
    // lwIP DNS retries and the transport timeout bound those phases. A late
    // connect sees the timeout bit and shuts its socket down immediately.
    if (xSemaphoreTake(guard->socketMutex, portMAX_DELAY) == pdTRUE) {
      if (guard->socketFd >= 0) {
        shutdown(guard->socketFd, SHUT_RDWR);
      }
      xSemaphoreGive(guard->socketMutex);
    }
  }
  xEventGroupSetBits(guard->events, HTTP_WATCHDOG_FINISHED_BIT);
  vTaskDelete(nullptr);
}

void publishHttpDeadlineSocket(HttpDeadlineGuard& guard, int socketFd) {
  if (!guard.events || !guard.socketMutex || socketFd < 0) {
    return;
  }
  if (xSemaphoreTake(guard.socketMutex, portMAX_DELAY) == pdTRUE) {
    guard.socketFd = socketFd;
    // Cover the race where the request budget elapsed immediately before the
    // connect callback published the descriptor.
    if ((xEventGroupGetBits(guard.events) & HTTP_TIMED_OUT_BIT) != 0) {
      shutdown(guard.socketFd, SHUT_RDWR);
    }
    xSemaphoreGive(guard.socketMutex);
  }
}

void invalidateHttpDeadlineSocket(HttpDeadlineGuard& guard) {
  if (!guard.socketMutex) {
    return;
  }
  // ESP-IDF dispatches DISCONNECTED synchronously before closing the socket.
  // Sharing this mutex with the watchdog prevents an old descriptor from
  // being closed, reused, and then accidentally shut down by the watchdog.
  if (xSemaphoreTake(guard.socketMutex, portMAX_DELAY) == pdTRUE) {
    guard.socketFd = -1;
    xSemaphoreGive(guard.socketMutex);
  }
}

esp_err_t handleHttpClientEvent(esp_http_client_event_t* event) {
  if (!event || !event->user_data) {
    return ESP_OK;
  }
  HttpDeadlineGuard* guard =
      static_cast<HttpDeadlineGuard*>(event->user_data);
  if (event->event_id == HTTP_EVENT_ON_CONNECTED && event->client) {
    publishHttpDeadlineSocket(*guard,
                              esp_http_client_get_socket(event->client));
  } else if (event->event_id == HTTP_EVENT_DISCONNECTED) {
    invalidateHttpDeadlineSocket(*guard);
  }
  return ESP_OK;
}

bool startHttpDeadlineGuard(uint32_t requestStartedAt,
                            HttpDeadlineGuard& guard) {
  const uint32_t elapsed = millis() - requestStartedAt;
  if (elapsed >= HTTP_REQUEST_BUDGET_MS) {
    return false;
  }
  guard.timeoutMs = HTTP_REQUEST_BUDGET_MS - elapsed;
  guard.socketMutex =
      xSemaphoreCreateMutexStatic(&guard.socketMutexStorage);
  if (!guard.socketMutex) {
    return false;
  }
  guard.events = xEventGroupCreateStatic(&guard.eventStorage);
  if (!guard.events) {
    vSemaphoreDelete(guard.socketMutex);
    guard.socketMutex = nullptr;
    return false;
  }
  if (xTaskCreatePinnedToCore(httpDeadlineTask, "llm-http-deadline",
                              HTTP_DEADLINE_TASK_STACK_BYTES, &guard, 2,
                              nullptr, 1) != pdPASS) {
    vEventGroupDelete(guard.events);
    guard.events = nullptr;
    vSemaphoreDelete(guard.socketMutex);
    guard.socketMutex = nullptr;
    return false;
  }
  return true;
}

bool finishHttpDeadlineGuard(HttpDeadlineGuard& guard) {
  if (!guard.events) {
    return false;
  }
  xEventGroupSetBits(guard.events, HTTP_REQUEST_FINISHED_BIT);
  const EventBits_t bits = xEventGroupWaitBits(
      guard.events, HTTP_WATCHDOG_FINISHED_BIT, pdFALSE, pdTRUE,
      portMAX_DELAY);
  const bool completedInTime = (bits & HTTP_TIMED_OUT_BIT) == 0;
  vEventGroupDelete(guard.events);
  guard.events = nullptr;
  return completedInTime;
}

void releaseHttpDeadlineGuard(HttpDeadlineGuard& guard) {
  if (guard.socketMutex) {
    vSemaphoreDelete(guard.socketMutex);
    guard.socketMutex = nullptr;
  }
}

bool applyHttpDeadline(esp_http_client_handle_t client,
                       uint32_t requestStartedAt) {
  const uint32_t elapsed = millis() - requestStartedAt;
  if (elapsed >= HTTP_REQUEST_BUDGET_MS) {
    return false;
  }
  const uint32_t remaining = HTTP_REQUEST_BUDGET_MS - elapsed;
  const uint32_t operationTimeout =
      remaining < HTTP_TIMEOUT_MS ? remaining : HTTP_TIMEOUT_MS;
  return esp_http_client_set_timeout_ms(client,
                                        static_cast<int>(operationTimeout)) ==
         ESP_OK;
}

bool httpDeadlineExpired(uint32_t requestStartedAt) {
  return millis() - requestStartedAt >= HTTP_REQUEST_BUDGET_MS;
}

bool writeAll(esp_http_client_handle_t client, const uint8_t* data,
              size_t size, uint32_t requestStartedAt) {
  size_t written = 0;
  while (written < size) {
    if (!applyHttpDeadline(client, requestStartedAt)) {
      return false;
    }
    const int count = esp_http_client_write(
        client, reinterpret_cast<const char*>(data + written), size - written);
    if (count <= 0) {
      return false;
    }
    written += static_cast<size_t>(count);
  }
  return true;
}

bool writeAll(esp_http_client_handle_t client, const String& value,
              uint32_t requestStartedAt) {
  return writeAll(client,
                  reinterpret_cast<const uint8_t*>(value.c_str()),
                  value.length(), requestStartedAt);
}

bool writeBase64(esp_http_client_handle_t client, const uint8_t* data,
                 size_t size, uint32_t requestStartedAt) {
  uint8_t encoded[BASE64_OUTPUT_CHUNK_BYTES + 4];
  size_t offset = 0;
  while (offset < size) {
    const size_t inputSize =
        min(BASE64_INPUT_CHUNK_BYTES, size - offset);
    size_t outputSize = 0;
    if (mbedtls_base64_encode(encoded, sizeof(encoded), &outputSize,
                              data + offset, inputSize) != 0 ||
        !writeAll(client, encoded, outputSize, requestStartedAt)) {
      return false;
    }
    offset += inputSize;
  }
  return true;
}

bool readResponse(esp_http_client_handle_t client, String& response,
                  uint32_t requestStartedAt,
                  LlmConfigTestFailure& failure) {
  char buffer[512];
  while (true) {
    if (!applyHttpDeadline(client, requestStartedAt)) {
      failure = LlmConfigTestFailure::Timeout;
      return false;
    }
    const int count = esp_http_client_read(client, buffer, sizeof(buffer));
    if (count == -ESP_ERR_HTTP_EAGAIN) {
      failure = LlmConfigTestFailure::Timeout;
      return false;
    }
    if (count < 0) {
      failure = LlmConfigTestFailure::Connection;
      return false;
    }
    if (count == 0) {
      return true;
    }
    if (response.length() + static_cast<size_t>(count) >
        MAX_HTTP_RESPONSE_BYTES) {
      failure = LlmConfigTestFailure::InvalidResponse;
      return false;
    }
    response.concat(buffer, count);
  }
}

bool postVisionRequest(const NarrationJob& job, const ImagePayload& image,
                       String& response, int& httpStatus,
                       LlmConfigTestFailure& failure) {
  httpStatus = 0;
  failure = LlmConfigTestFailure::Internal;
  const String endpoint = chatCompletionsUrl(job.baseUrl);
  String prompt;
  const size_t promptBytes = strlen(job.narrationPrompt) +
                             strlen(NARRATION_OUTPUT_CONSTRAINTS);
  if (!prompt.reserve(promptBytes + 1)) {
    return false;
  }
  prompt = job.narrationPrompt;
  prompt += NARRATION_OUTPUT_CONSTRAINTS;
  String prefix = "{\"model\":" + jsonQuoted(job.model) +
                  ",\"max_tokens\":96,\"messages\":[{\"role\":\"user\",\"content\":[{"
                  "\"type\":\"text\",\"text\":" +
                  jsonQuoted(prompt.c_str()) +
                  "},{\"type\":\"image_url\",\"image_url\":{"
                  "\"url\":\"data:" +
                  image.mime + ";base64,";
  const String suffix = "\",\"detail\":\"low\"}}]}]}";
  const size_t base64Size = ((image.size + 2) / 3) * 4;
  const size_t bodySize = prefix.length() + base64Size + suffix.length();
  const uint32_t requestStartedAt = millis();
  // Start the best-effort wall-clock budget before connection setup. The
  // synchronous resolver/TLS phases use their platform timeouts; once
  // connected, the callback publishes a socket that can interrupt blocking
  // request-header, upload, and response I/O when the budget expires.

  esp_http_client_config_t config{};
  config.url = endpoint.c_str();
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  // Never forward the bearer token to a URL selected by a redirect response.
  config.disable_auto_redirect = true;
  config.buffer_size = 2048;
  config.buffer_size_tx = 2048;
  HttpDeadlineGuard deadlineGuard;
  config.event_handler = handleHttpClientEvent;
  config.user_data = &deadlineGuard;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }

  String authorization = "Bearer " + String(job.apiKey);
  esp_http_client_set_header(client, "Authorization", authorization.c_str());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  bool ok = false;
  const bool deadlineGuardStarted =
      startHttpDeadlineGuard(requestStartedAt, deadlineGuard);
  if (!deadlineGuardStarted) {
    failure = httpDeadlineExpired(requestStartedAt)
                  ? LlmConfigTestFailure::Timeout
                  : LlmConfigTestFailure::Internal;
  } else {
    const esp_err_t requestError =
        applyHttpDeadline(client, requestStartedAt)
            ? esp_http_client_open(client, bodySize)
            : ESP_ERR_TIMEOUT;
    if (requestError != ESP_OK) {
      failure = requestError == ESP_ERR_TIMEOUT ||
                        requestError == ESP_ERR_HTTP_EAGAIN ||
                        httpDeadlineExpired(requestStartedAt)
                    ? LlmConfigTestFailure::Timeout
                    : LlmConfigTestFailure::Connection;
      Serial.printf("[narration] API connection failed: %s\n",
                    esp_err_to_name(requestError));
    } else if (!writeAll(client, prefix, requestStartedAt) ||
               !writeBase64(client, image.data, image.size,
                            requestStartedAt) ||
               !writeAll(client, suffix, requestStartedAt)) {
      failure = httpDeadlineExpired(requestStartedAt)
                    ? LlmConfigTestFailure::Timeout
                    : LlmConfigTestFailure::Connection;
      Serial.println("[narration] API request write failed");
    } else {
      const int64_t headers =
          applyHttpDeadline(client, requestStartedAt)
              ? esp_http_client_fetch_headers(client)
              : -static_cast<int64_t>(ESP_ERR_HTTP_EAGAIN);
      httpStatus = esp_http_client_get_status_code(client);
      if (headers < 0) {
        failure = headers == -static_cast<int64_t>(ESP_ERR_HTTP_EAGAIN) ||
                          httpDeadlineExpired(requestStartedAt)
                      ? LlmConfigTestFailure::Timeout
                      : LlmConfigTestFailure::Connection;
      } else if (httpStatus < 200 || httpStatus >= 300) {
        failure = llmConfigTestFailureForHttpStatus(httpStatus);
      } else if (readResponse(client, response, requestStartedAt, failure)) {
        failure = LlmConfigTestFailure::None;
        ok = true;
      }
      if (!ok) {
        Serial.printf("[narration] API request failed (HTTP %d)\n",
                      httpStatus);
      }
    }
  }
  if (deadlineGuardStarted && !finishHttpDeadlineGuard(deadlineGuard)) {
    ok = false;
    failure = LlmConfigTestFailure::Timeout;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  releaseHttpDeadlineGuard(deadlineGuard);
  wipeString(authorization);
  return ok;
}

String responseContent(const String& body) {
  DynamicJsonDocument document(MAX_HTTP_RESPONSE_BYTES);
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    Serial.printf("[narration] response JSON parse failed: %s\n",
                  error.c_str());
    return "";
  }
  JsonVariant content = document["choices"][0]["message"]["content"];
  if (content.is<const char*>()) {
    return String(content.as<const char*>());
  }
  if (content.is<JsonArray>()) {
    for (JsonVariant item : content.as<JsonArray>()) {
      const char* text = item["text"] | nullptr;
      if (text && text[0]) {
        return String(text);
      }
    }
  }
  Serial.println("[narration] response has no assistant text");
  return "";
}

size_t utf8SequenceLength(uint8_t lead) {
  if (lead < 0x80) {
    return 1;
  }
  if ((lead & 0xE0) == 0xC0) {
    return 2;
  }
  if ((lead & 0xF0) == 0xE0) {
    return 3;
  }
  if ((lead & 0xF8) == 0xF0) {
    return 4;
  }
  return 0;
}

String sanitizeNarration(String text) {
  if (!validImageNarration(text)) {
    return "";
  }
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.replace("\t", " ");
  while (text.indexOf("  ") >= 0) {
    text.replace("  ", " ");
  }
  text.trim();
  const char* prefixes[] = {"旁白：", "旁白:", "摘要：", "摘要:"};
  for (const char* prefix : prefixes) {
    if (text.startsWith(prefix)) {
      text.remove(0, strlen(prefix));
      text.trim();
      break;
    }
  }
  if (text.length() >= 2 &&
      ((text.startsWith("\"") && text.endsWith("\"")) ||
       (text.startsWith("“") && text.endsWith("”")))) {
    const size_t endingBytes = text.endsWith("”") ? strlen("”") : 1;
    const size_t startingBytes = text.startsWith("“") ? strlen("“") : 1;
    text.remove(text.length() - endingBytes);
    text.remove(0, startingBytes);
    text.trim();
  }

  String truncated;
  truncated.reserve(min(text.length(), RESULT_TEXT_BYTES - 1));
  size_t offset = 0;
  size_t codepoints = 0;
  while (offset < text.length() &&
         codepoints < LLM_NARRATION_MAX_CODEPOINTS) {
    const uint8_t lead = static_cast<uint8_t>(text[offset]);
    const size_t sequenceLength = utf8SequenceLength(lead);
    if (sequenceLength == 0 || offset + sequenceLength > text.length()) {
      ++offset;
      continue;
    }
    bool valid = true;
    for (size_t i = 1; i < sequenceLength; ++i) {
      valid &= (static_cast<uint8_t>(text[offset + i]) & 0xC0) == 0x80;
    }
    if (!valid) {
      ++offset;
      continue;
    }
    truncated.concat(text.c_str() + offset, sequenceLength);
    offset += sequenceLength;
    ++codepoints;
  }
  truncated.trim();
  if (!validImageNarration(truncated)) {
    return "";
  }
  return truncated;
}

// Must hold narrationMux. Do not release the worker slot: its file handle and
// HTTP request remain alive until completion, but its old result is obsolete.
void invalidateNarrationsLocked(const char* imagePath) {
  if (!imagePath || strcmp(activeImagePath, imagePath) == 0) {
    activeInvalidated = true;
  }
  if (!imagePath || strcmp(pendingResult.imagePath, imagePath) == 0) {
    pendingResult = {};
    resultPending = false;
    resultRetryAt = 0;
  }
  if (imagePath) {
    forgetFailure(hashPath(imagePath));
  } else {
    memset(failures, 0, sizeof(failures));
  }
}

void disposeNarrationJob(NarrationJob* job) {
  if (!job) {
    return;
  }
  if (job->rgb565Alpha) {
    heap_caps_free(job->rgb565Alpha);
  }
  wipeMemory(job, sizeof(*job));
  heap_caps_free(job);
}

// One extra immutable snapshot is enough to wait behind the current worker.
// Repeated taps never pile up paid requests for the same image.
bool queueRegenerationJob(NarrationJob* job) {
  portENTER_CRITICAL(&narrationMux);
  const bool accepted = !queuedRegenerationJob &&
      !(activeRegeneration && strcmp(activeImagePath, job->imagePath) == 0);
  if (accepted) {
    invalidateNarrationsLocked(job->imagePath);
    queuedRegenerationJob = job;
  }
  portEXIT_CRITICAL(&narrationMux);
  if (!accepted) {
    disposeNarrationJob(job);
  }
  return accepted;
}

void finishJob(const NarrationResult& result, uint32_t pathHash, bool ok) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&narrationMux);
  if (!activeInvalidated && ok && !resultPending) {
    pendingResult = result;
    resultPending = true;
    resultRetryAt = 0;
    forgetFailure(pathHash);
  } else if (!activeInvalidated && !ok) {
    rememberFailure(pathHash, now);
  }
  activePathHash = 0;
  activeImagePath[0] = '\0';
  activeInvalidated = false;
  activeRegeneration = false;
  fetchRunning = false;
  portEXIT_CRITICAL(&narrationMux);
}

void finishConfigTest(uint32_t testId, bool ok,
                      LlmConfigTestFailure failure, int httpStatus) {
  portENTER_CRITICAL(&narrationMux);
  if (configTestStatus.id == testId &&
      configTestStatus.state == LlmConfigTestState::Running) {
    configTestStatus.state = ok ? LlmConfigTestState::Passed
                                : LlmConfigTestState::Failed;
    configTestStatus.failure =
        ok ? LlmConfigTestFailure::None : failure;
    configTestStatus.httpStatus = httpStatus;
  }
  fetchRunning = false;
  portEXIT_CRITICAL(&narrationMux);
}

void narrationTask(void* argument) {
  NarrationJob* job = static_cast<NarrationJob*>(argument);
  const bool configTest = job && job->kind == JobKind::ConfigTest;
  const uint32_t completedTestId = job ? job->testId : 0;
  const uint32_t completedPageId = job ? job->pageId : 0;
  const uint32_t completedPathHash = job ? hashPath(job->imagePath) : 0;
  ImagePayload image;
  String body;
  String narration;
  int httpStatus = 0;
  LlmConfigTestFailure failure = LlmConfigTestFailure::Internal;
  bool ok = job != nullptr;
  if (ok && WiFi.status() != WL_CONNECTED) {
    failure = LlmConfigTestFailure::Offline;
    ok = false;
  }
  if (ok) {
    ok = configTest
             ? loadConfigTestImage(image)
             : (job->rgb565Alpha ? encodeRgb565AlphaFrame(*job, image)
                                 : loadImagePayload(job->imagePath, image));
    if (!ok) {
      failure = LlmConfigTestFailure::Internal;
    }
  }
  if (job && job->rgb565Alpha) {
    heap_caps_free(job->rgb565Alpha);
    job->rgb565Alpha = nullptr;
  }
  portENTER_CRITICAL(&narrationMux);
  mediaReadInProgress = false;
  portEXIT_CRITICAL(&narrationMux);
  if (ok) {
    body.reserve(2048);
    ok = postVisionRequest(*job, image, body, httpStatus, failure);
  }
  if (image.data) {
    heap_caps_free(image.data);
  }
  if (ok) {
    narration = sanitizeNarration(responseContent(body));
    ok = !narration.isEmpty();
    if (!ok) {
      failure = LlmConfigTestFailure::InvalidResponse;
    }
  }
  NarrationResult completedResult;
  if (job) {
    if (!configTest) {
      completedResult.pageId = completedPageId;
      copyText(completedResult.imagePath, sizeof(completedResult.imagePath),
               job->imagePath);
      if (ok) {
        copyText(completedResult.narration, sizeof(completedResult.narration),
                 narration);
      }
    }
    wipeMemory(job, sizeof(*job));
    heap_caps_free(job);
  }
  wipeString(body);
  wipeString(narration);
  if (configTest && ok) {
    Serial.println("[llm-test] configuration test passed");
  } else if (configTest) {
    Serial.printf("[llm-test] configuration test failed (%s, HTTP %d)\n",
                  llmConfigTestFailureCode(failure), httpStatus);
  } else if (ok) {
    Serial.printf("[narration] generated for page %u\n",
                  static_cast<unsigned>(completedPageId));
  } else {
    Serial.println("[narration] generation failed; retry delayed");
  }
  if (configTest) {
    finishConfigTest(completedTestId, ok, failure, httpStatus);
  } else {
    finishJob(completedResult, completedPathHash, ok);
  }
  vTaskDelete(nullptr);
}
}  // namespace

void llmNarrationBegin() {
  portENTER_CRITICAL(&narrationMux);
  initialized = true;
  fetchRunning = false;
  resultPending = false;
  resultRetryAt = 0;
  mediaReadInProgress = false;
  activePathHash = 0;
  pendingResult = {};
  queuedConfigTestJob = nullptr;
  queuedRegenerationJob = nullptr;
  activeImagePath[0] = '\0';
  activeInvalidated = false;
  activeRegeneration = false;
  configTestStatus = {};
  previousConfigTestStatus = {};
  memset(failures, 0, sizeof(failures));
  portEXIT_CRITICAL(&narrationMux);
}

bool llmNarrationTestStart(const String& baseUrl, const String& apiKey,
                           const String& model,
                           const String& narrationPrompt,
                           const String& requestToken, uint32_t& testId) {
  testId = 0;
  if (!initialized || baseUrl.isEmpty() || apiKey.isEmpty() ||
      model.isEmpty() || !validLlmNarrationPrompt(narrationPrompt) ||
      baseUrl.length() >= JOB_BASE_URL_BYTES ||
      apiKey.length() >= JOB_API_KEY_BYTES ||
      model.length() >= JOB_MODEL_BYTES ||
      !llmConfigTestRequestTokenValid(requestToken.c_str())) {
    return false;
  }

  NarrationJob* job = static_cast<NarrationJob*>(heap_caps_calloc(
      1, sizeof(NarrationJob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!job) {
    return false;
  }
  job->kind = JobKind::ConfigTest;
  copyText(job->requestToken, sizeof(job->requestToken), requestToken);
  copyText(job->baseUrl, sizeof(job->baseUrl), baseUrl);
  copyText(job->apiKey, sizeof(job->apiKey), apiKey);
  copyText(job->model, sizeof(job->model), model);
  copyText(job->narrationPrompt, sizeof(job->narrationPrompt),
           narrationPrompt);

  uint32_t generatedId = esp_random();
  if (generatedId == 0) {
    generatedId = 1;
  }

  portENTER_CRITICAL(&narrationMux);
  const bool accepted = !configTestBusy() && queuedConfigTestJob == nullptr;
  if (accepted) {
    if (generatedId == configTestStatus.id ||
        generatedId == previousConfigTestStatus.id) {
      ++generatedId;
      if (generatedId == 0) {
        generatedId = 1;
      }
    }
    job->testId = generatedId;
    queuedConfigTestJob = job;
    if (configTestStatus.state == LlmConfigTestState::Passed ||
        configTestStatus.state == LlmConfigTestState::Failed) {
      previousConfigTestStatus = configTestStatus;
    }
    configTestStatus = {};
    configTestStatus.id = generatedId;
    memcpy(configTestStatus.requestToken, job->requestToken,
           sizeof(configTestStatus.requestToken));
    configTestStatus.state = LlmConfigTestState::Queued;
  }
  portEXIT_CRITICAL(&narrationMux);

  if (!accepted) {
    wipeMemory(job, sizeof(*job));
    heap_caps_free(job);
    return false;
  }
  testId = generatedId;
  return true;
}

bool llmNarrationTestStatus(uint32_t testId, LlmConfigTestStatus& status) {
  portENTER_CRITICAL(&narrationMux);
  bool available = testId != 0 && configTestStatus.id == testId &&
                   configTestStatus.state != LlmConfigTestState::Idle;
  if (available) {
    status = configTestStatus;
  } else if (testId != 0 && previousConfigTestStatus.id == testId &&
             previousConfigTestStatus.state != LlmConfigTestState::Idle) {
    status = previousConfigTestStatus;
    available = true;
  }
  portEXIT_CRITICAL(&narrationMux);
  return available;
}

bool llmNarrationTestStatusByRequestToken(
    const String& requestToken, LlmConfigTestStatus& status) {
  if (!llmConfigTestRequestTokenValid(requestToken.c_str())) {
    return false;
  }
  portENTER_CRITICAL(&narrationMux);
  bool available =
      configTestStatus.state != LlmConfigTestState::Idle &&
      llmConfigTestRequestTokenMatches(configTestStatus, requestToken.c_str());
  if (available) {
    status = configTestStatus;
  } else if (previousConfigTestStatus.state != LlmConfigTestState::Idle &&
             llmConfigTestRequestTokenMatches(previousConfigTestStatus,
                                              requestToken.c_str())) {
    status = previousConfigTestStatus;
    available = true;
  }
  portEXIT_CRITICAL(&narrationMux);
  return available;
}

bool llmNarrationTestBusy() {
  portENTER_CRITICAL(&narrationMux);
  const bool busy = configTestBusy();
  portEXIT_CRITICAL(&narrationMux);
  return busy;
}

void llmNarrationLoop() {
  NarrationJob* job = nullptr;
  NarrationJob* discarded = nullptr;
  const bool enabled = appConfig.imageNarrationEnabled() &&
                       appConfig.llmConfigured();
  portENTER_CRITICAL(&narrationMux);
  if (!enabled) {
    discarded = queuedRegenerationJob;
    queuedRegenerationJob = nullptr;
  }
  if (initialized && !fetchRunning && queuedConfigTestJob &&
      configTestStatus.state == LlmConfigTestState::Queued) {
    job = queuedConfigTestJob;
    queuedConfigTestJob = nullptr;
    fetchRunning = true;
    configTestStatus.state = LlmConfigTestState::Running;
  } else if (initialized && !fetchRunning && !resultPending &&
             queuedRegenerationJob) {
    job = queuedRegenerationJob;
    queuedRegenerationJob = nullptr;
    fetchRunning = true;
    mediaReadInProgress = true;
    activePathHash = hashPath(job->imagePath);
    copyText(activeImagePath, sizeof(activeImagePath), job->imagePath);
    activeInvalidated = false;
    activeRegeneration = true;
  }
  portEXIT_CRITICAL(&narrationMux);
  disposeNarrationJob(discarded);

  if (!job) {
    return;
  }
  if (xTaskCreatePinnedToCore(narrationTask, "llm-queued",
                              NARRATION_TASK_STACK_BYTES, job, 1, nullptr,
                              0) != pdPASS) {
    const uint32_t failedTestId = job->testId;
    const bool configTest = job->kind == JobKind::ConfigTest;
    const uint32_t pathHash = hashPath(job->imagePath);
    disposeNarrationJob(job);
    if (configTest) {
      finishConfigTest(failedTestId, false, LlmConfigTestFailure::Internal, 0);
    } else {
      portENTER_CRITICAL(&narrationMux);
      mediaReadInProgress = false;
      portEXIT_CRITICAL(&narrationMux);
      finishJob({}, pathHash, false);
    }
    Serial.println("[llm] unable to start queued worker task");
  }
}

void llmNarrationInvalidateAll() {
  portENTER_CRITICAL(&narrationMux);
  invalidateNarrationsLocked(nullptr);
  NarrationJob* discarded = queuedRegenerationJob;
  queuedRegenerationJob = nullptr;
  portEXIT_CRITICAL(&narrationMux);
  disposeNarrationJob(discarded);
}

bool llmNarrationRequest(uint32_t pageId, const String& imagePath,
                         const String& baseUrl, const String& apiKey,
                         const String& model,
                         const String& narrationPrompt, bool regenerate) {
  if (!initialized || pageId == 0 || imagePath.isEmpty() || baseUrl.isEmpty() ||
      apiKey.isEmpty() || model.isEmpty() ||
      !validLlmNarrationPrompt(narrationPrompt) ||
      WiFi.status() != WL_CONNECTED ||
      imagePath.length() >= JOB_PATH_BYTES ||
      baseUrl.length() >= JOB_BASE_URL_BYTES ||
      apiKey.length() >= JOB_API_KEY_BYTES ||
      model.length() >= JOB_MODEL_BYTES) {
    return false;
  }

  const uint32_t pathHash = hashPath(imagePath.c_str());
  const uint32_t now = millis();
  portENTER_CRITICAL(&narrationMux);
  const bool accepted = regenerate
      ? !queuedRegenerationJob &&
            !(activeRegeneration && strcmp(activeImagePath, imagePath.c_str()) == 0)
      : !fetchRunning && !resultPending && !queuedRegenerationJob &&
            !configTestBusy() && retryAllowed(pathHash, now);
  if (accepted && !regenerate) {
    fetchRunning = true;
    mediaReadInProgress = true;
    activePathHash = pathHash;
    copyText(activeImagePath, sizeof(activeImagePath), imagePath);
    activeInvalidated = false;
    activeRegeneration = false;
  }
  portEXIT_CRITICAL(&narrationMux);
  if (!accepted) {
    return false;
  }

  NarrationJob* job = static_cast<NarrationJob*>(heap_caps_calloc(
      1, sizeof(NarrationJob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!job) {
    if (!regenerate) {
      portENTER_CRITICAL(&narrationMux);
      activePathHash = 0;
      activeImagePath[0] = '\0';
      fetchRunning = false;
      mediaReadInProgress = false;
      portEXIT_CRITICAL(&narrationMux);
    }
    return false;
  }
  job->pageId = pageId;
  copyText(job->imagePath, sizeof(job->imagePath), imagePath);
  copyText(job->baseUrl, sizeof(job->baseUrl), baseUrl);
  copyText(job->apiKey, sizeof(job->apiKey), apiKey);
  copyText(job->model, sizeof(job->model), model);
  copyText(job->narrationPrompt, sizeof(job->narrationPrompt),
           narrationPrompt);

  if (regenerate) {
    return queueRegenerationJob(job);
  }
  if (xTaskCreatePinnedToCore(narrationTask, "llm-narration",
                              NARRATION_TASK_STACK_BYTES, job, 1, nullptr,
                              0) != pdPASS) {
    wipeMemory(job, sizeof(*job));
    heap_caps_free(job);
    portENTER_CRITICAL(&narrationMux);
    activePathHash = 0;
    activeImagePath[0] = '\0';
    fetchRunning = false;
    mediaReadInProgress = false;
    portEXIT_CRITICAL(&narrationMux);
    Serial.println("[narration] unable to start worker task");
    return false;
  }
  return true;
}

bool llmNarrationRequestRgb565Alpha(
    uint32_t pageId, const String& imagePath, const uint8_t* pixels,
    uint16_t width, uint16_t height, const String& baseUrl,
    const String& apiKey, const String& model,
    const String& narrationPrompt, bool regenerate) {
  if (!initialized || pageId == 0 || imagePath.isEmpty() || !pixels ||
      width == 0 || height == 0 || width > 480 || height > 480 ||
      baseUrl.isEmpty() || apiKey.isEmpty() || model.isEmpty() ||
      !validLlmNarrationPrompt(narrationPrompt) ||
      WiFi.status() != WL_CONNECTED || imagePath.length() >= JOB_PATH_BYTES ||
      baseUrl.length() >= JOB_BASE_URL_BYTES ||
      apiKey.length() >= JOB_API_KEY_BYTES ||
      model.length() >= JOB_MODEL_BYTES) {
    return false;
  }

  const uint32_t pathHash = hashPath(imagePath.c_str());
  const uint32_t now = millis();
  portENTER_CRITICAL(&narrationMux);
  const bool accepted = regenerate
      ? !queuedRegenerationJob &&
            !(activeRegeneration && strcmp(activeImagePath, imagePath.c_str()) == 0)
      : !fetchRunning && !resultPending && !queuedRegenerationJob &&
            !configTestBusy() && retryAllowed(pathHash, now);
  if (accepted && !regenerate) {
    fetchRunning = true;
    mediaReadInProgress = true;
    activePathHash = pathHash;
    copyText(activeImagePath, sizeof(activeImagePath), imagePath);
    activeInvalidated = false;
    activeRegeneration = false;
  }
  portEXIT_CRITICAL(&narrationMux);
  if (!accepted) {
    return false;
  }

  NarrationJob* job = static_cast<NarrationJob*>(heap_caps_calloc(
      1, sizeof(NarrationJob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const size_t frameBytes = static_cast<size_t>(width) * height * 3U;
  uint8_t* frameCopy = static_cast<uint8_t*>(
      heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!job || !frameCopy) {
    if (frameCopy) {
      heap_caps_free(frameCopy);
    }
    if (job) {
      heap_caps_free(job);
    }
    if (!regenerate) {
      portENTER_CRITICAL(&narrationMux);
      activePathHash = 0;
      activeImagePath[0] = '\0';
      fetchRunning = false;
      mediaReadInProgress = false;
      portEXIT_CRITICAL(&narrationMux);
    }
    return false;
  }

  memcpy(frameCopy, pixels, frameBytes);
  job->pageId = pageId;
  job->rgb565Alpha = frameCopy;
  job->rgb565AlphaBytes = frameBytes;
  job->frameWidth = width;
  job->frameHeight = height;
  copyText(job->imagePath, sizeof(job->imagePath), imagePath);
  copyText(job->baseUrl, sizeof(job->baseUrl), baseUrl);
  copyText(job->apiKey, sizeof(job->apiKey), apiKey);
  copyText(job->model, sizeof(job->model), model);
  copyText(job->narrationPrompt, sizeof(job->narrationPrompt),
           narrationPrompt);

  if (regenerate) {
    return queueRegenerationJob(job);
  }
  if (xTaskCreatePinnedToCore(narrationTask, "llm-narration",
                              NARRATION_TASK_STACK_BYTES, job, 1, nullptr,
                              0) != pdPASS) {
    heap_caps_free(frameCopy);
    wipeMemory(job, sizeof(*job));
    heap_caps_free(job);
    portENTER_CRITICAL(&narrationMux);
    activePathHash = 0;
    activeImagePath[0] = '\0';
    fetchRunning = false;
    mediaReadInProgress = false;
    portEXIT_CRITICAL(&narrationMux);
    Serial.println("[narration] unable to start GIF worker task");
    return false;
  }
  return true;
}

bool llmNarrationBusyForImage(const String& imagePath) {
  if (imagePath.isEmpty()) {
    return false;
  }
  portENTER_CRITICAL(&narrationMux);
  const bool busy =
      (queuedRegenerationJob &&
       strcmp(queuedRegenerationJob->imagePath, imagePath.c_str()) == 0) ||
      (fetchRunning && !activeInvalidated &&
       strcmp(activeImagePath, imagePath.c_str()) == 0) ||
      (resultPending &&
       strcmp(pendingResult.imagePath, imagePath.c_str()) == 0);
  portEXIT_CRITICAL(&narrationMux);
  return busy;
}

bool llmNarrationTakeResult(uint32_t& pageId, String& imagePath,
                            String& narration) {
  NarrationResult result;
  const uint32_t now = millis();
  portENTER_CRITICAL(&narrationMux);
  const bool available = resultPending &&
      (resultRetryAt == 0 || static_cast<int32_t>(now - resultRetryAt) >= 0);
  if (available) {
    result = pendingResult;
    resultRetryAt = now + RESULT_SAVE_RETRY_MS;
    if (resultRetryAt == 0) {
      resultRetryAt = 1;
    }
  }
  portEXIT_CRITICAL(&narrationMux);
  if (!available) {
    return false;
  }
  pageId = result.pageId;
  imagePath = result.imagePath;
  narration = result.narration;
  return true;
}

void llmNarrationAcknowledgeResult() {
  portENTER_CRITICAL(&narrationMux);
  pendingResult = {};
  resultPending = false;
  resultRetryAt = 0;
  portEXIT_CRITICAL(&narrationMux);
}

bool llmNarrationMediaReadInProgress() {
  portENTER_CRITICAL(&narrationMux);
  const bool active = mediaReadInProgress;
  portEXIT_CRITICAL(&narrationMux);
  return active;
}
