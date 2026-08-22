#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
CONFIG_HEADER = (ROOT / "include" / "app_config.h").read_text()
CONFIG_SOURCE = (ROOT / "src" / "app_config.cpp").read_text()
DISPLAY_SOURCE = (ROOT / "src" / "display_ui.cpp").read_text()
WEATHER_SOURCE = (ROOT / "src" / "weather.cpp").read_text()
WEATHER_ICONS = (ROOT / "src" / "weather_icons.c").read_text()
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


class WeatherGuards(unittest.TestCase):
    def test_weather_setting_is_persistent_and_shared_by_both_uis(self) -> None:
        self.assertIn("bool showWeather() const", CONFIG_HEADER)
        self.assertIn("void setShowWeather(bool enabled)", CONFIG_HEADER)
        self.assertIn('doc["showWeather"] | false', CONFIG_SOURCE)
        self.assertIn('doc["showWeather"] = showWeather_', CONFIG_SOURCE)
        self.assertIn('doc["showWeather"] = appConfig.showWeather()', WEB_SOURCE)
        self.assertIn('server.arg("showWeather") == "1"', WEB_SOURCE)
        self.assertIn('id="showWeather"', WEB_UI_SOURCE)
        self.assertIn('showWeather:$("#showWeather").checked', WEB_UI_SOURCE)
        self.assertIn("SettingsAction::ToggleWeather", DISPLAY_SOURCE)

    def test_weather_fetch_is_async_rate_limited_and_source_backed(self) -> None:
        self.assertIn("https://ipwho.is/", WEATHER_SOURCE)
        self.assertIn("https://api.open-meteo.com/v1/forecast", WEATHER_SOURCE)
        for field in ("temperature_2m", "weather_code", "is_day"):
            self.assertIn(field, WEATHER_SOURCE)
        for field in ("city", "region", "country"):
            self.assertIn(field, WEATHER_SOURCE)
        self.assertIn("&lang=zh-CN", WEATHER_SOURCE)
        self.assertIn("30U * 60U * 1000U", WEATHER_SOURCE)
        self.assertIn("15U * 1000U", WEATHER_SOURCE)
        self.assertIn("retryReady && stale", WEATHER_SOURCE)
        self.assertIn("xTaskCreatePinnedToCore", WEATHER_SOURCE)
        self.assertIn("esp_crt_bundle_attach", WEATHER_SOURCE)
        self.assertIn("weatherLoop(appConfig.showWeather())", MAIN_SOURCE)
        self.assertIn("char location[64]", (ROOT / "include" / "weather.h").read_text())
        self.assertIn("formatLocationLine", WEATHER_SOURCE)
        self.assertIn("languageStale", WEATHER_SOURCE)
        self.assertIn("locationUsesChinese", WEATHER_SOURCE)
        self.assertIn('"%.2f %c, %.2f %c"', WEATHER_SOURCE)
        format_location = function_body(WEATHER_SOURCE, "void formatLocationLine(")
        self.assertIn("%s%s%s", format_location)
        self.assertIn('chinese ? "，" : ", "', format_location)

    def test_weather_uses_all_reference_icon_categories(self) -> None:
        mapping = function_body(DISPLAY_SOURCE, "const lv_img_dsc_t* weatherIconFor(")
        for icon in (
            "weather_clear_day",
            "weather_clear_night",
            "weather_cloudy",
            "weather_drizzle",
            "weather_flurries",
            "weather_haze_fog",
            "weather_heavy_rain",
            "weather_rain_showers",
            "weather_scattered_rain_showers_day",
            "weather_scattered_rain_showers_night",
            "weather_scattered_snow_showers_day",
            "weather_scattered_snow_showers_night",
            "weather_strong_thunderstorms",
            "weather_thunderstorms_day",
            "weather_thunderstorms_night",
            "weather_not_available",
        ):
            self.assertIn(icon, mapping)
        self.assertEqual(WEATHER_ICONS.count("const lv_img_dsc_t weather_"), 23)
        self.assertEqual(WEATHER_ICONS.count("LV_IMG_CF_RAW_ALPHA"), 23)

    def test_weather_overlay_shares_the_playback_card_on_image_pages(self) -> None:
        overlay = function_body(DISPLAY_SOURCE, "void addPlaybackClock(")
        self.assertIn("onImage && appConfig.showWeather() && weatherGetSnapshot(snapshot)", overlay)
        self.assertIn("showClock && !showWeather", overlay)
        self.assertIn("lv_obj_set_size(card, 184, 104)", overlay)
        self.assertIn("LV_ALIGN_TOP_MID", overlay)
        self.assertIn("LV_FLEX_FLOW_ROW", overlay)
        self.assertIn("LV_ALIGN_CENTER", overlay)
        self.assertIn("addPlaybackWeatherCluster", overlay)
        self.assertIn("alignPlaybackClockCard(card)", overlay)
        weather = function_body(DISPLAY_SOURCE, "void addPlaybackWeatherCluster(")
        self.assertIn("&lv_font_montserrat_16", weather)
        self.assertIn(r'\xC2\xB0', weather)
        self.assertNotIn("addPlaybackLocation", weather)
        self.assertNotIn("void addPlaybackLocation(", DISPLAY_SOURCE)
        display_settings = function_body(DISPLAY_SOURCE, "void renderDisplaySettings(")
        self.assertIn("weatherSnapshot.location", display_settings)
        self.assertIn('uiText("定位中", "Locating")', display_settings)
        self.assertIn("16 + weatherTitleWidth + 8, 17", display_settings)
        self.assertIn("lv_obj_set_pos(weatherLabel, 16, 17)", display_settings)
        self.assertIn("addSettingsList(screen)", display_settings)
        list_helper = function_body(DISPLAY_SOURCE, "lv_obj_t* addSettingsList(")
        self.assertIn("LV_DIR_VER", list_helper)
        self.assertIn("LV_OBJ_FLAG_SCROLLABLE", list_helper)
        image_page = function_body(DISPLAY_SOURCE, "void renderImagePage(")
        text_page = function_body(DISPLAY_SOURCE, "void renderTextPage(")
        self.assertIn("addPlaybackClock(screen, true)", image_page)
        self.assertIn("addPlaybackClock(screen, false", text_page)
        self.assertNotIn("addImageWeather", DISPLAY_SOURCE)
        self.assertNotIn("addPageIndicator", image_page)
        self.assertNotIn("addPageIndicator", text_page)

    def test_weather_temperature_keeps_montserrat_degree_glyph(self) -> None:
        add_label = function_body(
            DISPLAY_SOURCE, "lv_obj_t* addLabel(lv_obj_t* parent, const String& text,"
        )
        self.assertIn("containsCjk(text)", add_label)
        self.assertNotIn("containsUtf8(text)", add_label)
        self.assertNotIn('U+00B0 "°"', (ROOT / "src" / "ui_font_16_zh.c").read_text())


if __name__ == "__main__":
    unittest.main()
