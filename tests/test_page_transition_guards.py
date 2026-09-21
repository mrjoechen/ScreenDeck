#!/usr/bin/env python3
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
DISPLAY_SOURCE = (ROOT / "src" / "display_ui.cpp").read_text()
DISPLAY_HEADER = (ROOT / "include" / "display_ui.h").read_text()
STACK_SOURCE = (ROOT / "src" / "esp_display_stack.cpp").read_text()
STACK_HEADER = (ROOT / "include" / "esp_display_stack.h").read_text()
APP_CONFIG_HEADER = (ROOT / "include" / "app_config.h").read_text()
APP_CONFIG_SOURCE = (ROOT / "src" / "app_config.cpp").read_text()
WEB_SOURCE = (ROOT / "src" / "web_portal.cpp").read_text()
WEB_UI_SOURCE = (ROOT / "include" / "web_ui.h").read_text()
MAIN_SOURCE = (ROOT / "src" / "main.cpp").read_text()


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
        self.assertIn("lv_scr_act()", slide)
        self.assertIn("captureOutgoingPane(", slide)
        self.assertIn("populatePageContent(", slide)
        self.assertIn("lv_obj_move_foreground(playbackClockCard)", slide)
        self.assertNotIn("createScreen(", slide)
        self.assertNotIn("loadScreen(", slide)
        self.assertNotIn("addPlaybackClock(", slide)
        self.assertNotIn("SCREEN_WIDTH * 2", slide)
        self.assertNotIn("LV_SCR_LOAD_ANIM_MOVE", slide)

        finish = function_body(DISPLAY_SOURCE, "void finishSlideTransition(")
        self.assertNotIn("renderCurrent()", finish)
        self.assertIn("settleSlidePanes()", finish)

        created = function_body(DISPLAY_SOURCE, "lv_obj_t* createScreen(")
        self.assertIn("forgetSlideWidgets()", created)

        transition = function_body(DISPLAY_SOURCE, "void updatePageTransition(")
        self.assertIn("PageTransitionPhase::Sliding", transition)
        self.assertIn("applySlideShift(", transition)
        self.assertIn("finishSlideTransition(now)", transition)

    def test_slide_does_not_read_flash_on_the_live_swipe_path(self) -> None:
        ready = function_body(DISPLAY_SOURCE, "bool ensureSlideImageReady(")
        self.assertIn("findCachedImage(page.imagePath)", ready)
        self.assertNotIn("loadCachedImage", ready)

        slide = function_body(DISPLAY_SOURCE, "bool startSlideTransition(")
        self.assertNotIn("loadCachedImage", slide)

        loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        warm = loop.index("warmSlideImages(")
        pause = loop.index("espDisplayStackPause()", warm - 400)
        resume = loop.index("espDisplayStackResume()", warm)
        change = loop.rindex("changePage(")
        self.assertLess(pause, warm)
        self.assertLess(warm, resume)
        self.assertLess(resume, change)
        self.assertIn("slideNeedsWarm(", loop)

    def test_pending_swipe_is_kept_until_the_current_transition_idles(self) -> None:
        loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        swipe = loop.index("pendingSwipe != 0")
        idle = loop.index(
            "pageTransitionPhase == PageTransitionPhase::Idle", swipe
        )
        consume = loop.index("pendingSwipe = 0", swipe)
        self.assertLess(idle, consume)
        swipe_guard = loop[swipe : loop.index(") {", swipe)]
        self.assertIn("!contentWasRendered", swipe_guard)
        self.assertIn("lastPlaybackDirection", DISPLAY_SOURCE)

        preload = function_body(DISPLAY_SOURCE, "size_t nextImagePreloadPage(")
        self.assertIn("lastPlaybackDirection", preload)
        self.assertIn("adjacentPage(centerPage, firstDir, pageCount)", preload)

    def test_neighbour_preload_starts_once_the_current_frame_has_settled(self) -> None:
        delay = __import__("re").search(
            r"IMAGE_PRELOAD_IDLE_DELAY_MS\s*=\s*(\d+)", DISPLAY_SOURCE
        )
        interval = __import__("re").search(
            r"IMAGE_PRELOAD_MIN_INTERVAL_MS\s*=\s*(\d+)", DISPLAY_SOURCE
        )
        self.assertIsNotNone(delay)
        self.assertIsNotNone(interval)
        self.assertLessEqual(int(delay.group(1)), 100)
        self.assertLessEqual(int(interval.group(1)), 80)
        self.assertGreaterEqual(int(delay.group(1)), 50)

    def test_slide_motion_is_not_preempted_by_clock_or_network_work(self) -> None:
        loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        clock_block = loop[loop.index("lastClockRefreshAt") :]
        self.assertIn("updateLiveClock()", clock_block)
        self.assertRegex(
            clock_block,
            r"if \(!warmBeforeChange &&\s*"
            r"pageTransitionPhase != PageTransitionPhase::Sliding\)",
        )
        dirty = loop[loop.index("contentDirty") : loop.index("contentDirty = false")]
        self.assertIn("pageTransitionPhase == PageTransitionPhase::Idle", dirty)
        self.assertIn("bool displaySlideInProgress();", DISPLAY_HEADER)
        self.assertIn("displaySlideInProgress()", MAIN_SOURCE)
        self.assertLess(
            MAIN_SOURCE.index("displaySlideInProgress()"),
            MAIN_SOURCE.index("webPortal.loop()"),
        )

        warm_branch = loop[
            loop.index("if (slideNeedsWarm(") : loop.index(
                "} else {", loop.index("if (slideNeedsWarm(")
            )
        ]
        self.assertIn("finishPlaybackClockFade()", warm_branch)
        self.assertRegex(
            loop,
            r"if \(!contentWasRendered && !warmBeforeChange\)\s*"
            r"\{\s*updatePlaybackClockPosition\(millis\(\)\);",
        )

    def test_slide_synchronizes_overlay_and_releases_stale_page_state(self) -> None:
        slide = function_body(DISPLAY_SOURCE, "bool startSlideTransition(")
        self.assertIn("finishPlaybackClockFade()", slide)
        self.assertIn("syncPlaybackOverlay(screen, incomingContent)", slide)
        finish_fade = slide.index("finishPlaybackClockFade()")
        capture = slide.index("captureOutgoingPane(")
        sync_overlay = slide.index(
            "syncPlaybackOverlay(screen, incomingContent)"
        )
        foreground = slide.index("lv_obj_move_foreground(playbackClockCard)")
        self.assertLess(finish_fade, capture)
        self.assertLess(capture, sync_overlay)
        self.assertLess(sync_overlay, foreground)
        self.assertIn(
            'incomingContent.type != PageType::Image', slide
        )
        self.assertIn('activeCachedImagePath = ""', slide)

        finish_clock_fade = function_body(
            DISPLAY_SOURCE, "void finishPlaybackClockFade()"
        )
        self.assertIn("lv_anim_del(playbackClockCard, nullptr)", finish_clock_fade)
        self.assertIn("LV_STYLE_OPA", finish_clock_fade)

    def test_slide_clock_avoids_narration_on_either_visible_pane(self) -> None:
        slide = " ".join(
            function_body(DISPLAY_SOURCE, "bool startSlideTransition(").split()
        )
        outgoing = slide.index(
            "const bool outgoingNarrationVisible = imageNarrationVisible"
        )
        populate = slide.index("populatePageContent(incoming, incomingContent)")
        remember_incoming = slide.index(
            "slideIncomingNarrationVisible = imageNarrationVisible"
        )
        combine = slide.index(
            "imageNarrationVisible = outgoingNarrationVisible || "
            "slideIncomingNarrationVisible"
        )
        sync_overlay = slide.index("syncPlaybackOverlay(screen, incomingContent)")
        self.assertLess(outgoing, populate)
        self.assertLess(populate, remember_incoming)
        self.assertLess(remember_incoming, combine)
        self.assertLess(combine, sync_overlay)

        settle = " ".join(
            function_body(DISPLAY_SOURCE, "void settleSlidePanes() {").split()
        )
        restore = settle.index(
            "imageNarrationVisible = slideIncomingNarrationVisible"
        )
        realign = settle.index("alignPlaybackClockCard(playbackClockCard)")
        self.assertLess(restore, realign)

    def test_settings_screen_cannot_start_a_playback_transition(self) -> None:
        change_page = function_body(DISPLAY_SOURCE, "void changePage(")
        self.assertIn("deviceSettingsScreen", change_page)

        loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        auto_advance = loop[
            loop.index("queuedDirection == 0") : loop.index(
                "queuedDirection = 1", loop.index("queuedDirection == 0")
            )
        ]
        self.assertIn("!deviceSettingsScreen", auto_advance)

    def test_down_swipe_opens_device_settings_from_any_start_position(self) -> None:
        touch = function_body(DISPLAY_SOURCE, "void readTouch(")
        self.assertIn("dy > SWIPE_THRESHOLD", touch)
        self.assertIn(
            "pendingScreenRequest = ScreenRequest::DeviceSettings", touch
        )
        self.assertNotIn("touchStartY <= TOP_EDGE_SWIPE_ZONE", touch)


if __name__ == "__main__":
    unittest.main(verbosity=2)
