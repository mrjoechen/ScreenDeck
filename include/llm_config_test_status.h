#pragma once

#include <cstddef>
#include <cstdint>

enum class LlmConfigTestState : uint8_t {
  Idle,
  Queued,
  Running,
  Passed,
  Failed,
};

// Public test results deliberately use a small fixed vocabulary. Upstream
// response bodies can contain sensitive proxy details and are never exposed to
// the browser.
enum class LlmConfigTestFailure : uint8_t {
  None,
  Offline,
  Connection,
  Timeout,
  Authentication,
  NotFound,
  RateLimited,
  Upstream,
  RequestRejected,
  InvalidResponse,
  Internal,
};

constexpr size_t LLM_CONFIG_TEST_REQUEST_TOKEN_HEX_LENGTH = 32;
constexpr size_t LLM_CONFIG_TEST_REQUEST_TOKEN_BYTES =
    LLM_CONFIG_TEST_REQUEST_TOKEN_HEX_LENGTH + 1;

struct LlmConfigTestStatus {
  uint32_t id = 0;
  char requestToken[LLM_CONFIG_TEST_REQUEST_TOKEN_BYTES] = "";
  LlmConfigTestState state = LlmConfigTestState::Idle;
  LlmConfigTestFailure failure = LlmConfigTestFailure::None;
  int httpStatus = 0;
};

inline bool llmConfigTestRequestTokenValid(const char* token) {
  if (!token) {
    return false;
  }
  bool nonzero = false;
  for (size_t index = 0; index < LLM_CONFIG_TEST_REQUEST_TOKEN_HEX_LENGTH;
       ++index) {
    const char value = token[index];
    const bool hexadecimal =
        (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
    if (!hexadecimal) {
      return false;
    }
    nonzero = nonzero || value != '0';
  }
  return token[LLM_CONFIG_TEST_REQUEST_TOKEN_HEX_LENGTH] == '\0' && nonzero;
}

inline bool llmConfigTestRequestTokenMatches(
    const LlmConfigTestStatus& status, const char* token) {
  if (!llmConfigTestRequestTokenValid(token)) {
    return false;
  }
  for (size_t index = 0; index < LLM_CONFIG_TEST_REQUEST_TOKEN_BYTES;
       ++index) {
    if (status.requestToken[index] != token[index]) {
      return false;
    }
  }
  return true;
}

inline LlmConfigTestFailure llmConfigTestFailureForHttpStatus(int status) {
  if (status == 401 || status == 403) {
    return LlmConfigTestFailure::Authentication;
  }
  if (status == 404) {
    return LlmConfigTestFailure::NotFound;
  }
  if (status == 408 || status == 504) {
    return LlmConfigTestFailure::Timeout;
  }
  if (status == 429) {
    return LlmConfigTestFailure::RateLimited;
  }
  if (status >= 500 && status <= 599) {
    return LlmConfigTestFailure::Upstream;
  }
  return LlmConfigTestFailure::RequestRejected;
}

inline const char* llmConfigTestStateCode(LlmConfigTestState state) {
  switch (state) {
    case LlmConfigTestState::Queued:
      return "queued";
    case LlmConfigTestState::Running:
      return "running";
    case LlmConfigTestState::Passed:
      return "passed";
    case LlmConfigTestState::Failed:
      return "failed";
    case LlmConfigTestState::Idle:
    default:
      return "idle";
  }
}

inline const char* llmConfigTestFailureCode(LlmConfigTestFailure failure) {
  switch (failure) {
    case LlmConfigTestFailure::Offline:
      return "offline";
    case LlmConfigTestFailure::Connection:
      return "connection";
    case LlmConfigTestFailure::Timeout:
      return "timeout";
    case LlmConfigTestFailure::Authentication:
      return "authentication";
    case LlmConfigTestFailure::NotFound:
      return "not_found";
    case LlmConfigTestFailure::RateLimited:
      return "rate_limited";
    case LlmConfigTestFailure::Upstream:
      return "upstream";
    case LlmConfigTestFailure::RequestRejected:
      return "request_rejected";
    case LlmConfigTestFailure::InvalidResponse:
      return "invalid_response";
    case LlmConfigTestFailure::Internal:
      return "internal";
    case LlmConfigTestFailure::None:
    default:
      return "none";
  }
}
