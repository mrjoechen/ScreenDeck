#pragma once

#include <Arduino.h>

#include "llm_config_test_status.h"

constexpr size_t LLM_NARRATION_MAX_CODEPOINTS = 64;

// Image analysis runs on a low-priority FreeRTOS task. Callers hand it a
// complete immutable snapshot so the worker never reads AppConfig across
// cores. At most one image is analyzed at a time.
void llmNarrationBegin();

// Queue a one-shot vision request using the supplied, possibly unsaved,
// settings. The worker uses an embedded image and publishes only a sanitized
// status; it never updates the narration cache or AppConfig.
bool llmNarrationTestStart(const String& baseUrl, const String& apiKey,
                           const String& model,
                           const String& narrationPrompt,
                           const String& requestToken, uint32_t& testId);
bool llmNarrationTestStatus(uint32_t testId, LlmConfigTestStatus& status);
bool llmNarrationTestStatusByRequestToken(
    const String& requestToken, LlmConfigTestStatus& status);
bool llmNarrationTestBusy();

// Starts a queued configuration test as soon as the shared HTTP worker is
// free. Calling this from Arduino's loop keeps web handlers non-blocking.
void llmNarrationLoop();
// Discard queued/pending summaries and obsolete in-flight results after a
// successful cache reset. The current worker keeps its slot until it exits.
void llmNarrationInvalidateAll();
// Exact-path status, including queued work and successful results awaiting
// local persistence. Read under the worker mutex; no config/cache mutation.
bool llmNarrationBusyForImage(const String& imagePath);
// regenerate queues one explicit replacement even while the worker is busy,
// superseding that image's old in-flight result without clearing its saved
// narration. False means invalid/offline, allocation failure, or a full queue.
bool llmNarrationRequest(uint32_t pageId, const String& imagePath,
                         const String& baseUrl, const String& apiKey,
                         const String& model,
                         const String& narrationPrompt,
                         bool regenerate = false);

// OpenAI-compatible vision endpoints generally accept only non-animated GIFs.
// For a GIF already decoded by LVGL, copy its visible RGB565+alpha frame and
// let the worker encode that still frame as JPEG before uploading it.
bool llmNarrationRequestRgb565Alpha(
    uint32_t pageId, const String& imagePath, const uint8_t* pixels,
    uint16_t width, uint16_t height, const String& baseUrl,
    const String& apiKey, const String& model,
    const String& narrationPrompt, bool regenerate = false);

// Successful results are retained until acknowledged after persistence (or
// rejection of a deleted page). If not acknowledged, TakeResult retries only
// the local save after a delay; it never repeats the paid HTTP request.
// Both calls belong on Arduino's main loop, like AppConfig/LittleFS writes.
bool llmNarrationTakeResult(uint32_t& pageId, String& imagePath,
                            String& narration);
void llmNarrationAcknowledgeResult();

// True only while the worker is copying/encoding its source file. Destructive
// media operations can defer until the handle has been closed.
bool llmNarrationMediaReadInProgress();
