#!/usr/bin/env python3
"""Static regression guards for the ESP-IDF RGB display stack.

The panel continuously consumes pixels while image transitions and LittleFS
writes compete for the same external-memory bus. These checks keep ownership
of buffer swaps, VSYNC synchronization, and touch in Espressif's drivers.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
DISPLAY_SOURCE = (ROOT / "src" / "display_ui.cpp").read_text()
DISPLAY_STACK_SOURCE = (ROOT / "src" / "esp_display_stack.cpp").read_text()
WEB_SOURCE = (ROOT / "src" / "web_portal.cpp").read_text()
WEB_HEADER = (ROOT / "include" / "web_portal.h").read_text()
DISPLAY_HEADER = (ROOT / "include" / "display_ui.h").read_text()
OVERLAY_PRESENTATION_HEADER = (
    ROOT / "include" / "playback_overlay_presentation.h"
).read_text()
DISPLAY_STACK_HEADER = (ROOT / "include" / "esp_display_stack.h").read_text()
WEB_UI_SOURCE = (ROOT / "include" / "web_ui.h").read_text()
RAW_IMAGE_SOURCE = (ROOT / "src" / "raw_image.cpp").read_text() if (ROOT / "src" / "raw_image.cpp").exists() else ""
APP_CONFIG_SOURCE = (ROOT / "src" / "app_config.cpp").read_text()
APP_CONFIG_HEADER = (ROOT / "include" / "app_config.h").read_text()
MAIN_SOURCE = (ROOT / "src" / "main.cpp").read_text()
UI_FONT_SOURCE = (ROOT / "src" / "ui_font_misans_16.c").read_text()
EMOJI_FONT_PATH = ROOT / "src" / "ui_font_emoji_32.c"
EMOJI_FONT_SOURCE = EMOJI_FONT_PATH.read_text() if EMOJI_FONT_PATH.exists() else ""
PLATFORMIO_CONFIG = (ROOT / "platformio.ini").read_text()
IDF_MANIFEST = (ROOT / "src" / "idf_component.yml").read_text()
SDKCONFIG_DEFAULTS = (ROOT / "sdkconfig.defaults").read_text()
SDKCONFIG_TARGET = (ROOT / "sdkconfig.esp32s3").read_text()


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


class RgbCorruptionGuards(unittest.TestCase):
    def test_build_uses_esp_idf_managed_display_components(self) -> None:
        self.assertRegex(PLATFORMIO_CONFIG, r"framework\s*=\s*arduino,\s*espidf")
        self.assertNotIn("Arduino_GFX", PLATFORMIO_CONFIG)
        for component in (
            "espressif/esp_lvgl_adapter",
            "espressif/esp_lcd_st7701",
            "espressif/esp_lcd_panel_io_additions",
            "espressif/esp_lcd_touch_gt911",
        ):
            self.assertIn(component, IDF_MANIFEST)

    def test_panel_uses_normal_refresh_clock_and_dram_bounce_buffers(self) -> None:
        self.assertRegex(DISPLAY_STACK_SOURCE, r"RGB_BOUNCE_BUFFER_LINES\s*=\s*20")
        self.assertIn(
            "rgbConfig.bounce_buffer_size_px = RGB_BOUNCE_BUFFER_PIXELS",
            DISPLAY_STACK_SOURCE,
        )
        pclk = re.search(
            r"RGB_PIXEL_CLOCK_HZ\s*=\s*(\d+)", DISPLAY_STACK_SOURCE
        )
        self.assertIsNotNone(pclk)
        self.assertEqual(
            int(pclk.group(1)),
            16_000_000,
            "the 548 x 518 timing requires the validated 56 Hz panel clock",
        )

    def test_lvgl_uses_official_triple_full_anti_tearing(self) -> None:
        self.assertRegex(DISPLAY_STACK_SOURCE, r"RGB_FRAME_BUFFER_COUNT\s*=\s*3")
        self.assertIn(
            "rgbConfig.num_fbs = RGB_FRAME_BUFFER_COUNT", DISPLAY_STACK_SOURCE
        )
        self.assertIn("esp_lcd_rgb_panel_get_frame_buffer", DISPLAY_STACK_SOURCE)
        self.assertIn(
            "ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL", DISPLAY_STACK_SOURCE
        )
        self.assertIn("esp_lv_adapter_register_display", DISPLAY_STACK_SOURCE)

        for legacy_path in (
            "esp_lcd_panel_draw_bitmap",
            "esp_lcd_rgb_panel_restart",
            "lv_disp_draw_buf_init",
            "lv_disp_drv_register",
        ):
            self.assertNotIn(legacy_path, DISPLAY_SOURCE)

    def test_gt911_uses_the_esp_idf_touch_driver(self) -> None:
        self.assertIn("esp_lcd_touch_new_i2c_gt911", DISPLAY_STACK_SOURCE)
        self.assertIn("esp_lcd_touch_read_data", DISPLAY_STACK_SOURCE)
        self.assertIn("esp_lcd_touch_get_data", DISPLAY_STACK_SOURCE)
        self.assertNotIn("Wire.begin", DISPLAY_SOURCE)

    def test_image_pages_never_use_move_animation(self) -> None:
        self.assertRegex(
            DISPLAY_SOURCE,
            r"bool\s+isImagePage[\s\S]*?page\.type\s*==\s*PageType::Image",
        )
        load_screen = function_body(DISPLAY_SOURCE, "void loadScreen(")
        self.assertIn("LV_SCR_LOAD_ANIM_NONE", load_screen)
        self.assertNotIn("LV_SCR_LOAD_ANIM_MOVE_LEFT", DISPLAY_SOURCE)
        self.assertNotIn("LV_SCR_LOAD_ANIM_MOVE_RIGHT", DISPLAY_SOURCE)
        self.assertIn("PageTransitionPhase::Sliding", DISPLAY_SOURCE)
        self.assertIn("applySlideShift(", DISPLAY_SOURCE)
        self.assertIn("lv_obj_set_x(pageTransitionOutgoing", DISPLAY_SOURCE)
        can_slide = function_body(DISPLAY_SOURCE, "bool pageCanUseSlide(")
        self.assertIn("mediaIsAnimatedPath(page.imagePath)", can_slide)
        self.assertIn("cacheableImagePath(page.imagePath)", can_slide)
        self.assertNotIn("cachedImagePresent(page.imagePath)", can_slide)

    def test_upload_buffers_in_psram_before_gated_flash_commit(self) -> None:
        self.assertIn(
            "void displayBeginStorageWrite(bool blankBacklight = true);",
            DISPLAY_HEADER,
        )
        self.assertIn("void displayEndStorageWrite();", DISPLAY_HEADER)
        self.assertIn("uint8_t* uploadBuffer", WEB_SOURCE)
        self.assertIn("MALLOC_CAP_SPIRAM", WEB_SOURCE)

        start = WEB_SOURCE.index("UPLOAD_FILE_START")
        write = WEB_SOURCE.index("UPLOAD_FILE_WRITE", start)
        end = WEB_SOURCE.index("UPLOAD_FILE_END", write)
        start_path = WEB_SOURCE[start:write]
        streaming_path = WEB_SOURCE[write:end]
        self.assertNotIn("LittleFS.open", start_path)
        self.assertNotIn("displayBeginStorageWrite", start_path)
        self.assertNotIn("uploadFile.write", streaming_path)
        self.assertNotIn("LittleFS.open", streaming_path)
        self.assertIn("commitUploadBuffer()", WEB_SOURCE[end:])

        commit = function_body(WEB_SOURCE, "bool commitUploadBuffer()")
        pause = commit.find("displayBeginStorageWrite()")
        storage_commit = commit.find("mediaStoreCommitUpload")
        self.assertGreaterEqual(pause, 0)
        self.assertGreater(storage_commit, pause)
        self.assertNotIn("LittleFS.open", commit)
        self.assertIn("finishUploadDisplay", WEB_SOURCE)
        self.assertRegex(
            WEB_SOURCE,
            r"finishUploadDisplay[\s\S]{0,240}displayEndStorageWrite\(\)",
        )

    def test_uploaded_images_are_cropped_to_the_full_screen(self) -> None:
        self.assertIn("canvas.width=480", WEB_UI_SOURCE)
        self.assertIn("canvas.height=480", WEB_UI_SOURCE)
        self.assertRegex(
            WEB_UI_SOURCE,
            r"Math\.max\(480/image\.naturalWidth,480/image\.naturalHeight\)",
        )
        render_still = function_body(DISPLAY_SOURCE, "void renderStillImage(")
        self.assertIn("max<uint32_t>(zoomX, zoomY)", render_still)

        render_image = function_body(DISPLAY_SOURCE, "void renderImagePage(")
        self.assertIn("renderStillImage(screen, page)", render_image)
        populate_page = function_body(DISPLAY_SOURCE, "void populatePageContent(")
        self.assertIn("renderStillImage(parent, page)", populate_page)

    def test_browser_preconverts_uploads_to_rgb565(self) -> None:
        self.assertIn('name:stem+".rgb565"', WEB_UI_SOURCE)
        self.assertIn("new DataView", WEB_UI_SOURCE)
        self.assertRegex(WEB_UI_SOURCE, r"setUint16\([^,]+,[^,]+,true\)")
        self.assertIn("SDR5", WEB_UI_SOURCE)
        self.assertIn("rawImageInspect", WEB_SOURCE)
        self.assertIn("RAW_IMAGE_PIXEL_BYTES", RAW_IMAGE_SOURCE)

    def test_browser_supports_multi_image_selection_and_previews(self) -> None:
        self.assertRegex(
            WEB_UI_SOURCE,
            r'id="imageFile"[^>]+type="file"[^>]+multiple',
        )
        self.assertIn('id="uploadPreviews"', WEB_UI_SOURCE)
        self.assertIn("URL.createObjectURL(file)", WEB_UI_SOURCE)
        self.assertIn("removeSelectedImage", WEB_UI_SOURCE)
        self.assertIn("URL.revokeObjectURL", WEB_UI_SOURCE)

    def test_browser_uploads_selected_images_sequentially(self) -> None:
        submit = WEB_UI_SOURCE.index('$("#uploadForm").onsubmit')
        upload_loop = WEB_UI_SOURCE[submit:]
        self.assertIn("const queue=[...selectedImages]", upload_loop)
        self.assertIn("for(let index=0;index<queue.length;index++)", upload_loop)
        self.assertIn('await api("/api/upload"', upload_loop)
        self.assertNotIn("Promise.all(queue", upload_loop)

    def test_display_caches_current_and_adjacent_images_in_psram(self) -> None:
        self.assertRegex(DISPLAY_SOURCE, r"IMAGE_CACHE_SLOTS\s*=\s*3")
        self.assertIn("MALLOC_CAP_SPIRAM", DISPLAY_SOURCE)
        self.assertIn("preloadOneNeighbourImage(currentPage)", DISPLAY_SOURCE)
        self.assertIn("lv_img_decoder_open", DISPLAY_SOURCE)
        self.assertIn("lv_img_set_src(image, &cached->descriptor)", DISPLAY_SOURCE)

    def test_pages_auto_advance_after_their_configured_dwell(self) -> None:
        self.assertRegex(
            DISPLAY_SOURCE,
            r"AUTO_ADVANCE_INTERVAL_MS\s*=\s*5000",
        )
        dwell = function_body(DISPLAY_SOURCE, "uint32_t currentPageDwellMs(")
        self.assertIn("AUTO_ADVANCE_INTERVAL_MS", dwell)
        self.assertIn("ANIMATED_ADVANCE_INTERVAL_MS", dwell)

        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        self.assertIn(
            "now - lastPageChangeAt >= currentPageDwellMs()", display_loop
        )
        self.assertIn("queuedDirection = 1", display_loop)
        self.assertIn("changePage(queuedDirection, now)", display_loop)
        self.assertIn("changePage(queuedDirection, millis())", display_loop)
        self.assertIn("pendingSwipe", display_loop)
        self.assertIn("!contentWasRendered", display_loop)

        finish_slide = function_body(
            DISPLAY_SOURCE, "void finishSlideTransition("
        )
        update_transition = function_body(
            DISPLAY_SOURCE, "void updatePageTransition("
        )
        self.assertIn("lastPageChangeAt = now", finish_slide)
        self.assertIn("lastPageChangeAt = now", update_transition)

    def test_qr_page_is_only_default_or_opened_from_device_settings(self) -> None:
        self.assertRegex(
            DISPLAY_SOURCE,
            r"SYSTEM_PAGE_IDLE_TIMEOUT_MS\s*=\s*10000",
        )
        read_touch = function_body(DISPLAY_SOURCE, "void readTouch(")
        self.assertIn("dy > SWIPE_THRESHOLD", read_touch)
        self.assertIn(
            "pendingScreenRequest = ScreenRequest::DeviceSettings", read_touch
        )
        self.assertNotIn("touchStartY <= TOP_EDGE_SWIPE_ZONE", read_touch)

        settings_action = function_body(
            DISPLAY_SOURCE, "void handleSettingsAction("
        )
        self.assertIn("action == SettingsAction::Back", settings_action)
        self.assertIn(
            "pendingScreenRequest = ScreenRequest::SystemPage", settings_action
        )

        show_content = function_body(DISPLAY_SOURCE, "void displayShowContent(")
        self.assertIn("appConfig.pageCount() == 0", show_content)
        self.assertIn("currentPage = 1", show_content)

        commit_page = function_body(DISPLAY_SOURCE, "void commitPageChange(")
        self.assertIn("const size_t pageCount = appConfig.pageCount()", commit_page)
        self.assertIn("currentPage = 1", commit_page)
        self.assertNotIn("% total", commit_page)

        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        request_start = display_loop.index(
            "if (pendingScreenRequest != ScreenRequest::None)"
        )
        request_end = display_loop.index("if (deviceSettingsDirty", request_start)
        request_branch = display_loop[request_start:request_end]
        self.assertIn("renderDeviceSettings()", request_branch)
        self.assertIn("showSystemPage()", request_branch)
        self.assertNotIn("renderSystemPage()", request_branch)

        show_system = function_body(DISPLAY_SOURCE, "void showSystemPage(")
        self.assertIn(
            "resumeContentPage = min(currentPage, pageCount)", show_system
        )
        self.assertIn("currentPage = 0", show_system)

        timeout_start = display_loop.index(
            "appConfig.pageCount() > 0 && currentPage == 0"
        )
        timeout_end = display_loop.index("contentWasRendered = true", timeout_start)
        system_timeout = display_loop[timeout_start:timeout_end]
        self.assertIn(
            "now - lastTouchAt >= SYSTEM_PAGE_IDLE_TIMEOUT_MS", system_timeout
        )
        self.assertIn("resumePlayback()", system_timeout)

    def test_time_overlay_and_screen_off_settings_are_synchronized(self) -> None:
        for key in (
            "timezoneOffsetMinutes",
            "showDateTime",
            "screenOffEnabled",
            "screenOffStartMinutes",
            "screenOffEndMinutes",
        ):
            self.assertIn(key, APP_CONFIG_HEADER)
            self.assertIn(f'doc["{key}"]', APP_CONFIG_SOURCE)
            self.assertIn(f'doc["{key}"]', WEB_SOURCE)

        self.assertIn('server.on("/api/settings", HTTP_POST', WEB_SOURCE)
        self.assertIn("settimeofday", WEB_SOURCE)
        self.assertIn('configTime(0, 0, "pool.ntp.org"', MAIN_SOURCE)
        self.assertIn("addPlaybackClock(screen", DISPLAY_SOURCE)
        self.assertIn("screenOffWindowActive()", DISPLAY_SOURCE)
        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        self.assertIn("!scheduledScreenOff", display_loop)
        self.assertIn("refreshScheduledBacklight()", display_loop)

        self.assertIn("void renderDeviceSettings()", DISPLAY_SOURCE)
        self.assertIn("SettingsAction::Open", DISPLAY_SOURCE)
        for control in (
            'id="timezoneOffset"',
            'id="deviceDateTime"',
            'id="showDateTime"',
            'id="screenOffEnabled"',
            'id="screenOffStart"',
            'id="screenOffEnd"',
        ):
            self.assertIn(control, WEB_UI_SOURCE)
        self.assertIn('api("/api/settings"', WEB_UI_SOURCE)

    def test_device_settings_match_actionable_web_settings(self) -> None:
        for web_control in (
            'id="brightness"',
            'id="languageSwitch"',
            'id="deviceDateTime"',
            'id="timezoneOffset"',
            'id="showDateTime"',
            'id="showWeather"',
            'id="imageNarrationEnabled"',
            'id="pageTransitionSwitch"',
            'id="screenOffEnabled"',
            'id="screenOffStart"',
            'id="screenOffEnd"',
            'id="resetWifi"',
        ):
            self.assertIn(web_control, WEB_UI_SOURCE)

        self.assertIn("enum class SettingsSection", DISPLAY_SOURCE)
        for section in ("Display", "Time", "System"):
            self.assertIn(f"SettingsSection::{section}", DISPLAY_SOURCE)

        render_settings = function_body(DISPLAY_SOURCE, "void renderDeviceSettings() {")
        for renderer in (
            "renderDisplaySettings(screen)",
            "renderTimeSettings(screen)",
            "renderSystemSettings(screen)",
        ):
            self.assertIn(renderer, render_settings)

        display_settings = function_body(DISPLAY_SOURCE, "void renderDisplaySettings(")
        self.assertIn("lv_slider_set_range(brightnessSlider, 5, 100)", display_settings)
        self.assertIn("appConfig.brightness()", display_settings)
        self.assertIn("SettingsAction::ToggleClock", display_settings)
        self.assertIn("SettingsAction::CyclePageTransition", display_settings)
        self.assertIn("SettingsAction::ToggleWeather", display_settings)
        self.assertIn("SettingsAction::ToggleImageNarration", display_settings)
        for action in (
            "SettingsAction::ToggleScreenOff",
            "SettingsAction::EditScreenOffStart",
            "SettingsAction::EditScreenOffEnd",
        ):
            self.assertIn(action, display_settings)
        self.assertNotIn("SettingsAction::SetChinese", display_settings)
        self.assertNotIn("SettingsAction::SetEnglish", display_settings)

        system_settings = function_body(DISPLAY_SOURCE, "void renderSystemSettings(")
        self.assertIn("SettingsAction::SetChinese", system_settings)
        self.assertIn("SettingsAction::SetEnglish", system_settings)

        brightness_change = function_body(
            DISPLAY_SOURCE, "void handleBrightnessValueChanged("
        )
        self.assertIn("appConfig.setBrightness", brightness_change)
        self.assertIn("backlightApply()", brightness_change)
        self.assertIn("LV_EVENT_RELEASED", brightness_change)
        self.assertIn("LV_EVENT_PRESS_LOST", brightness_change)
        self.assertIn("persistDeviceSettings(false)", brightness_change)
        self.assertNotIn("displaySetBrightness", brightness_change)
        self.assertIn("handleBrightnessValueChanged", display_settings)
        self.assertIn("LV_EVENT_ALL", display_settings)
        brightness_route = WEB_SOURCE[
            WEB_SOURCE.index('server.on("/api/brightness"') :
            WEB_SOURCE.index('server.on("/api/language"')
        ]
        self.assertIn("displayMarkSettingsDirty()", brightness_route)
        settings_route = WEB_SOURCE[
            WEB_SOURCE.index('server.on("/api/settings"') :
            WEB_SOURCE.index('server.on("/api/sd"')
        ]
        self.assertIn("displayMarkContentDirty()", settings_route)
        marker = function_body(DISPLAY_SOURCE, "void displayMarkSettingsDirty()")
        self.assertIn("deviceSettingsScreen", marker)
        content_dirty_branch = function_body(DISPLAY_SOURCE, "void displayLoop()")
        self.assertRegex(
            content_dirty_branch,
            r"contentDirty[\s\S]{0,1000}deviceSettingsScreen[\s\S]{0,200}renderDeviceSettings",
        )

        time_settings = function_body(DISPLAY_SOURCE, "void renderTimeSettings(")
        for action in (
            "SettingsAction::SyncTime",
            "SettingsAction::EditDateTime",
            "SettingsAction::TimezonePrevious",
            "SettingsAction::TimezoneNext",
        ):
            self.assertIn(action, time_settings)
        for action in (
            "SettingsAction::ToggleScreenOff",
            "SettingsAction::EditScreenOffStart",
            "SettingsAction::EditScreenOffEnd",
        ):
            self.assertNotIn(action, time_settings)
        self.assertRegex(DISPLAY_SOURCE, r"TIMEZONE_OPTIONS[\s\S]{0,700}\b345\b")
        self.assertIn("SettingsEditor::DateTime", DISPLAY_SOURCE)
        self.assertIn("SettingsEditor::ScreenOffStart", DISPLAY_SOURCE)
        self.assertIn("SettingsEditor::ScreenOffEnd", DISPLAY_SOURCE)
        self.assertIn("settimeofday", DISPLAY_SOURCE)
        self.assertIn("lv_roller_create", DISPLAY_SOURCE)
        self.assertIn("struct DateTimeDraft", DISPLAY_SOURCE)
        self.assertIn("struct TimeOfDayDraft", DISPLAY_SOURCE)
        date_editor = function_body(DISPLAY_SOURCE, "void renderDateTimeEditor(")
        self.assertIn("dateTimeDraft", date_editor)
        self.assertGreaterEqual(date_editor.count("handleDateRollerChanged"), 5)
        time_editor = function_body(DISPLAY_SOURCE, "void renderTimeEditor(")
        self.assertIn("timeOfDayDraft", time_editor)
        self.assertEqual(time_editor.count("handleTimeRollerChanged"), 2)
        settings_handler = function_body(DISPLAY_SOURCE, "void handleSettingsAction(")
        self.assertIn("beginDateTimeDraft()", settings_handler)
        self.assertIn("beginTimeOfDayDraft", settings_handler)
        date_change = function_body(
            DISPLAY_SOURCE, "void handleDateRollerChanged("
        )
        self.assertIn("updateDateTimeDraftFromRollers()", date_change)
        time_change = function_body(
            DISPLAY_SOURCE, "void handleTimeRollerChanged("
        )
        self.assertIn("updateTimeOfDayDraftFromRollers()", time_change)
        self.assertIn("LOCAL_TIME_MAX_EPOCH_EXCLUSIVE", DISPLAY_SOURCE)
        self.assertIn("localWallTime", DISPLAY_SOURCE)
        self.assertIn("LOCAL_TIME_MAX_EPOCH_EXCLUSIVE", settings_route)
        self.assertIn("localWallTime", settings_route)

        self.assertIn(
            "lv_obj_set_ext_click_area(brightnessSlider, 10)", display_settings
        )

        system_settings = function_body(DISPLAY_SOURCE, "void renderSystemSettings(")
        self.assertIn("WiFi.SSID()", system_settings)
        self.assertIn("SettingsAction::OpenWifiReset", system_settings)

        read_touch = function_body(DISPLAY_SOURCE, "void readTouch(")
        self.assertIn(
            "!deviceSettingsScreen", read_touch,
            "settings sliders and rollers must not be mistaken for page gestures",
        )
        self.assertRegex(DISPLAY_SOURCE, r"SETTINGS_IDLE_TIMEOUT_MS\s*=\s*60000")
        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        self.assertIn("now - lastTouchAt >= SETTINGS_IDLE_TIMEOUT_MS", display_loop)
        self.assertIn("SETTINGS_SAVE_MAX_RETRIES", display_loop)
        self.assertIn("settingsSavePending = true", display_loop)

    def test_device_settings_layout_keeps_controls_separated(self) -> None:
        time_settings = function_body(DISPLAY_SOURCE, "void renderTimeSettings(")
        clock = re.search(
            r"liveClockLabel = addLabel\(currentPanel, \"\", &ui_font_misans_16,\s*"
            r"0xE7FF54, (\d+), LV_TEXT_ALIGN_LEFT\);",
            time_settings,
        )
        clock_pos = re.search(
            r"placeSettingsText\(liveClockLabel, (kSettingsRowInset|\d+),",
            time_settings,
        )
        manual = re.search(
            r"addSettingsButton\(currentPanel, uiText\(\"手动\", \"Set\"\), "
            r"(\d+), (?:\d+|buttonY), (\d+), (\d+),\s*"
            r"SettingsAction::EditDateTime\);",
            time_settings,
        )
        self.assertIsNotNone(clock)
        self.assertIsNotNone(clock_pos)
        self.assertIsNotNone(manual)
        clock_width = int(clock.group(1))
        clock_x = (
            16
            if clock_pos.group(1) == "kSettingsRowInset"
            else int(clock_pos.group(1))
        )
        manual_x = int(manual.group(1))
        self.assertGreaterEqual(
            manual_x - (clock_x + clock_width),
            12,
            "the live clock label and manual-time button must not overlap",
        )

        paired_controls = (
            (
                function_body(DISPLAY_SOURCE, "void renderSystemSettings("),
                r"addSettingsButton\(languagePanel, \"中文\", (\d+), (?:\d+|buttonY), (\d+),",
                r"addSettingsButton\(languagePanel, \"EN\", (\d+), (?:\d+|buttonY), (\d+),",
                "language buttons",
            ),
            (
                time_settings,
                r"addSettingsButton\(currentPanel, uiText\(\"手动\", \"Set\"\), "
                r"(\d+), (?:\d+|buttonY), (\d+),",
                r"addSettingsButton\(currentPanel, uiText\(\"校时\", \"Sync\"\), "
                r"(\d+), (?:\d+|buttonY), (\d+),",
                "time action buttons",
            ),
            (
                time_settings,
                r"addSettingsButton\(timezonePanel, \"-\", (\d+), (?:\d+|buttonY), (\d+),",
                r"addSettingsButton\(timezonePanel, \"\+\", (\d+), (?:\d+|buttonY), (\d+),",
                "timezone buttons",
            ),
        )
        for body, left_pattern, right_pattern, description in paired_controls:
            left = re.search(left_pattern, body)
            right = re.search(right_pattern, body)
            self.assertIsNotNone(left)
            self.assertIsNotNone(right)
            left_x, left_width = map(int, left.groups())
            right_x, _ = map(int, right.groups())
            self.assertGreaterEqual(
                right_x - (left_x + left_width),
                12,
                f"{description} need enough space to remain visually separate",
            )

        button_helper = function_body(DISPLAY_SOURCE, "lv_obj_t* addSettingsButton(")
        self.assertIn("lv_obj_set_style_pad_all(button, 0, 0)", button_helper)
        self.assertIn(
            "lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED)",
            button_helper,
        )
        self.assertIn(
            "lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED)",
            button_helper,
        )

        panel_helper = function_body(DISPLAY_SOURCE, "lv_obj_t* addSettingsPanel(")
        self.assertIn("lv_obj_set_style_pad_all(panel, 0, 0)", panel_helper)

    def test_screen_off_times_are_conditional_display_subitems(self) -> None:
        display_settings = function_body(DISPLAY_SOURCE, "void renderDisplaySettings(")
        time_settings = function_body(DISPLAY_SOURCE, "void renderTimeSettings(")
        self.assertIn(
            "const lv_font_t* settingsRowFont = &ui_font_misans_16;",
            display_settings,
        )
        for label in ("自动息屏", "息屏时间", "恢复时间"):
            self.assertRegex(
                display_settings,
                rf'uiText\("{label}",[^\n]+\),\s*settingsRowFont,\s*'
                r"0xF4EFE6",
            )
            self.assertNotIn(f'uiText("{label}"', time_settings)
        self.assertIn("if (appConfig.screenOffEnabled())", display_settings)
        self.assertIn("addSettingsList(screen)", display_settings)
        self.assertIn("addSettingsPanel(list,", display_settings)
        self.assertIn("startPanel = addSettingsPanel(list, y, 48)", display_settings)
        self.assertIn("endPanel = addSettingsPanel(list, y, 48)", display_settings)
        self.assertNotIn("styleSettingsSubpanel", DISPLAY_SOURCE)
        for getter, action in (
            ("screenOffStartMinutes", "EditScreenOffStart"),
            ("screenOffEndMinutes", "EditScreenOffEnd"),
        ):
            self.assertRegex(
                display_settings,
                rf"formatMinuteOfDay\(appConfig\.{getter}\(\)\), 318,\s*"
                rf"6, 100, 36, SettingsAction::{action}",
            )
        self.assertIn('id="screenOffTimes"', WEB_UI_SOURCE)
        self.assertIn('$("#screenOffStart").disabled=!enabled', WEB_UI_SOURCE)
        self.assertIn('$("#screenOffEnd").disabled=!enabled', WEB_UI_SOURCE)
        self.assertIn('classList.toggle("is-disabled",!enabled)', WEB_UI_SOURCE)
        settings_handler = function_body(DISPLAY_SOURCE, "void handleSettingsAction(")
        self.assertGreaterEqual(
            settings_handler.count("if (!appConfig.screenOffEnabled())"),
            3,
            "disabled screen-off time controls must not open or save",
        )

    def test_device_wifi_reset_is_confirmed_and_deferred(self) -> None:
        self.assertIn(
            "bool WebPortal::clearWifiAndRequestRestart()", WEB_SOURCE
        )
        self.assertIn("bool clearWifiAndRequestRestart();", WEB_HEADER)
        self.assertGreaterEqual(
            WEB_SOURCE.count("clearWifiAndRequestRestart()"),
            2,
            "the web route and shared implementation must use the same reset path",
        )

        self.assertIn("bool displayTakeWifiResetRequest();", DISPLAY_HEADER)
        self.assertIn("void displayReportWifiResetFailure();", DISPLAY_HEADER)
        self.assertIn("displayTakeWifiResetRequest()", MAIN_SOURCE)
        self.assertIn("webPortal.clearWifiAndRequestRestart()", MAIN_SOURCE)
        self.assertIn(
            "displayReportWifiResetFailure()", MAIN_SOURCE,
            "a failed NVS clear must restore the confirmation controls",
        )

        settings_handler = function_body(DISPLAY_SOURCE, "void handleSettingsAction(")
        self.assertIn("SettingsAction::OpenWifiReset", settings_handler)
        self.assertIn("SettingsAction::ConfirmWifiReset", settings_handler)
        self.assertIn("wifiResetRequested = true", settings_handler)
        self.assertNotIn("Preferences", settings_handler)
        self.assertNotIn("ESP.restart", settings_handler)

        reset_confirm = function_body(DISPLAY_SOURCE, "void renderWifiResetConfirmation(")
        self.assertIn("SettingsAction::CancelEditor", reset_confirm)
        self.assertIn("SettingsAction::ConfirmWifiReset", reset_confirm)
        self.assertIn("wifiResetInProgress", reset_confirm)

        take_request = function_body(
            DISPLAY_SOURCE, "bool displayTakeWifiResetRequest()"
        )
        self.assertNotIn("wifiResetInProgress = false", take_request)
        report_failure = function_body(
            DISPLAY_SOURCE, "void displayReportWifiResetFailure()"
        )
        self.assertIn("wifiResetInProgress = false", report_failure)
        self.assertIn("deviceSettingsDirty = true", report_failure)

    def test_interface_language_is_persistent_and_shared_by_both_uis(self) -> None:
        self.assertIn("enum class InterfaceLanguage", APP_CONFIG_HEADER)
        self.assertIn("InterfaceLanguage language() const", APP_CONFIG_HEADER)
        self.assertIn("const char* languageCode() const", APP_CONFIG_HEADER)
        self.assertIn('doc["language"]', APP_CONFIG_SOURCE)
        self.assertIn('doc["language"] = appConfig.languageCode()', WEB_SOURCE)
        self.assertIn('server.on("/api/language", HTTP_POST', WEB_SOURCE)
        self.assertIn("displayMarkSettingsDirty()", WEB_SOURCE)

        self.assertIn("SettingsAction::SetChinese", DISPLAY_SOURCE)
        self.assertIn("SettingsAction::SetEnglish", DISPLAY_SOURCE)
        self.assertIn('addSettingsButton(languagePanel, "中文"', DISPLAY_SOURCE)
        self.assertIn('uiText("设备设置", "DEVICE SETTINGS")', DISPLAY_SOURCE)

        self.assertIn('id="languageSwitch"', WEB_UI_SOURCE)
        self.assertIn('data-language="zh"', WEB_UI_SOURCE)
        self.assertIn('data-language="en"', WEB_UI_SOURCE)
        self.assertIn("const translations=", WEB_UI_SOURCE)
        self.assertIn("applyLanguage(device.language)", WEB_UI_SOURCE)
        self.assertIn('api("/api/language"', WEB_UI_SOURCE)

    def test_device_chinese_ui_font_contains_every_localized_glyph(self) -> None:
        localized_strings = re.findall(
            r'uiText\(\s*"([^"]*)"', DISPLAY_SOURCE + MAIN_SOURCE
        )
        localized_strings.append("中文")
        characters = {
            character
            for text in localized_strings
            for character in text
            if ord(character) > 0x7F
        }
        self.assertTrue(characters)
        for character in characters:
            self.assertIn(
                f'U+{ord(character):04X} "{character}"',
                UI_FONT_SOURCE,
                f"localized device glyph {character} is missing from ui_font_misans_16",
            )

    def test_device_uses_misans_with_emoji_supplement(self) -> None:
        self.assertNotIn("lv_font_montserrat", DISPLAY_SOURCE)
        self.assertNotIn("lv_font_simsun", DISPLAY_SOURCE)
        self.assertTrue(EMOJI_FONT_PATH.exists())
        self.assertIn("containsEmoji(page.text)", DISPLAY_SOURCE)
        self.assertIn("font = &ui_font_emoji_32", DISPLAY_SOURCE)
        self.assertIn("font != &ui_font_emoji_32", DISPLAY_SOURCE)
        self.assertIn("codepoint == 0xFE0F || codepoint == 0x200D", DISPLAY_SOURCE)
        self.assertRegex(EMOJI_FONT_SOURCE,
                         r"\.fallback\s*=\s*&ui_font_misans_16")
        self.assertNotIn("lv_font_simsun", EMOJI_FONT_SOURCE)
        self.assertIn("MiSans / Xiaomi", DISPLAY_SOURCE)
        encoded = {int(value, 16) for value in re.findall(
            r'U\+([0-9A-Fa-f]{4,6})\s+"', UI_FONT_SOURCE)}
        self.assertTrue(set(range(0x20, 0x7F)).issubset(encoded))
        for unsupported in "あア한αЯ😀":
            self.assertNotIn(ord(unsupported), encoded)

    def test_scheduled_screen_off_supports_double_tap_temporary_wake(self) -> None:
        self.assertRegex(
            DISPLAY_SOURCE,
            r"TEMPORARY_WAKE_IDLE_MS\s*=\s*30000",
        )
        read_touch = function_body(DISPLAY_SOURCE, "void readTouch(")
        self.assertIn("touchBeganWhileBlanked", read_touch)
        self.assertIn("registerBlankedTap", read_touch)
        self.assertIn("LV_INDEV_STATE_RELEASED", read_touch)

        register_tap = function_body(DISPLAY_SOURCE, "void registerBlankedTap(")
        self.assertIn("wakeOverrideUntil", register_tap)
        self.assertIn("TEMPORARY_WAKE_IDLE_MS", register_tap)
        self.assertIn("refreshScheduledBacklight()", register_tap)

        note_activity = function_body(DISPLAY_SOURCE, "void noteWakeActivity(")
        self.assertIn(
            "wakeOverrideUntil = now + TEMPORARY_WAKE_IDLE_MS", note_activity
        )
        self.assertIn("noteWakeActivity", read_touch)

        refresh_backlight = function_body(
            DISPLAY_SOURCE, "void refreshScheduledBacklight()"
        )
        self.assertIn("wakeOverrideActive", refresh_backlight)
        self.assertIn("screenOffWindowActive() && !wakeOverrideActive", refresh_backlight)

    def test_playback_clock_stacks_larger_shadowed_text_without_background(self) -> None:
        playback_clock = function_body(DISPLAY_SOURCE, "void addPlaybackClock(")
        self.assertIn(
            "lv_obj_set_size(card, presentation.width, presentation.height)",
            playback_clock,
        )
        self.assertIn(
            "presentation.width = 184", OVERLAY_PRESENTATION_HEADER
        )
        self.assertIn(
            "presentation.height = 104", OVERLAY_PRESENTATION_HEADER
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0)", playback_clock
        )
        self.assertIn(
            "lv_obj_set_style_bg_color(card, lv_color_hex(0x101619), 0)",
            playback_clock,
        )
        self.assertIn("playbackDateShadowLabel", playback_clock)
        self.assertIn("playbackTimeShadowLabel", playback_clock)
        self.assertIn("&ui_font_misans_24", playback_clock)
        self.assertIn("&ui_font_misans_48", playback_clock)
        self.assertGreaterEqual(playback_clock.count("0x000000"), 2)
        self.assertGreaterEqual(playback_clock.count("LV_OPA_50"), 2)
        for size in (
            "lv_obj_set_height(playbackDateShadowLabel, 27)",
            "lv_obj_set_height(playbackDateLabel, 27)",
            "lv_obj_set_height(playbackTimeShadowLabel, 52)",
            "lv_obj_set_height(playbackTimeLabel, 52)",
        ):
            self.assertIn(size, playback_clock)
        for alignment in (
            "lv_obj_align(playbackDateShadowLabel, LV_ALIGN_TOP_MID, 2, 10)",
            "lv_obj_align(playbackDateLabel, LV_ALIGN_TOP_MID, 0, 8)",
            "lv_obj_align(playbackTimeShadowLabel, LV_ALIGN_TOP_MID, 2, 45)",
            "lv_obj_align(playbackTimeLabel, LV_ALIGN_TOP_MID, 0, 43)",
        ):
            self.assertIn(alignment, playback_clock)
        self.assertLess(
            playback_clock.index("playbackDateShadowLabel ="),
            playback_clock.index("playbackDateLabel ="),
        )
        self.assertLess(
            playback_clock.index("playbackTimeShadowLabel ="),
            playback_clock.index("playbackTimeLabel ="),
        )

        update_clock = function_body(DISPLAY_SOURCE, "void updateLiveClock()")
        self.assertIn('strftime(date, sizeof(date), "%Y-%m-%d"', update_clock)
        self.assertIn('strftime(clock, sizeof(clock), "%H:%M"', update_clock)
        self.assertIn("&ui_font_misans_24", update_clock)
        self.assertIn("&ui_font_misans_48", update_clock)
        self.assertIn("playbackDateShadowLabel", update_clock)
        self.assertIn("playbackTimeShadowLabel", update_clock)
        self.assertNotIn("CONFIG_LV_FONT_MONTSERRAT_48=y", SDKCONFIG_DEFAULTS)
        self.assertNotIn("CONFIG_LV_FONT_MONTSERRAT_48=y", SDKCONFIG_TARGET)
        self.assertIn(
            "width:184px;height:104px;padding:8px 0 0;background:transparent",
            WEB_UI_SOURCE,
        )
        self.assertIn('font:700 24px/27px', WEB_UI_SOURCE)
        self.assertIn('font:800 48px/52px', WEB_UI_SOURCE)
        self.assertIn('gap:8px', WEB_UI_SOURCE)
        self.assertIn(
            'text-shadow:2px 2px 0 rgba(0,0,0,.5)', WEB_UI_SOURCE
        )
        self.assertIn('id="clockPreviewDate"', WEB_UI_SOURCE)
        self.assertIn('id="clockPreviewTime"', WEB_UI_SOURCE)

    def test_playback_clock_shadow_and_foreground_share_one_cache_gate(self) -> None:
        apply_clock = function_body(DISPLAY_SOURCE, "void applyClockLabel(")
        cache_guard = apply_clock.index("cache == text")
        first_text_write = apply_clock.index("lv_label_set_text(")
        self.assertLess(cache_guard, first_text_write)
        self.assertIn("if (shadowLabel)", apply_clock)
        self.assertIn(
            "lv_obj_set_style_text_font(shadowLabel, selectedFont, 0)",
            apply_clock,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(label, selectedFont, 0)", apply_clock
        )
        self.assertEqual(apply_clock.count("lv_label_set_text("), 2)
        self.assertNotIn("lv_obj_invalidate", apply_clock)
        self.assertNotIn("lv_refr_now", apply_clock)
        self.assertNotIn("espDisplayStackRefreshNow", apply_clock)
        self.assertGreater(apply_clock.index("cache = text"), first_text_write)

        update_clock = function_body(DISPLAY_SOURCE, "void updateLiveClock()")
        self.assertNotIn(
            "applyClockLabel(playbackDateShadowLabel", update_clock
        )
        self.assertNotIn(
            "applyClockLabel(playbackTimeShadowLabel", update_clock
        )
        self.assertEqual(update_clock.count("playbackDateShadowLabel"), 3)
        self.assertEqual(update_clock.count("playbackTimeShadowLabel"), 3)

    def test_playback_clock_preserves_corner_and_timer_across_rebuilds(self) -> None:
        self.assertRegex(
            DISPLAY_SOURCE,
            r"PLAYBACK_CLOCK_MOVE_INTERVAL_MS\s*=\s*60000",
        )
        self.assertRegex(
            DISPLAY_SOURCE,
            r"PlaybackClockCorner\s+playbackClockCorner\s*=\s*"
            r"PlaybackClockCorner::BottomRight",
        )

        create_screen = function_body(DISPLAY_SOURCE, "lv_obj_t* createScreen(")
        for pointer in (
            "playbackClockCard = nullptr",
            "playbackDateShadowLabel = nullptr",
            "playbackDateLabel = nullptr",
            "playbackTimeShadowLabel = nullptr",
            "playbackTimeLabel = nullptr",
        ):
            self.assertIn(pointer, create_screen)
        self.assertIn('playbackDateText = ""', create_screen)
        self.assertIn('playbackTimeText = ""', create_screen)
        self.assertNotIn("playbackClockCorner", create_screen)
        self.assertNotIn("playbackClockLastMoveAt", create_screen)
        self.assertNotIn("playbackClockMoveTimerStarted", create_screen)

        playback_clock = function_body(DISPLAY_SOURCE, "void addPlaybackClock(")
        self.assertIn("playbackClockCard = card", playback_clock)
        self.assertIn("if (!playbackClockMoveTimerStarted)", playback_clock)
        self.assertIn("esp_random() % PLAYBACK_CLOCK_CORNER_COUNT", playback_clock)
        self.assertIn("playbackClockLastMoveAt = millis()", playback_clock)
        self.assertIn("alignPlaybackClockCard(card)", playback_clock)
        self.assertNotIn("lv_obj_fade_in", playback_clock)

    def test_playback_clock_randomly_selects_another_top_corner(self) -> None:
        self.assertRegex(
            DISPLAY_SOURCE,
            r"PLAYBACK_CLOCK_CORNER_COUNT\s*=\s*4",
        )
        move_clock = function_body(
            DISPLAY_SOURCE, "void updatePlaybackClockPosition("
        )
        self.assertRegex(
            move_clock,
            r"esp_random\(\)\s*%\s*\(PLAYBACK_CLOCK_CORNER_COUNT\s*-\s*1\)",
        )
        self.assertIn(
            "(current + offset) % PLAYBACK_CLOCK_CORNER_COUNT",
            move_clock,
        )
        self.assertLess(
            move_clock.index("playbackClockCorner ="),
            move_clock.index("alignPlaybackClockCard(playbackClockCard)"),
        )
        self.assertLess(
            move_clock.index("alignPlaybackClockCard(playbackClockCard)"),
            move_clock.index("playbackClockLastMoveAt = now"),
        )

    def test_playback_clock_uses_safe_corners_and_timed_fade(self) -> None:
        align_clock = function_body(
            DISPLAY_SOURCE, "void alignPlaybackClockCard("
        )
        self.assertRegex(
            DISPLAY_SOURCE,
            r"PLAYBACK_CLOCK_INSET\s*=\s*16",
        )
        self.assertNotIn("16, 48", align_clock)
        for alignment in (
            "lv_obj_align(card, LV_ALIGN_TOP_LEFT, PLAYBACK_CLOCK_INSET,\n"
            "                   PLAYBACK_CLOCK_INSET)",
            "lv_obj_align(card, LV_ALIGN_TOP_RIGHT, -PLAYBACK_CLOCK_INSET,\n"
            "                   PLAYBACK_CLOCK_INSET)",
            "lv_obj_align(card, LV_ALIGN_BOTTOM_LEFT, PLAYBACK_CLOCK_INSET,\n"
            "                   -PLAYBACK_CLOCK_INSET)",
            "lv_obj_align(card, LV_ALIGN_BOTTOM_RIGHT, -PLAYBACK_CLOCK_INSET,\n"
            "                   -PLAYBACK_CLOCK_INSET)",
        ):
            self.assertIn(alignment, align_clock)

        self.assertRegex(
            DISPLAY_SOURCE,
            r"PLAYBACK_CLOCK_FADE_IN_MS\s*=\s*120",
        )
        self.assertEqual(DISPLAY_SOURCE.count("lv_obj_fade_in("), 1)
        move_clock = function_body(
            DISPLAY_SOURCE, "void updatePlaybackClockPosition("
        )
        self.assertIn(
            "lv_obj_fade_in(playbackClockCard, PLAYBACK_CLOCK_FADE_IN_MS, 0)",
            move_clock,
        )

    def test_playback_clock_defers_until_visible_idle_playback(self) -> None:
        move_clock = function_body(
            DISPLAY_SOURCE, "void updatePlaybackClockPosition("
        )
        for guard in (
            "!playbackClockCard",
            "provisioningScreen",
            "deviceSettingsScreen",
            "currentPage == 0",
            "pageTransitionPhase != PageTransitionPhase::Idle",
            "scheduledScreenOff",
            "storageBlankActive",
            "backlightResumeAt != 0",
        ):
            self.assertIn(guard, move_clock)

        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        move = display_loop.index("updatePlaybackClockPosition(millis())")
        self.assertGreater(move, display_loop.index("updatePageTransition(now)"))
        self.assertGreater(move, display_loop.index("refreshScheduledBacklight()"))
        self.assertGreater(
            move, display_loop.index("preloadOneNeighbourImage(currentPage)")
        )
        self.assertGreater(move, display_loop.index("backlightResumeAt = 0"))
        self.assertIn("updatePlaybackClockPosition(millis())", display_loop)
        self.assertRegex(
            display_loop,
            r"if \(!contentWasRendered && !warmBeforeChange\) \{\s*"
            r"updatePlaybackClockPosition\(millis\(\)\);",
        )
        self.assertIn(
            "初始随机位于四角之一，此后每分钟在左上、右上、左下、右下间移动",
            WEB_UI_SOURCE,
        )
        self.assertIn(
            "starts randomly in one corner, then moves among the top-left, "
            "top-right, bottom-left, and bottom-right each minute",
            WEB_UI_SOURCE,
        )

    def test_page_redraw_swaps_complete_frames_without_backlight_blank(self) -> None:
        render_current = function_body(DISPLAY_SOURCE, "void renderCurrent(")
        self.assertNotIn("beginProtectedFrameUpdate()", render_current)
        self.assertNotIn("digitalWrite(BACKLIGHT_PIN, LOW)", render_current)

        display_begin = function_body(DISPLAY_SOURCE, "bool displayBegin()")
        self.assertIn("espDisplayStackBegin()", display_begin)
        self.assertIn("espDisplayStackStart()", display_begin)
        self.assertNotIn("lv_init()", display_begin)

        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        self.assertNotIn("lv_tick_inc", display_loop)
        self.assertNotIn("lv_timer_handler", display_loop)
        self.assertIn("LvglLockGuard", display_loop)

        storage_write = function_body(
            DISPLAY_SOURCE, "void displayBeginStorageWrite("
        )
        self.assertIn("if (blankBacklight)", storage_write)
        self.assertIn("storageBlankActive = true", storage_write)
        self.assertIn("backlightApply()", storage_write)
        self.assertIn("espDisplayStackPause()", storage_write)

        backlight = function_body(DISPLAY_SOURCE, "void backlightApply()")
        self.assertIn(
            "storageBlankActive || backlightResumeAt != 0", backlight
        )
        self.assertIn("forcedOff ? 0", backlight)
        self.assertIn("ledcWrite(BACKLIGHT_PIN, duty)", backlight)
        storage_end = function_body(
            DISPLAY_SOURCE, "void displayEndStorageWrite()"
        )
        self.assertIn("espDisplayStackResume()", storage_end)

    def test_psram_and_lvgl_configuration_survive_clean_idf_builds(self) -> None:
        for setting in (
            "CONFIG_SPIRAM=y",
            "CONFIG_SPIRAM_MODE_OCT=y",
            "CONFIG_SPIRAM_SPEED_80M=y",
            "CONFIG_SPIRAM_XIP_FROM_PSRAM=y",
            "CONFIG_LV_MEM_CUSTOM=y",
            "CONFIG_LV_USE_PNG=y",
            "CONFIG_LV_USE_SJPG=y",
            "CONFIG_LV_USE_QRCODE=y",
        ):
            self.assertIn(setting, SDKCONFIG_DEFAULTS)

    def test_flash_writes_pause_the_lvgl_worker(self) -> None:
        commit = function_body(WEB_SOURCE, "bool commitUploadBuffer()")
        self.assertLess(
            commit.index("displayBeginStorageWrite()"),
            commit.index("mediaStoreCommitUpload"),
        )
        for route in (
            'server.on("/api/pages/text"',
            'server.on("/api/pages/delete"',
            'server.on("/api/pages/move"',
            'server.on("/api/brightness"',
            'server.on("/api/language"',
            'server.on("/api/settings"',
            'server.on("/api/wifi"',
            'server.on("/api/wifi/reset"',
        ):
            route_at = WEB_SOURCE.index(route)
            next_route = WEB_SOURCE.find("server.on(", route_at + len(route))
            route_body = WEB_SOURCE[
                route_at : next_route if next_route >= 0 else len(WEB_SOURCE)
            ]
            self.assertIn("displayBeginStorageWrite(", route_body, route)
            self.assertIn("displayEndStorageWrite()", route_body, route)

    def test_manual_frame_restart_workaround_cannot_return(self) -> None:
        for legacy_path in (
            "dmaRestartRequested",
            "pendingFrameCompletions",
            "onRgbFrameBufferComplete",
            "esp_lcd_rgb_panel_restart",
        ):
            self.assertNotIn(legacy_path, DISPLAY_SOURCE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
