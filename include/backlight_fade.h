#pragma once

#include <cstdint>

// Non-blocking fade used when the configured screen-off window begins. The
// caller advances it with millis(); keeping the timing logic hardware-free
// makes the curve deterministic and host-testable.
class ScheduledBacklightFade {
 public:
  void begin(uint32_t now, uint8_t visiblePercent) {
    startedAt_ = now;
    lastStepAt_ = now;
    startPercent_ = visiblePercent > 100 ? 100 : visiblePercent;
    percent_ = startPercent_;
    active_ = startPercent_ != 0;
  }

  bool update(uint32_t now) {
    if (!active_) {
      return false;
    }

    const uint32_t elapsed = now - startedAt_;
    if (elapsed < DURATION_MS && now - lastStepAt_ < STEP_MS) {
      return false;
    }
    lastStepAt_ = now;

    const uint8_t remaining = remainingPercent(elapsed);
    uint8_t next = static_cast<uint8_t>(
        (static_cast<uint16_t>(startPercent_) * remaining + 50U) / 100U);
    if (elapsed < DURATION_MS && next == 0) {
      next = 1;
    }
    if (elapsed >= DURATION_MS) {
      next = 0;
      active_ = false;
    }

    if (next == percent_) {
      return false;
    }
    percent_ = next;
    return true;
  }

  void cancel() {
    active_ = false;
    startPercent_ = 100;
    percent_ = 100;
    startedAt_ = 0;
    lastStepAt_ = 0;
  }

  bool active() const { return active_; }
  uint8_t percent() const { return percent_; }
  uint8_t effectivePercent(bool scheduledScreenOff,
                           uint8_t normalTransitionPercent) const {
    const uint8_t normal =
        normalTransitionPercent > 100 ? 100 : normalTransitionPercent;
    return scheduledScreenOff ? percent_ : normal;
  }

 private:
  static constexpr uint32_t DURATION_MS = 600;
  static constexpr uint32_t STEP_MS = 12;
  static constexpr uint16_t EASING_SCALE = 1024;

  static uint8_t remainingPercent(uint32_t elapsed) {
    if (elapsed >= DURATION_MS) {
      return 0;
    }

    const uint16_t progress = static_cast<uint16_t>(
        (static_cast<uint64_t>(elapsed) * EASING_SCALE) / DURATION_MS);
    const uint64_t inverse = EASING_SCALE - progress;
    const uint64_t inverseCubed = inverse * inverse * inverse;
    const uint16_t eased = EASING_SCALE -
                           static_cast<uint16_t>(
                               inverseCubed /
                               (static_cast<uint64_t>(EASING_SCALE) *
                                EASING_SCALE));
    const uint8_t easedPercent = static_cast<uint8_t>(
        (static_cast<uint32_t>(eased) * 100U + EASING_SCALE / 2U) /
        EASING_SCALE);
    return 100U - easedPercent;
  }

  uint32_t startedAt_ = 0;
  uint32_t lastStepAt_ = 0;
  uint8_t startPercent_ = 100;
  uint8_t percent_ = 100;
  bool active_ = false;
};
