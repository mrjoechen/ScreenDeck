#!/usr/bin/env python3
"""Host-side behavior tests for playback overlay presentation changes."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PlaybackOverlayPresentationTests(unittest.TestCase):
    def test_page_changes_rebuild_only_when_the_overlay_presentation_changes(self) -> None:
        source = textwrap.dedent(
            r'''
            #include <cassert>
            #include <cstdint>

            #include "playback_overlay_presentation.h"

            int main() {
              const auto hidden = playbackOverlayPresentation(
                  false, 0x112233, false, false, false);
              assert(!hidden.visible);
              assert(hidden.width == 0);
              assert(hidden.height == 0);

              const auto textClock = playbackOverlayPresentation(
                  false, 0x112233, true, true, true);
              assert(textClock.visible);
              assert(textClock.showClock);
              assert(!textClock.showWeather);
              assert(!textClock.darkBackground);
              assert(textClock.textColor == 0x112233);
              assert(textClock.width == 184);
              assert(textClock.height == 104);

              const auto imageClockAndWeather = playbackOverlayPresentation(
                  true, 0x112233, true, true, true);
              assert(imageClockAndWeather.visible);
              assert(imageClockAndWeather.showClock);
              assert(imageClockAndWeather.showWeather);
              assert(imageClockAndWeather.darkBackground);
              assert(imageClockAndWeather.textColor == 0xFFFFFF);
              assert(imageClockAndWeather.width == 220);
              assert(imageClockAndWeather.height == 104);
              assert(textClock != imageClockAndWeather);

              const auto textClockWithAnotherColor = playbackOverlayPresentation(
                  false, 0xF4EFE6, true, true, true);
              assert(textClock != textClockWithAnotherColor);

              const auto weatherOnlyText = playbackOverlayPresentation(
                  false, 0x112233, false, true, true);
              assert(!weatherOnlyText.visible);

              const auto weatherOnlyImage = playbackOverlayPresentation(
                  true, 0x112233, false, true, true);
              assert(weatherOnlyImage.visible);
              assert(!weatherOnlyImage.showClock);
              assert(weatherOnlyImage.showWeather);
              assert(weatherOnlyImage.width == 72);
              assert(weatherOnlyImage.height == 82);

              const auto sameImagePresentation = playbackOverlayPresentation(
                  true, 0x000000, true, true, true);
              assert(imageClockAndWeather == sameImagePresentation);

              const auto imageWithoutSnapshot = playbackOverlayPresentation(
                  true, 0x112233, true, true, false);
              assert(imageWithoutSnapshot.visible);
              assert(imageWithoutSnapshot.showClock);
              assert(imageWithoutSnapshot.showWeather);
              assert(imageWithoutSnapshot.width == 220);

              const auto weatherOnlyImageWithoutSnapshot =
                  playbackOverlayPresentation(
                      true, 0x112233, false, true, false);
              assert(weatherOnlyImageWithoutSnapshot.visible);
              assert(!weatherOnlyImageWithoutSnapshot.showClock);
              assert(weatherOnlyImageWithoutSnapshot.showWeather);
              assert(weatherOnlyImageWithoutSnapshot.width == 72);
              return 0;
            }
            '''
        )
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "playback-overlay-presentation"
            compiled = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "include"),
                    "-x",
                    "c++",
                    "-",
                    "-o",
                    str(executable),
                ],
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run(
                [str(executable)], capture_output=True, text=True
            )
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
