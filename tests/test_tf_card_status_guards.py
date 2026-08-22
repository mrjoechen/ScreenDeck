#!/usr/bin/env python3
"""Regression guards for TF-card detection and status visibility."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
DISPLAY_STACK_SOURCE = (ROOT / "src" / "esp_display_stack.cpp").read_text()
DISPLAY_SOURCE = (ROOT / "src" / "display_ui.cpp").read_text()
DISPLAY_HEADER = (ROOT / "include" / "display_ui.h").read_text()
MAIN_SOURCE = (ROOT / "src" / "main.cpp").read_text()
MEDIA_SOURCE = (ROOT / "src" / "media_store.cpp").read_text()
MEDIA_HEADER = (ROOT / "include" / "media_store.h").read_text()
WEB_SOURCE = (ROOT / "src" / "web_portal.cpp").read_text()
WEB_UI_SOURCE = (ROOT / "include" / "web_ui.h").read_text()
UI_FONT_SOURCE = (ROOT / "src" / "ui_font_16_zh.c").read_text()


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


def route_body(source: str, route: str) -> str:
    route_at = source.index(route)
    next_route = source.find("server.on(", route_at + len(route))
    return source[route_at : next_route if next_route >= 0 else len(source)]


class TfCardStatusGuards(unittest.TestCase):
    def test_panel_releases_the_spi_pins_before_tf_mount(self) -> None:
        self.assertRegex(
            DISPLAY_STACK_SOURCE,
            r"vendorConfig\.flags\.enable_io_multiplex\s*=\s*true",
            "the ST7701 command IO must release shared GPIO 48/47 after init",
        )
        self.assertRegex(
            DISPLAY_STACK_SOURCE,
            r"ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG\(\s*rgbPanel,\s*nullptr",
            "the LVGL adapter must not retain the command IO after it is released",
        )
        setup = function_body(MAIN_SOURCE, "void setup()")
        self.assertLess(setup.index("displayBegin()"), setup.index("mediaStoreMountSd()"))

    def test_status_endpoints_retry_a_card_inserted_after_boot(self) -> None:
        self.assertIn("bool mediaStoreEnsureSdMounted();", MEDIA_HEADER)
        ensure_mounted = function_body(
            MEDIA_SOURCE, "bool mediaStoreEnsureSdMounted()"
        )
        self.assertIn("SD_MOUNT_RETRY_INTERVAL_MS", ensure_mounted)
        self.assertIn("mediaStoreMountSd()", ensure_mounted)

        for route in ('server.on("/api/status"', 'server.on("/api/sd"'):
            self.assertIn(
                "mediaStoreEnsureSdMounted()",
                route_body(WEB_SOURCE, route),
                f"{route} must detect a card inserted after boot",
            )

    def test_hot_removed_card_is_released_before_automatic_remount(self) -> None:
        for fragment in (
            "void mediaStoreReportSdIoError(int errorCode);",
            "bool mediaStoreTakeSdRemovalDetected();",
        ):
            self.assertIn(fragment, MEDIA_HEADER)

        report_error = function_body(
            MEDIA_SOURCE, "void mediaStoreReportSdIoError(int errorCode)"
        )
        self.assertIn("EIO", report_error)
        self.assertIn("ENODEV", report_error)
        self.assertIn("sdRemovalDetected", report_error)

        take_removal = function_body(
            MEDIA_SOURCE, "bool mediaStoreTakeSdRemovalDetected()"
        )
        self.assertIn("exchange(false)", take_removal)

        mount_sd = function_body(MEDIA_SOURCE, "bool mediaStoreMountSd()")
        self.assertLess(
            mount_sd.index("probeSdCard("),
            mount_sd.index("for (const uint32_t clock"),
            "a missing card should be rejected by one low-speed probe before "
            "the multi-clock filesystem mount attempts",
        )
        self.assertGreaterEqual(
            mount_sd.rfind("sdLastMountAttemptAt = millis()"),
            mount_sd.index("Serial.println(\"[sd] no TF card detected\")"),
            "the retry interval must begin after a failed probe completes",
        )

        fs_read = function_body(DISPLAY_SOURCE, "lv_fs_res_t fsRead(")
        self.assertIn("mediaStoreReportSdIoError", fs_read)
        self.assertIn("errno", fs_read)
        self.assertIn("handle->failed = true", fs_read)

        main_loop = function_body(MAIN_SOURCE, "void loop()")
        removed_at = main_loop.index("mediaStoreTakeSdRemovalDetected()")
        release_at = main_loop.index("displayReleaseMedia()", removed_at)
        unmount_at = main_loop.index("mediaStoreUnmountSd()", release_at)
        remount_at = main_loop.index("mediaStoreEnsureSdMounted()", unmount_at)
        self.assertLess(removed_at, release_at)
        self.assertLess(release_at, unmount_at)
        self.assertLess(unmount_at, remount_at)
        self.assertIn("displayMarkContentDirty()", main_loop[removed_at:remount_at])

    def test_web_uploads_use_transactional_sd_first_managed_storage(self) -> None:
        self.assertIn(
            "String mediaStoreCommitUpload(const String& filename,",
            MEDIA_HEADER,
        )
        for path in (
            '"/sd/ScreenDeck/media/"',
            '"/sd/ScreenDeck/animations/"',
            '"/sd/ScreenDeck/temp/"',
        ):
            self.assertIn(path, MEDIA_SOURCE)

        commit = function_body(
            MEDIA_SOURCE,
            "String mediaStoreCommitUpload(const String& filename,",
        )
        self.assertIn("mediaStoreEnsureSdMounted()", commit)
        self.assertIn("commitUploadToPath", commit)
        self.assertIn("littleFsUploadPath", commit)

        transactional_write = function_body(
            MEDIA_SOURCE, "bool commitUploadToPath("
        )
        self.assertIn(".part", transactional_write)
        self.assertIn("rename", transactional_write)
        self.assertLess(
            transactional_write.index("file.write"),
            transactional_write.index("rename"),
        )

        web_commit = function_body(WEB_SOURCE, "bool commitUploadBuffer()")
        self.assertIn("mediaStoreCommitUpload", web_commit)
        self.assertNotIn("LittleFS.open", web_commit)
        self.assertNotIn("LittleFS.remove", web_commit)

        upload_route = route_body(WEB_SOURCE, '"/api/upload"')
        self.assertIn("mediaExists(uploadPath)", upload_route)
        self.assertIn("mediaRemove(uploadPath)", upload_route)
        self.assertNotIn("LittleFS.exists(uploadPath)", upload_route)
        self.assertNotIn("LittleFS.remove(uploadPath)", upload_route)

        remove_media = function_body(MEDIA_SOURCE, "bool mediaRemove(")
        self.assertIn("managedSdPath", remove_media)
        self.assertIn("filesystem->remove", remove_media)

    def test_unmountable_card_reports_detected_filesystem(self) -> None:
        for fragment in (
            "enum class MediaStoreSdStatus",
            "UnsupportedFilesystem",
            "UnreadableFilesystem",
            "MediaStoreSdStatus mediaStoreSdStatus();",
            "const char* mediaStoreSdStatusCode();",
            "const char* mediaStoreSdFilesystemName();",
            "const char* mediaStoreSdSupportedFilesystems();",
        ):
            self.assertIn(fragment, MEDIA_HEADER)

        for fragment in (
            '#include "sd_diskio.h"',
            '#include "diskio.h"',
            "sdcard_init(",
            "disk_initialize(",
            "sd_read_raw(",
            "sdFilesystemFromBootSector(",
            "sdGptFirstPartitionLba(",
            "MediaStoreSdStatus::UnsupportedFilesystem",
            "MediaStoreSdStatus::UnreadableFilesystem",
        ):
            self.assertIn(fragment, MEDIA_SOURCE)

    def test_web_and_device_settings_show_tf_card_status(self) -> None:
        status_route = route_body(WEB_SOURCE, 'server.on("/api/status"')
        for field in (
            "sdMounted",
            "sdStatus",
            "sdFilesystem",
            "sdSupportedFilesystems",
            "sdTotal",
            "sdUsed",
            "sdClockHz",
        ):
            self.assertIn(f'doc["{field}"]', status_route)

        sd_route = route_body(WEB_SOURCE, 'server.on("/api/sd"')
        for symbol in (
            "mediaStoreSdStatusCode()",
            "mediaStoreSdFilesystemName()",
            "mediaStoreSdSupportedFilesystems()",
        ):
            self.assertIn(symbol, sd_route)
        rescan_route = route_body(WEB_SOURCE, 'server.on("/api/sd/rescan"')
        self.assertIn("MediaStoreSdStatus::UnsupportedFilesystem", rescan_route)
        self.assertIn("MediaStoreSdStatus::UnreadableFilesystem", rescan_route)
        self.assertIn("415", rescan_route)

        self.assertIn('id="sdStorage"', WEB_UI_SOURCE)
        self.assertIn("device.sdMounted", WEB_UI_SOURCE)
        self.assertIn("device.sdUsed", WEB_UI_SOURCE)
        self.assertIn("device.sdTotal", WEB_UI_SOURCE)
        self.assertIn("device.sdClockHz", WEB_UI_SOURCE)
        for fragment in (
            "sdUnsupported",
            "sdUnreadable",
            "sdSupportedFormats",
            "function sdIssueText(",
            'status==="unsupported"',
            'status==="unreadable"',
        ):
            self.assertIn(fragment, WEB_UI_SOURCE)
        rescan_at = WEB_UI_SOURCE.index('$("#sdRescan").onclick')
        rescan_handler = WEB_UI_SOURCE[
            rescan_at : WEB_UI_SOURCE.index(
                '$$("#languageSwitch button")', rescan_at
            )
        ]
        self.assertIn("await loadStatus()", rescan_handler)

        system_settings = function_body(
            DISPLAY_SOURCE, "void renderSystemSettings("
        )
        self.assertNotIn("mediaStoreEnsureSdMounted()", system_settings)
        for symbol in (
            "mediaStoreSdMounted()",
            "mediaStoreSdStatus()",
            "mediaStoreSdTotalBytes()",
            "mediaStoreSdUsedBytes()",
            "formatStorageSize(",
            "MediaStoreSdStatus::UnsupportedFilesystem",
            "MediaStoreSdStatus::UnreadableFilesystem",
            'uiText("TF 卡", "TF card")',
            'uiText("容量 ", "Capacity ")',
            'uiText("已用 ", "Used ")',
            "SettingsAction::RescanSd",
        ):
            self.assertIn(symbol, system_settings)
        for technical_detail in (
            "mediaStoreSdFilesystemName()",
            "mediaStoreSdSupportedFilesystems()",
            "mediaStoreSdClockHz()",
            'uiText("在线", "Mounted")',
        ):
            self.assertNotIn(technical_detail, system_settings)
        self.assertNotIn("sdUsedLabel", system_settings)
        self.assertIn('" · "', system_settings)

        formatter = function_body(DISPLAY_SOURCE, "String formatStorageSize(")
        for unit in ("GB", "MB", "KB", "B"):
            self.assertIn(f'" {unit}"', formatter)
        self.assertIn(", 1)", formatter)

        settings_handler = function_body(
            DISPLAY_SOURCE, "void handleSettingsAction("
        )
        self.assertIn("SettingsAction::RescanSd", settings_handler)
        self.assertIn("sdRescanRequested = true", settings_handler)
        self.assertNotIn("mediaStoreUnmountSd()", settings_handler)
        self.assertNotIn("mediaStoreMountSd()", settings_handler)
        read_touch = function_body(DISPLAY_SOURCE, "void readTouch(")
        self.assertIn("sdRescanRequested = true", read_touch)

    def test_web_does_not_scan_card_files_until_the_user_requests_it(self) -> None:
        bootstrap_at = WEB_UI_SOURCE.rindex("loadStatus()")
        bootstrap = WEB_UI_SOURCE[bootstrap_at:]
        self.assertNotIn(
            "loadSdMedia()",
            bootstrap,
            "opening the controller must not recursively scan TF-card files",
        )

        load_pages_at = WEB_UI_SOURCE.index("async function loadPages()")
        load_pages = WEB_UI_SOURCE[
            load_pages_at : WEB_UI_SOURCE.index("let sdFiles=null", load_pages_at)
        ]
        self.assertIn("const sdBacked=isSdPath(p.path)", load_pages)
        self.assertIn(
            'sdBacked?`<div class="thumb sd-fallback">TF</div>`',
            load_pages,
            "the normal playlist must not download full SD media for thumbnails",
        )

        rescan_at = WEB_UI_SOURCE.index('$("#sdRescan").onclick')
        rescan_handler = WEB_UI_SOURCE[rescan_at:bootstrap_at]
        self.assertIn(
            "loadSdMedia()",
            rescan_handler,
            "the explicit TF-card action must retain manual file browsing",
        )

    def test_playback_skips_images_from_an_unavailable_tf_card(self) -> None:
        render_current = function_body(DISPLAY_SOURCE, "void renderCurrent()")
        page_change = function_body(
            DISPLAY_SOURCE, "void commitPageChange("
        )

        self.assertIn("findPlayablePage(currentPage, 1, true)", render_current)
        self.assertIn(
            "findPlayablePage(currentPage, direction, false)", page_change
        )
        self.assertNotIn("renderMissingMedia", DISPLAY_SOURCE)
        self.assertNotIn("TF 卡上找不到该文件", DISPLAY_SOURCE)
        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        self.assertIn("!playableContentUnavailable", display_loop)
        self.assertIn("playableContentUnavailable = false", display_loop)

        for signature in (
            "void renderTextPage(",
            "void renderImagePage(",
        ):
            render_page = function_body(DISPLAY_SOURCE, signature)
            self.assertNotIn("addPageIndicator", render_page)
            self.assertNotIn("playablePageIndicator", render_page)
        self.assertNotIn("void addPageIndicator(", DISPLAY_SOURCE)

    def test_device_tf_capacity_separator_has_a_font_glyph(self) -> None:
        system_settings = function_body(
            DISPLAY_SOURCE, "void renderSystemSettings("
        )
        self.assertIn('" · "', system_settings)
        self.assertIn(
            'U+00B7 "·"',
            UI_FONT_SOURCE,
            "the TF capacity separator must not render as a missing-glyph box",
        )

        self.assertIn("bool displayTakeSdRescanRequest();", DISPLAY_HEADER)
        take_request = function_body(
            DISPLAY_SOURCE, "bool displayTakeSdRescanRequest()"
        )
        self.assertIn("!deviceSettingsScreen", take_request)
        self.assertIn("clearMediaCache()", take_request)
        main_loop = function_body(MAIN_SOURCE, "void loop()")
        self.assertIn("displayTakeSdRescanRequest()", main_loop)
        self.assertIn("mediaStoreUnmountSd()", main_loop)
        self.assertIn("mediaStoreMountSd()", main_loop)
        self.assertIn("displayMarkContentDirty()", main_loop)


if __name__ == "__main__":
    unittest.main(verbosity=2)
