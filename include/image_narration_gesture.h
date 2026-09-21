#pragma once

#include <cstdint>
#include <cstdlib>

// pageId == 0 rejects settings, blanked screens and page transitions. Each
// release also carries the page captured on press, so a page change cannot
// turn two unrelated touches into a regeneration request.
class ImageNarrationGesture {
 public:
  bool release(uint32_t pageId, uint32_t now, uint32_t duration,
               int16_t dx, int16_t dy, int16_t x, int16_t y) {
    if (pageId == 0 || duration > 500 || std::abs(dx) > 24 ||
        std::abs(dy) > 24) {
      reset();
      return false;
    }
    const bool doubleTap = pageId_ == pageId && now - releasedAt_ <= 600 &&
                           std::abs(x - x_) <= 48 && std::abs(y - y_) <= 48;
    if (doubleTap) {
      reset();
      return true;
    }
    pageId_ = pageId;
    releasedAt_ = now;
    x_ = x;
    y_ = y;
    return false;
  }

  void reset() { pageId_ = 0; }

 private:
  uint32_t pageId_ = 0;
  uint32_t releasedAt_ = 0;
  int16_t x_ = 0;
  int16_t y_ = 0;
};
