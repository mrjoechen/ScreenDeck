#pragma once

#include <cstdint>

// The overlay is a screen-level sibling of the sliding page panes. Its visual
// presentation therefore has to be synchronized explicitly when the incoming
// page differs from the outgoing page.
struct PlaybackOverlayPresentation {
  bool visible = false;
  bool showClock = false;
  bool showWeather = false;
  bool darkBackground = false;
  uint32_t textColor = 0;
  uint16_t width = 0;
  uint16_t height = 0;
};

inline bool operator==(const PlaybackOverlayPresentation& left,
                       const PlaybackOverlayPresentation& right) {
  return left.visible == right.visible &&
         left.showClock == right.showClock &&
         left.showWeather == right.showWeather &&
         left.darkBackground == right.darkBackground &&
         left.textColor == right.textColor && left.width == right.width &&
         left.height == right.height;
}

inline bool operator!=(const PlaybackOverlayPresentation& left,
                       const PlaybackOverlayPresentation& right) {
  return !(left == right);
}

inline PlaybackOverlayPresentation playbackOverlayPresentation(
    bool onImage, uint32_t contentColor, bool showDateTime,
    bool weatherEnabled, bool /*weatherAvailable*/) {
  PlaybackOverlayPresentation presentation;
  presentation.showClock = showDateTime;
  // Keep the configured weather area visible while the first fetch is pending
  // or temporarily unavailable. The renderer already has a dedicated
  // unavailable icon and "--°C" placeholder for this state.
  presentation.showWeather = onImage && weatherEnabled;
  presentation.visible =
      presentation.showClock || presentation.showWeather;
  if (!presentation.visible) {
    return {};
  }

  presentation.darkBackground = onImage;
  presentation.textColor = onImage ? 0xFFFFFF : contentColor;
  if (presentation.showClock && presentation.showWeather) {
    presentation.width = 220;
    presentation.height = 104;
  } else if (presentation.showClock) {
    presentation.width = 184;
    presentation.height = 104;
  } else {
    presentation.width = 72;
    presentation.height = 82;
  }
  return presentation;
}
