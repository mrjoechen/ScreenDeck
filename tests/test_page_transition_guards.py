#!/usr/bin/env python3
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
DISPLAY_SOURCE = (ROOT / "src" / "display_ui.cpp").read_text()
STACK_SOURCE = (ROOT / "src" / "esp_display_stack.cpp").read_text()
STACK_HEADER = (ROOT / "include" / "esp_display_stack.h").read_text()


def function_body(source: str, signature: str) -> str:
    signature_at = source.index(signature)
    body_at = source.index("{", signature_at)
    depth = 0
    for index in range(body_at, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[body_at + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


class PageTransitionGuards(unittest.TestCase):
    def test_new_frame_is_published_while_backlight_stays_black(self) -> None:
        self.assertIn("WaitingForFrame", DISPLAY_SOURCE)
        self.assertRegex(DISPLAY_SOURCE, r"PAGE_FRAME_SETTLE_MS\s*=\s*[4-9]\d")
        self.assertIn("bool espDisplayStackRefreshNow();", STACK_HEADER)
        self.assertIn("esp_lv_adapter_refresh_now(lvglDisplay)", STACK_SOURCE)

        transition = function_body(DISPLAY_SOURCE, "void updatePageTransition(")
        commit = transition.index("commitPageChange(pageTransitionDirection)")
        refresh = transition.index("espDisplayStackRefreshNow()", commit)
        wait_phase = transition.index(
            "pageTransitionPhase = PageTransitionPhase::WaitingForFrame", refresh
        )
        self.assertLess(commit, refresh)
        self.assertLess(refresh, wait_phase)

        self.assertIn(
            "pageTransitionPhase == PageTransitionPhase::WaitingForFrame",
            transition,
        )
        self.assertIn(
            "now - pageTransitionStartedAt < PAGE_FRAME_SETTLE_MS", transition
        )
        self.assertIn(
            "pageTransitionPhase = PageTransitionPhase::FadingIn", transition
        )
        self.assertIn("pageTransitionPercent = 0", transition)

    def test_down_swipe_opens_device_settings_from_any_start_position(self) -> None:
        touch = function_body(DISPLAY_SOURCE, "void readTouch(")
        self.assertIn("dy > SWIPE_THRESHOLD", touch)
        self.assertIn(
            "pendingScreenRequest = ScreenRequest::DeviceSettings", touch
        )
        self.assertNotIn("touchStartY <= TOP_EDGE_SWIPE_ZONE", touch)


if __name__ == "__main__":
    unittest.main(verbosity=2)
