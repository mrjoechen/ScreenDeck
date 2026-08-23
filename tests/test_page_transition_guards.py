#!/usr/bin/env python3
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
DISPLAY_SOURCE = (ROOT / "src" / "display_ui.cpp").read_text()
STACK_SOURCE = (ROOT / "src" / "esp_display_stack.cpp").read_text()
STACK_HEADER = (ROOT / "include" / "esp_display_stack.h").read_text()
APP_CONFIG_HEADER = (ROOT / "include" / "app_config.h").read_text()
APP_CONFIG_SOURCE = (ROOT / "src" / "app_config.cpp").read_text()
WEB_SOURCE = (ROOT / "src" / "web_portal.cpp").read_text()
WEB_UI_SOURCE = (ROOT / "include" / "web_ui.h").read_text()


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

    def test_slide_transition_is_opt_in_and_falls_back_to_fade(self) -> None:
        self.assertIn("enum class PageTransitionStyle", APP_CONFIG_HEADER)
        self.assertIn("SlideHorizontal", APP_CONFIG_HEADER)
        self.assertIn('doc["pageTransition"] | "fade"', APP_CONFIG_SOURCE)
        self.assertIn('doc["pageTransition"] = pageTransitionStyleCode()', APP_CONFIG_SOURCE)
        self.assertIn('doc["pageTransition"] = appConfig.pageTransitionStyleCode()', WEB_SOURCE)
        self.assertIn('server.on("/api/page-transition"', WEB_SOURCE)
        self.assertIn('id="pageTransitionSwitch"', WEB_UI_SOURCE)
        self.assertIn("SettingsAction::CyclePageTransition", DISPLAY_SOURCE)

        change_page = function_body(DISPLAY_SOURCE, "void changePage(")
        self.assertIn("PageTransitionStyle::SlideHorizontal", change_page)
        self.assertIn("startSlideTransition(direction, startedAt)", change_page)
        self.assertIn("startFadeTransition(direction, startedAt)", change_page)

        can_slide = function_body(DISPLAY_SOURCE, "bool pageCanUseSlide(")
        self.assertIn("mediaIsAnimatedPath(page.imagePath)", can_slide)
        self.assertIn("cacheableImagePath(page.imagePath)", can_slide)
        self.assertNotIn("cachedImagePresent(page.imagePath)", can_slide)

        slide = function_body(DISPLAY_SOURCE, "bool startSlideTransition(")
        self.assertIn("ensureSlideImageReady(outgoingPage)", slide)
        self.assertIn("ensureSlideImageReady(incomingContent)", slide)
        self.assertIn("SCREEN_WIDTH * 2", slide)
        self.assertIn("addPlaybackClock(screen", slide)
        self.assertLess(slide.index("addPlaybackClock(screen"), slide.index("loadScreen(screen)"))
        self.assertIn("lv_obj_move_foreground(playbackClockCard)", slide)
        self.assertNotIn("LV_SCR_LOAD_ANIM_MOVE", slide)

        finish = function_body(DISPLAY_SOURCE, "void finishSlideTransition(")
        self.assertNotIn("renderCurrent()", finish)

        transition = function_body(DISPLAY_SOURCE, "void updatePageTransition(")
        self.assertIn("PageTransitionPhase::Sliding", transition)
        self.assertIn("lv_obj_set_x(pageTransitionTrack", transition)
        self.assertIn("finishSlideTransition(now)", transition)

    def test_down_swipe_opens_device_settings_from_any_start_position(self) -> None:
        touch = function_body(DISPLAY_SOURCE, "void readTouch(")
        self.assertIn("dy > SWIPE_THRESHOLD", touch)
        self.assertIn(
            "pendingScreenRequest = ScreenRequest::DeviceSettings", touch
        )
        self.assertNotIn("touchStartY <= TOP_EDGE_SWIPE_ZONE", touch)


if __name__ == "__main__":
    unittest.main(verbosity=2)
