#!/usr/bin/env python3
"""Regression guards for persisted, asynchronous image narration."""

from pathlib import Path
import hashlib
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
APP_CONFIG_HEADER = (ROOT / "include" / "app_config.h").read_text()
APP_CONFIG_SOURCE = (ROOT / "src" / "app_config.cpp").read_text()
DISPLAY_SOURCE = (ROOT / "src" / "display_ui.cpp").read_text()
LLM_HEADER = (ROOT / "include" / "llm_narration.h").read_text()
LLM_SOURCE = (ROOT / "src" / "llm_narration.cpp").read_text()
MAIN_SOURCE = (ROOT / "src" / "main.cpp").read_text()
WEB_SOURCE = (ROOT / "src" / "web_portal.cpp").read_text()
WEB_UI_SOURCE = (ROOT / "include" / "web_ui.h").read_text()
IDF_MANIFEST = (ROOT / "src" / "idf_component.yml").read_text()
CMAKE_SOURCE = (ROOT / "src" / "CMakeLists.txt").read_text()
UI_FONT_SOURCE = (ROOT / "src" / "ui_font_misans_16.c").read_text()
UI_FONT_HEADER = (ROOT / "include" / "ui_font.h").read_text()
UI_FONT_SUPPORT_PATH = ROOT / "src" / "ui_font_support.cpp"
UI_FONT_SUPPORT_SOURCE = (
    UI_FONT_SUPPORT_PATH.read_text() if UI_FONT_SUPPORT_PATH.exists() else ""
)
NARRATION_ICON_HEADER_PATH = ROOT / "include" / "narration_icon.h"
NARRATION_ICON_SOURCE_PATH = ROOT / "src" / "narration_icon.c"
NARRATION_ICON_HEADER = (
    NARRATION_ICON_HEADER_PATH.read_text()
    if NARRATION_ICON_HEADER_PATH.exists()
    else ""
)
NARRATION_ICON_SOURCE = (
    NARRATION_ICON_SOURCE_PATH.read_text()
    if NARRATION_ICON_SOURCE_PATH.exists()
    else ""
)


def function_body(source: str, signature: str) -> str:
    """Return a C/C++ function body without counting braces in literals."""
    signature_at = source.index(signature)
    body_at = source.index("{", signature_at)
    depth = 0
    state = "code"
    escaped = False
    index = body_at
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and following == "/":
                state = "code"
                index += 1
        elif state in ("string", "character"):
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif (state == "string" and char == '"') or (
                state == "character" and char == "'"
            ):
                state = "code"
        elif char == "/" and following == "/":
            state = "line-comment"
            index += 1
        elif char == "/" and following == "*":
            state = "block-comment"
            index += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "character"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[body_at + 1 : index]
        index += 1
    raise AssertionError(f"unterminated function: {signature}")


def route_body(source: str, route: str) -> str:
    route_at = source.index(route)
    next_route = source.find("server.on(", route_at + len(route))
    return source[route_at : next_route if next_route >= 0 else len(source)]


def compact(source: str) -> str:
    return re.sub(r"\s+", "", source)


class LlmNarrationGuards(unittest.TestCase):
    def test_llm_triple_and_image_narration_are_persisted_without_key_echo(self) -> None:
        for public_api in (
            "const String& llmBaseUrl() const",
            "const String& llmApiKey() const",
            "const String& llmModel() const",
            "bool llmConfigured() const",
            "bool setLlmConfig(",
            "void clearLlmConfig()",
            "bool setImageNarration(",
        ):
            self.assertIn(public_api, APP_CONFIG_HEADER)
        self.assertIn("String narration", APP_CONFIG_HEADER)

        load = function_body(APP_CONFIG_SOURCE, "bool AppConfig::load()")
        save = function_body(APP_CONFIG_SOURCE, "bool AppConfig::save() const")
        pages_json = function_body(APP_CONFIG_SOURCE, "String AppConfig::toJson() const")
        for field in ("llmBaseUrl", "llmApiKey", "llmModel"):
            self.assertIn(f'doc["{field}"]', load)
            self.assertIn(f'doc["{field}"]', save)
        self.assertIn('item["narration"]', load)
        self.assertIn('item["narration"]', save)
        self.assertIn('item["narration"]', pages_json)

        status = route_body(WEB_SOURCE, 'server.on("/api/status"')
        for public_field in (
            "llmBaseUrl",
            "llmModel",
            "llmApiKeyConfigured",
        ):
            self.assertIn(f'doc["{public_field}"]', status)
        self.assertNotIn('doc["llmApiKey"]', status)

    def test_web_form_posts_the_triple_and_supports_an_explicit_clear(self) -> None:
        for element_id in (
            "llmSettingsForm",
            "llmBaseUrl",
            "llmApiKey",
            "llmModel",
            "clearLlmSettings",
        ):
            self.assertIn(f'id="{element_id}"', WEB_UI_SOURCE)
        self.assertIn('api("/api/llm"', WEB_UI_SOURCE)
        for posted_name in ("baseUrl", "apiKey", "model"):
            self.assertIn(f'name="{posted_name}"', WEB_UI_SOURCE)
            self.assertRegex(WEB_UI_SOURCE, rf"{posted_name}\s*:\s*\$\(")
        self.assertRegex(WEB_UI_SOURCE, r'clear\s*:\s*"1"')

        route = route_body(WEB_SOURCE, 'server.on("/api/llm"')
        for posted_name in ("baseUrl", "apiKey", "model", "clear"):
            self.assertIn(f'server.arg("{posted_name}")', route)
        self.assertIn("appConfig.clearLlmConfig()", route)
        self.assertIn("appConfig.setLlmConfig(", route)
        self.assertIn("appConfig.save()", route)
        self.assertIn("previousApiKey = appConfig.llmApiKey()", route)
        self.assertIn('server.arg("csrfToken")', route)
        self.assertIn("llmSettingsToken", route)
        self.assertIn("baseUrl != previousBaseUrl", route)
        self.assertIn("model != previousModel", route)
        self.assertIn('server.arg("allowInsecureHttp")', route)
        self.assertIn("llmSettingsToken", WEB_UI_SOURCE)
        self.assertIn("llmAllowInsecureHttp", WEB_UI_SOURCE)
        self.assertRegex(
            compact(route),
            r"apiKey\.isEmpty\(\).*apiKey=previousApiKey",
            "an empty password field must preserve the already stored API key",
        )

    def test_custom_prompt_is_persisted_while_output_contract_stays_in_firmware(self) -> None:
        # Break caught: accepting a custom prompt as raw request structure, or
        # failing to carry it through persisted config and worker snapshots.
        self.assertIn("MAX_LLM_NARRATION_PROMPT_BYTES", APP_CONFIG_HEADER)
        self.assertIn("DEFAULT_LLM_NARRATION_PROMPT", APP_CONFIG_HEADER)
        self.assertIn("const String& llmNarrationPrompt() const", APP_CONFIG_HEADER)
        self.assertIn("bool setLlmNarrationPrompt(", APP_CONFIG_HEADER)

        load = function_body(APP_CONFIG_SOURCE, "bool AppConfig::load()")
        save = function_body(APP_CONFIG_SOURCE, "bool AppConfig::save() const")
        clear = function_body(APP_CONFIG_SOURCE, "void AppConfig::clearLlmConfig()")
        set_prompt = function_body(
            APP_CONFIG_SOURCE, "bool AppConfig::setLlmNarrationPrompt("
        )
        self.assertIn('doc["llmNarrationPrompt"]', load)
        self.assertIn("DEFAULT_LLM_NARRATION_PROMPT", load)
        self.assertIn('doc["llmNarrationPrompt"]', save)
        self.assertIn("DEFAULT_LLM_NARRATION_PROMPT", clear)
        self.assertNotIn("pages_", set_prompt)
        self.assertNotIn(".narration", set_prompt)

        status = route_body(WEB_SOURCE, 'server.on("/api/status"')
        save_route = route_body(WEB_SOURCE, 'server.on("/api/llm", HTTP_POST')
        test_route = route_body(
            WEB_SOURCE, 'server.on("/api/llm/test", HTTP_POST'
        )
        self.assertIn('doc["llmNarrationPrompt"]', status)
        for route in (save_route, test_route):
            self.assertIn('server.arg("narrationPrompt")', route)
            self.assertNotIn('server.arg("messages")', route)
            self.assertNotIn('server.arg("responseFormat")', route)
            self.assertNotIn('server.arg("maxTokens")', route)
        self.assertIn("previousNarrationPrompt", save_route)
        self.assertIn("setLlmNarrationPrompt", save_route)

        self.assertRegex(
            WEB_UI_SOURCE,
            r'<textarea[^>]*id="llmNarrationPrompt"[^>]*data-max-codepoints="2048"',
        )
        hydrate = function_body(WEB_UI_SOURCE, "function hydrateLlmSettings(")
        self.assertIn("device.llmNarrationPrompt", hydrate)
        self.assertRegex(
            WEB_UI_SOURCE,
            r'\$\("#llmNarrationPrompt"\)\.addEventListener\("input",markLlmSettingsChanged\)',
        )
        for handler in (
            function_body(WEB_UI_SOURCE, '$("#llmSettingsForm").onsubmit'),
            function_body(WEB_UI_SOURCE, '$("#testLlmSettings").onclick'),
        ):
            self.assertIn("narrationPrompt", handler)
            self.assertIn('$("#llmNarrationPrompt").value.trim()', handler)

        for signature in (
            "bool llmNarrationTestStart(",
            "bool llmNarrationRequest(",
            "bool llmNarrationRequestRgb565Alpha(",
        ):
            signature_at = LLM_SOURCE.index(signature)
            declaration = LLM_SOURCE[
                signature_at : LLM_SOURCE.index("{", signature_at)
            ]
            self.assertIn("const String& narrationPrompt", declaration)
        job = re.search(r"struct NarrationJob\s*\{(?P<body>[\s\S]*?)\n\};", LLM_SOURCE)
        self.assertIsNotNone(job)
        self.assertIn("narrationPrompt", job.group("body"))
        post = function_body(LLM_SOURCE, "bool postVisionRequest(")
        self.assertIn("NARRATION_OUTPUT_CONSTRAINTS", post)
        self.assertIn("job.narrationPrompt", post)
        self.assertIn("jsonQuoted", post)
        self.assertIn(r'\"max_tokens\":96', post)
        self.assertIn(r'\"type\":\"image_url\"', compact(post))
        self.assertNotIn("responseFormat", post)

        display = function_body(DISPLAY_SOURCE, "bool requestImageNarration(")
        self.assertIn("appConfig.llmNarrationPrompt()", display)

    def test_custom_prompt_is_strict_utf8_and_limited_to_2048_codepoints(self) -> None:
        # The textarea limit is only a convenience. Direct API callers must hit
        # the same firmware-owned UTF-8/codepoint validation as persisted data.
        self.assertIn(
            "MAX_LLM_NARRATION_PROMPT_CODEPOINTS = 2048", APP_CONFIG_HEADER
        )
        self.assertIn(
            "bool validLlmNarrationPrompt(const String& value)",
            APP_CONFIG_HEADER,
        )

        validator = function_body(
            APP_CONFIG_SOURCE, "bool validLlmNarrationPrompt("
        )
        self.assertIn("decodeUtf8CodePoint", validator)
        self.assertIn("MAX_LLM_NARRATION_PROMPT_CODEPOINTS", validator)
        self.assertIn("codepoints", validator)

        load = function_body(APP_CONFIG_SOURCE, "bool AppConfig::load()")
        setter = function_body(
            APP_CONFIG_SOURCE, "bool AppConfig::setLlmNarrationPrompt("
        )
        save_route = route_body(WEB_SOURCE, 'server.on("/api/llm", HTTP_POST')
        test_route = route_body(
            WEB_SOURCE, 'server.on("/api/llm/test", HTTP_POST'
        )
        for call_site in (load, setter, save_route, test_route):
            self.assertIn("validLlmNarrationPrompt", call_site)

        # Reject overlong, surrogate, out-of-range and overlong-encoded UTF-8;
        # accepting every 0xC0..0xF7 lead byte is not Unicode validation.
        decoder = function_body(APP_CONFIG_SOURCE, "bool decodeUtf8CodePoint(")
        for boundary in ("0xC2", "0xE0", "0xED", "0xF0", "0xF4"):
            self.assertIn(boundary, decoder)
        self.assertNotIn("narrationPrompt.length() >= MAX_LLM", save_route)
        self.assertNotIn("narrationPrompt.length() >= MAX_LLM", test_route)

    def test_narration_overlay_is_transparent_and_uses_requested_left_icon(self) -> None:
        # Break caught: substituting or corrupting the supplied PNG, restoring
        # the old translucent card, or placing the icon after the narration.
        match = re.search(
            r"static const uint8_t narration_auto_awesome_icon_data\[\]\s*=\s*\{(?P<data>[\s\S]*?)\};",
            NARRATION_ICON_SOURCE,
        )
        self.assertIsNotNone(match, "embed the supplied auto-awesome PNG")
        payload = bytes(
            int(value, 16)
            for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group("data"))
        )
        self.assertEqual(
            hashlib.sha256(payload).hexdigest(),
            "4893072846104cae9179c333bcba3bfbb0262d453c3df3a73f878dd608f45622",
        )
        self.assertEqual(payload[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(int.from_bytes(payload[16:20], "big"), 24)
        self.assertEqual(int.from_bytes(payload[20:24], "big"), 24)
        self.assertEqual(payload[25], 6, "the embedded PNG must remain RGBA")
        self.assertIn("LV_IMG_CF_RAW_ALPHA", NARRATION_ICON_SOURCE)
        self.assertRegex(NARRATION_ICON_SOURCE, r"\.w\s*=\s*24")
        self.assertRegex(NARRATION_ICON_SOURCE, r"\.h\s*=\s*24")
        self.assertIn(
            "extern const lv_img_dsc_t narration_auto_awesome_icon",
            NARRATION_ICON_HEADER,
        )

        overlay = function_body(DISPLAY_SOURCE, "void addImageNarration(")
        self.assertIn("lv_img_create", overlay)
        icon_at = overlay.index("lv_img_create")
        text_at = overlay.index("addLabel(")
        self.assertLess(icon_at, text_at)
        self.assertIn("&narration_auto_awesome_icon", overlay)
        self.assertIn("LV_ALIGN_LEFT_MID", overlay)
        self.assertIn("LV_OPA_TRANSP", overlay)
        self.assertNotIn("LV_OPA_70", overlay)
        self.assertNotIn("LV_OPA_20", overlay)

    def test_persisted_enable_switch_gates_display_and_fetch_without_losing_cache(self) -> None:
        for public_api in (
            "bool imageNarrationEnabled() const",
            "void setImageNarrationEnabled(bool enabled)",
            "bool persistedImageNarrationEnabled() const",
            "void restorePersistedImageNarrationEnabled()",
        ):
            self.assertIn(public_api, APP_CONFIG_HEADER)
        self.assertIn("bool imageNarrationEnabled_ = true", APP_CONFIG_HEADER)
        self.assertIn(
            "mutable bool persistedImageNarrationEnabled_ = true",
            APP_CONFIG_HEADER,
        )

        load = function_body(APP_CONFIG_SOURCE, "bool AppConfig::load()")
        save = function_body(APP_CONFIG_SOURCE, "bool AppConfig::save() const")
        clear = function_body(APP_CONFIG_SOURCE, "void AppConfig::clearLlmConfig()")
        self.assertIn('doc["imageNarrationEnabled"]', load)
        self.assertIn('doc["imageNarrationEnabled"]', save)
        self.assertIn(
            "persistedImageNarrationEnabled_ = imageNarrationEnabled_", save
        )
        self.assertNotIn("imageNarrationEnabled", clear)

        status = route_body(WEB_SOURCE, 'server.on("/api/status"')
        self.assertIn('doc["imageNarrationEnabled"]', status)
        toggle_route = route_body(
            WEB_SOURCE, 'server.on("/api/llm/narration"'
        )
        self.assertIn('server.arg("csrfToken")', toggle_route)
        self.assertIn('server.arg("enabled")', toggle_route)
        self.assertIn("appConfig.setImageNarrationEnabled", toggle_route)
        self.assertIn("appConfig.save()", toggle_route)
        self.assertIn("displayMarkContentDirty()", toggle_route)

        for fragment in (
            'id="imageNarrationEnabled"',
            'role="switch"',
            'aria-labelledby="imageNarrationEnabledLabel"',
            'aria-describedby="imageNarrationEnabledHint"',
            'api("/api/llm/narration"',
            "device.imageNarrationEnabled!==false",
        ):
            self.assertIn(fragment, WEB_UI_SOURCE)
        self.assertRegex(
            WEB_UI_SOURCE,
            r'enabled\s*:\s*enabled\?"1":"0"',
        )
        self.assertIn(".toggle input:focus-visible+i", WEB_UI_SOURCE)

        schedule = function_body(DISPLAY_SOURCE, "void addImageNarration(")
        schedule_compact = compact(schedule)
        self.assertIn(
            "const bool enabled = appConfig.imageNarrationEnabled()", schedule
        )
        self.assertIn(
            "imageNarrationVisible=enabled&&!page.narration.isEmpty()",
            schedule_compact,
        )
        self.assertRegex(schedule_compact, r"if\(!enabled\)\{return;\}")
        gate_at = schedule_compact.index("if(!enabled){return;}")
        self.assertGreater(schedule_compact.index("requestImageNarration("), gate_at)
        request = function_body(DISPLAY_SOURCE, "bool requestImageNarration(")
        request_gate = request.index("!appConfig.imageNarrationEnabled()")
        self.assertLess(request_gate, request.index("llmNarrationRequest("))
        self.assertLess(request_gate, request.index("llmNarrationRequestRgb565Alpha("))
        self.assertGreater(schedule_compact.index("addLabel("), gate_at)

        settings_action = function_body(
            DISPLAY_SOURCE, "void handleSettingsAction("
        )
        display_settings = function_body(
            DISPLAY_SOURCE, "void renderDisplaySettings("
        )
        self.assertIn("SettingsAction::ToggleImageNarration", settings_action)
        self.assertIn("appConfig.setImageNarrationEnabled", settings_action)
        self.assertIn("SettingsAction::ToggleImageNarration", display_settings)
        display_loop = function_body(DISPLAY_SOURCE, "void displayLoop()")
        self.assertIn(
            "appConfig.restorePersistedImageNarrationEnabled()", display_loop
        )
        self.assertRegex(
            compact(WEB_UI_SOURCE),
            r"cachedNarration=device\.imageNarrationEnabled!==false\?String\(p\.narration",
            "the web playlist must hide cached summaries while the switch is off",
        )
        toggle_handler_at = WEB_UI_SOURCE.index(
            '$("#imageNarrationEnabled").onchange'
        )
        toggle_handler = WEB_UI_SOURCE[toggle_handler_at : toggle_handler_at + 900]
        self.assertIn("loadPages()", toggle_handler)

        take_at = MAIN_SOURCE.index("llmNarrationTakeResult(")
        persist_at = MAIN_SOURCE.index("appConfig.setImageNarration(", take_at)
        redraw_gate_at = MAIN_SOURCE.index(
            "appConfig.imageNarrationEnabled()", persist_at
        )
        self.assertLess(take_at, persist_at)
        self.assertLess(persist_at, redraw_gate_at)

    def test_openai_request_runs_off_loop_with_tls_and_no_key_redirect(self) -> None:
        self.assertIn("void llmNarrationBegin();", LLM_HEADER)
        request = function_body(LLM_SOURCE, "bool llmNarrationRequest(")
        post = function_body(LLM_SOURCE, "bool postVisionRequest(")
        parse = function_body(LLM_SOURCE, "String responseContent(")
        self.assertIn("xTaskCreatePinnedToCore", request)
        self.assertIn("fetchRunning", request)
        self.assertIn("retryAllowed", request)
        self.assertIn("HTTP_METHOD_POST", post)
        self.assertIn("esp_crt_bundle_attach", post)
        self.assertIn("disable_auto_redirect", post)
        self.assertIn("Bearer ", post)
        self.assertIn('"Authorization"', post)
        self.assertIn('"Content-Type", "application/json"', post)
        self.assertIn(r'\"max_tokens\":96', compact(post))
        self.assertIn("/chat/completions", LLM_SOURCE)
        self.assertIn('document["choices"][0]["message"]["content"]', parse)
        self.assertRegex(LLM_SOURCE, r"MAX_HTTP_RESPONSE_BYTES\s*=\s*8192")
        self.assertRegex(LLM_SOURCE, r"HTTP_TIMEOUT_MS\s*=\s*30000")
        self.assertRegex(LLM_SOURCE, r"HTTP_REQUEST_BUDGET_MS\s*=\s*45000")

        read_response = function_body(LLM_SOURCE, "bool readResponse(")
        self.assertIn("esp_http_client_set_timeout_ms", LLM_SOURCE)
        self.assertIn(
            "HTTP_REQUEST_BUDGET_MS",
            function_body(LLM_SOURCE, "bool startHttpDeadlineGuard("),
        )
        self.assertIn("ESP_ERR_HTTP_EAGAIN", post)
        self.assertIn("ESP_ERR_HTTP_EAGAIN", read_response)
        self.assertIn("LlmConfigTestFailure::Timeout", post)
        self.assertIn("LlmConfigTestFailure::Timeout", read_response)
        self.assertIn("esp_http_client_get_socket", LLM_SOURCE)
        self.assertIn("shutdown", LLM_SOURCE)
        self.assertNotIn("esp_http_client_cancel_request", LLM_SOURCE)
        self.assertIn("xEventGroupWaitBits", LLM_SOURCE)
        self.assertIn("startHttpDeadlineGuard", post)
        self.assertIn("finishHttpDeadlineGuard", post)
        self.assertIn("HTTP_EVENT_ON_CONNECTED", LLM_SOURCE)
        self.assertIn("HTTP_EVENT_DISCONNECTED", LLM_SOURCE)
        self.assertIn("StaticSemaphore_t socketMutexStorage", LLM_SOURCE)
        self.assertIn("config.event_handler", post)
        self.assertIn("config.user_data", post)
        connected = function_body(
            LLM_SOURCE, "esp_err_t handleHttpClientEvent("
        )
        self.assertIn("publishHttpDeadlineSocket", connected)
        self.assertIn("invalidateHttpDeadlineSocket", connected)
        self.assertIn("esp_http_client_get_socket", connected)
        watchdog = function_body(LLM_SOURCE, "void httpDeadlineTask(")
        self.assertLess(watchdog.index("xSemaphoreTake"), watchdog.index("shutdown"))
        self.assertLess(watchdog.index("shutdown"), watchdog.index("xSemaphoreGive"))
        self.assertLess(
            post.index("startHttpDeadlineGuard"),
            post.index("esp_http_client_open"),
            "the request budget must begin before connect/header work in open",
        )

        self.assertIn("llmNarrationBegin()", MAIN_SOURCE)
        self.assertIn("llmNarrationRequest(", DISPLAY_SOURCE)
        self.assertIn("llmNarrationTakeResult(", MAIN_SOURCE)
        for serial_call in re.findall(
            r"Serial(?:\.printf|\.println)\((.*?)\);", LLM_SOURCE, re.DOTALL
        ):
            self.assertNotIn("apiKey", serial_call)

        gif_request = function_body(
            LLM_SOURCE, "bool llmNarrationRequestRgb565Alpha("
        )
        for request_body in (request, gif_request):
            task_failure = request_body[request_body.index("if (xTaskCreatePinnedToCore") :]
            self.assertIn("wipeMemory(job, sizeof(*job))", task_failure)
            self.assertLess(
                task_failure.index("wipeMemory(job, sizeof(*job))"),
                task_failure.index("heap_caps_free(job)"),
                "a worker-start failure must wipe the copied API key before free",
            )

    def test_rgb565_is_converted_to_a_supported_jpeg_payload(self) -> None:
        self.assertIn("espressif/esp_new_jpeg", IDF_MANIFEST)
        self.assertRegex(CMAKE_SOURCE, r"\bespressif__esp_new_jpeg\b")
        encode = function_body(LLM_SOURCE, "bool encodeRawImage(")
        load = function_body(LLM_SOURCE, "bool loadImagePayload(")
        self.assertIn("RAW_IMAGE_EXTENSION", load)
        self.assertIn("encodeRawImage", load)
        self.assertIn("JPEG_PIXEL_FORMAT_RGB565_LE", encode)
        self.assertIn("JPEG_SUBSAMPLE_420", encode)
        self.assertIn("jpeg_calloc_align", encode)
        self.assertIn("jpeg_enc_process", encode)
        self.assertIn('payload.mime = "image/jpeg"', encode)
        self.assertNotIn("image/rgb565", LLM_SOURCE)

    def test_animated_gif_uses_a_jpeg_snapshot_instead_of_raw_gif(self) -> None:
        capture = function_body(
            LLM_SOURCE, "bool llmNarrationRequestRgb565Alpha("
        )
        encode = function_body(LLM_SOURCE, "bool encodeRgb565AlphaFrame(")
        display = function_body(DISPLAY_SOURCE, "bool requestImageNarration(")
        self.assertIn("rgb565Alpha", capture)
        self.assertIn("memcpy(frameCopy, pixels, frameBytes)", capture)
        self.assertIn("JPEG_PIXEL_FORMAT_RGB565_LE", encode)
        self.assertIn('payload.mime = "image/jpeg"', encode)
        self.assertIn("mediaIsAnimatedPath(page.imagePath)", display)
        self.assertIn("llmNarrationRequestRgb565Alpha(", display)

    def test_narration_font_contains_common_description_characters(self) -> None:
        for character in "蓝狗草咖啡瀑布餐桌城市海滩建筑鲜花":
            self.assertIn(
                f'U+{ord(character):04X} "{character}"', UI_FONT_SOURCE
            )
        self.assertRegex(
            UI_FONT_SOURCE,
            r"\.fallback\s*=\s*NULL",
        )
        narration = function_body(DISPLAY_SOURCE, "void addImageNarration(")
        self.assertIn("&ui_font_misans_16", narration)

    def test_narration_font_contains_all_gb2312_han_characters(self) -> None:
        # Break caught: the previous font stopped at GB2312 level 1, so normal
        # level-2 characters such as “淇” and “瞰” rendered as tofu.
        expected = set()
        for lead in range(0xA1, 0xF8):
            for trail in range(0xA1, 0xFF):
                try:
                    char = bytes((lead, trail)).decode("gb2312")
                    if 0x4E00 <= ord(char) <= 0x9FFF:
                        expected.add(char)
                except UnicodeDecodeError:
                    pass
        self.assertEqual(len(expected), 6763)
        encoded = {
            chr(int(codepoint, 16))
            for codepoint in re.findall(r'U\+([0-9A-Fa-f]{4,6})\s+"', UI_FONT_SOURCE)
        }
        missing = expected - encoded
        self.assertFalse(
            missing,
            f"ui_font_misans_16 is missing {len(missing)} GB2312 glyphs: "
            + "".join(sorted(missing)[:24]),
        )
        self.assertTrue({"淇", "瞰"}.issubset(encoded))

    def test_narration_validation_rejects_glyphs_the_display_font_cannot_draw(self) -> None:
        # Font support is read directly from immutable cmaps. Calling LVGL's
        # public descriptor lookup here would race its mutable last-glyph cache
        # against the narration worker and the display task.
        self.assertTrue(
            UI_FONT_SUPPORT_PATH.exists(),
            "add the immutable-cmap ui_font_misans_16 coverage helper",
        )
        self.assertIn(
            "bool uiFontMiSansSupportsCodePoint(uint32_t codePoint)",
            UI_FONT_HEADER,
        )
        helper = function_body(
            UI_FONT_SUPPORT_SOURCE,
            "bool uiFontMiSansSupportsCodePoint(",
        )
        self.assertIn("ui_font_misans_16", helper)
        self.assertNotIn("->fallback", helper)
        for cmap_type in (
            "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY",
            "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL",
            "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
            "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL",
        ):
            self.assertIn(cmap_type, UI_FONT_SUPPORT_SOURCE)
        self.assertIn("glyph_id_ofs_list", UI_FONT_SUPPORT_SOURCE)
        self.assertNotRegex(
            UI_FONT_SUPPORT_SOURCE,
            r"\blv_font_(?:get_glyph_dsc|get_glyph_width|get_glyph_bitmap|"
            r"get_glyph_dsc_fmt_txt|get_bitmap_fmt_txt)\s*\(",
        )
        self.assertNotRegex(
            UI_FONT_SUPPORT_SOURCE,
            r"(?:->|\.)cache\b|last_letter|last_glyph_id",
        )

        validator = function_body(APP_CONFIG_SOURCE, "bool validImageNarration(")
        self.assertRegex(
            compact(validator),
            r"if\(!normalizedWhitespace&&"
            r"!uiFontMiSansSupportsCodePoint\(codePoint\)\)\{returnfalse;\}",
        )

    def test_font_coverage_lookup_handles_every_cmap_without_mutating_cache(self) -> None:
        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("a host C++ compiler is required for the cmap unit test")

        stub_header = r"""
            #pragma once
            #include <cstdint>

            struct lv_font_t;
            struct lv_font_glyph_dsc_t {};
            using glyph_callback_t = bool (*)(const lv_font_t*,
                                               lv_font_glyph_dsc_t*,
                                               uint32_t, uint32_t);
            enum : uint8_t {
              LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
              LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
              LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
              LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
            };
            struct lv_font_fmt_txt_cmap_t {
              uint32_t range_start;
              uint16_t range_length;
              uint16_t glyph_id_start;
              const uint16_t* unicode_list;
              const void* glyph_id_ofs_list;
              uint16_t list_length;
              uint8_t type;
            };
            struct lv_font_fmt_txt_glyph_cache_t {
              uint32_t last_letter;
              uint32_t last_glyph_id;
            };
            struct lv_font_fmt_txt_dsc_t {
              const lv_font_fmt_txt_cmap_t* cmaps;
              uint16_t cmap_num;
              lv_font_fmt_txt_glyph_cache_t* cache;
            };
            struct lv_font_t {
              glyph_callback_t get_glyph_dsc;
              const void* dsc;
              const lv_font_t* fallback;
            };
            bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t*,
                                                lv_font_glyph_dsc_t*,
                                                uint32_t, uint32_t);
            extern const lv_font_t ui_font_misans_16;
            bool uiFontMiSansSupportsCodePoint(uint32_t codePoint);
        """
        harness = r"""
            #include "ui_font.h"
            #include <cassert>
            #include <initializer_list>

            static int lookup_calls = 0;
            bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t*,
                                                lv_font_glyph_dsc_t*,
                                                uint32_t, uint32_t) {
              ++lookup_calls;
              return false;
            }

            static const uint8_t format0_full_offsets[] = {0, 1, 0, 2};
            static const uint16_t sparse_tiny_codes[] = {0, 2, 5};
            static const uint16_t sparse_full_codes[] = {0, 3, 5};
            static const uint16_t sparse_full_offsets[] = {0, 7, 2};
            static const lv_font_fmt_txt_cmap_t primary_cmaps[] = {
              {0x20, 2, 1, nullptr, nullptr, 0,
               LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},
              {100, 4, 3, nullptr, format0_full_offsets, 4,
               LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL},
              {200, 6, 6, sparse_tiny_codes, nullptr, 3,
               LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},
              {300, 6, 9, sparse_full_codes, sparse_full_offsets, 3,
               LV_FONT_FMT_TXT_CMAP_SPARSE_FULL},
            };
            static lv_font_fmt_txt_glyph_cache_t primary_cache = {
              0x12345678, 0x87654321
            };
            static const lv_font_fmt_txt_dsc_t primary_dsc = {
              primary_cmaps, 4, &primary_cache
            };
            static const lv_font_fmt_txt_cmap_t fallback_cmaps[] = {
              {400, 1, 1, nullptr, nullptr, 0,
               LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},
            };
            static lv_font_fmt_txt_glyph_cache_t fallback_cache = {11, 22};
            static const lv_font_fmt_txt_dsc_t fallback_dsc = {
              fallback_cmaps, 1, &fallback_cache
            };
            static const lv_font_t fallback_font = {
              lv_font_get_glyph_dsc_fmt_txt, &fallback_dsc, nullptr
            };
            const lv_font_t ui_font_misans_16 = {
              lv_font_get_glyph_dsc_fmt_txt, &primary_dsc, &fallback_font
            };

            int main() {
              for (uint32_t supported : {0x20u, 0x21u, 100u, 101u, 103u,
                                         200u, 202u, 205u, 300u, 303u, 305u}) {
                assert(uiFontMiSansSupportsCodePoint(supported));
              }
              for (uint32_t unsupported : {0x22u, 102u, 104u, 201u, 204u,
                                           301u, 304u, 400u, 999u}) {
                assert(!uiFontMiSansSupportsCodePoint(unsupported));
              }
              assert(lookup_calls == 0);
              assert(primary_cache.last_letter == 0x12345678);
              assert(primary_cache.last_glyph_id == 0x87654321);
              assert(fallback_cache.last_letter == 11);
              assert(fallback_cache.last_glyph_id == 22);
              return 0;
            }
        """
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "ui_font.h").write_text(
                textwrap.dedent(stub_header), encoding="utf-8"
            )
            (directory / "harness.cpp").write_text(
                textwrap.dedent(harness), encoding="utf-8"
            )
            executable = directory / "font_support_test"
            compilation = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-I",
                    str(directory),
                    str(UI_FONT_SUPPORT_PATH),
                    str(directory / "harness.cpp"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compilation.returncode, 0, compilation.stderr)
            subprocess.run([str(executable)], check=True)

    def test_image_data_is_base64_streamed_in_independently_decodable_chunks(self) -> None:
        input_chunk = re.search(
            r"BASE64_INPUT_CHUNK_BYTES\s*=\s*(\d+)", LLM_SOURCE
        )
        self.assertIsNotNone(input_chunk)
        self.assertEqual(int(input_chunk.group(1)) % 3, 0)
        encode = function_body(LLM_SOURCE, "bool writeBase64(")
        post = function_body(LLM_SOURCE, "bool postVisionRequest(")
        self.assertIn("while (offset < size)", encode)
        self.assertIn("mbedtls_base64_encode", encode)
        self.assertIn(
            "writeAll(client, encoded, outputSize, requestStartedAt)", encode
        )
        self.assertIn("((image.size + 2) / 3) * 4", post)
        self.assertIn("esp_http_client_open(client, bodySize)", post)
        self.assertIn(
            "writeBase64(client,image.data,image.size,requestStartedAt)",
            compact(post),
        )
        self.assertIn(r'\"type\":\"image_url\"', compact(post))
        self.assertIn(r'\"url\":\"data:', compact(post))

    def test_narration_is_limited_by_utf8_codepoints_and_only_success_is_published(self) -> None:
        self.assertIn("LLM_NARRATION_MAX_CODEPOINTS = 64", LLM_HEADER)
        sanitize = function_body(LLM_SOURCE, "String sanitizeNarration(")
        self.assertIn("utf8SequenceLength", sanitize)
        self.assertIn("codepoints < LLM_NARRATION_MAX_CODEPOINTS", sanitize)
        self.assertIn("codepoints", sanitize)
        self.assertIn("sequenceLength", sanitize)
        self.assertIn("& 0xC0", sanitize)
        self.assertNotRegex(sanitize, r"substring\(\s*0\s*,\s*64\s*\)")

        task = function_body(LLM_SOURCE, "void narrationTask(")
        finish = function_body(LLM_SOURCE, "void finishJob(")
        self.assertIn("sanitizeNarration(responseContent(body))", task)
        self.assertIn("ok = !narration.isEmpty()", task)
        self.assertIn("wipeMemory(job, sizeof(*job))", task)
        self.assertLess(task.index("wipeMemory(job, sizeof(*job))"), task.rindex("finishConfigTest("))
        self.assertLess(task.index("wipeString(body)"), task.rindex("finishConfigTest("))
        self.assertRegex(compact(finish), r"if\(!activeInvalidated&&ok&&!resultPending\)")
        self.assertNotIn("resultPending = true", finish[finish.index("else if (!activeInvalidated && !ok)") :])

    def test_narration_contract_rejects_invalid_utf8_markdown_emoji_and_non_cjk(self) -> None:
        self.assertIn(
            "bool validImageNarration(const String& value)", APP_CONFIG_HEADER
        )
        validator = function_body(APP_CONFIG_SOURCE, "bool validImageNarration(")
        for guard in (
            "decodeUtf8CodePoint",
            "containsMarkdownDecoration",
            "isEmojiCodePoint",
            "isCjkCodePoint",
            "MAX_IMAGE_NARRATION_CODEPOINTS",
        ):
            self.assertIn(guard, validator)

        sanitize = function_body(LLM_SOURCE, "String sanitizeNarration(")
        self.assertIn("validImageNarration", sanitize)
        self.assertNotIn('text.replace("```", "")', sanitize)
        self.assertRegex(
            compact(sanitize),
            r"if\(!validImageNarration\(text\)\)\{return\"\";\}",
        )

        commit = function_body(APP_CONFIG_SOURCE, "bool AppConfig::setImageNarration(")
        load = function_body(APP_CONFIG_SOURCE, "bool AppConfig::load()")
        self.assertIn("validImageNarration(normalized)", commit)
        self.assertIn("validImageNarration(page.narration)", load)

    def test_commit_is_compare_and_set_and_propagates_to_the_same_image(self) -> None:
        commit = function_body(APP_CONFIG_SOURCE, "bool AppConfig::setImageNarration(")
        target = function_body(APP_CONFIG_SOURCE, "bool AppConfig::imageNarrationTargetExists(")
        self.assertIn("imageNarrationTargetExists(pageId, expectedPath)", commit)
        self.assertIn("findPage(pageId)", target)
        self.assertIn("expectedPath", target)
        self.assertIn("imagePath", target)
        self.assertIn("PageType::Image", target)
        self.assertRegex(
            compact(target),
            r"pages_\[.*\]\.imagePath!=expectedPath",
            "a late response must not attach to a reused page id",
        )
        self.assertIn("if (target < 0)", target)
        self.assertIn("pathStillReferenced", target)
        self.assertRegex(
            compact(target),
            r"pathStillReferenced=true.*if\(!pathStillReferenced\)\{returnfalse;\}",
            "a deleted source may commit only while the same asset remains referenced",
        )
        self.assertIn("for (size_t", commit)
        propagation_loop = compact(commit)[compact(commit).index("for(size_t") :]
        self.assertRegex(
            propagation_loop,
            r"pages_\[i\]\.imagePath(?:==|!=)expectedPath",
            "every page backed by the same image should receive the result",
        )
        self.assertIn(".narration", commit)
        self.assertIn("save()", commit)
        self.assertRegex(
            commit,
            r"if\s*\(.*save\(\).*\)[\s\S]*return true;[\s\S]*narration",
            "a failed config write must restore the previous narration values",
        )

        add_image = function_body(APP_CONFIG_SOURCE, "bool AppConfig::addImage(")
        self.assertIn("pages_[", add_image)
        self.assertIn("imagePath == path", add_image)
        self.assertIn("narration", add_image)

    def test_display_requests_only_missing_text_and_main_saves_accepted_results(self) -> None:
        schedule = function_body(DISPLAY_SOURCE, "bool requestImageNarration(")
        self.assertIn("page.narration.isEmpty()", schedule)
        self.assertIn("!regenerate && !page.narration.isEmpty()", schedule)
        self.assertIn("appConfig.llmConfigured()", schedule)
        self.assertIn("llmNarrationRequest(", schedule)
        self.assertIn("llmNarrationTakeResult(", MAIN_SOURCE)
        self.assertIn("appConfig.setImageNarration(", MAIN_SOURCE)
        apply_at = MAIN_SOURCE.index("appConfig.setImageNarration(")
        dirty_at = MAIN_SOURCE.find("displayMarkContentDirty()", apply_at)
        self.assertGreater(dirty_at, apply_at)
        guarded_apply = MAIN_SOURCE[apply_at - 80 : dirty_at]
        self.assertRegex(guarded_apply, r"if\s*\(")

    def test_existing_narration_is_drawn_on_direct_and_sliding_image_paths(self) -> None:
        candidates = []
        for match in re.finditer(
            r"(?:void|lv_obj_t\s*\*)\s+(\w*[Nn]arration\w*)\s*\(",
            DISPLAY_SOURCE,
        ):
            signature = match.group(0)
            body = function_body(DISPLAY_SOURCE, signature)
            if "page.narration" in body and "addLabel(" in body:
                candidates.append((match.group(1), body))
        self.assertTrue(candidates, "define one image narration rendering helper")
        helper_name, helper = candidates[0]
        self.assertIn("page.narration.isEmpty()", helper)
        self.assertIn("addLabel(", helper)

        direct = function_body(DISPLAY_SOURCE, "void renderImagePage(")
        slide = function_body(DISPLAY_SOURCE, "void populatePageContent(")
        helper_call = f"{helper_name}("
        self.assertIn(helper_call, direct)
        self.assertIn(helper_call, slide)
        self.assertLess(direct.index(helper_call), direct.index("addPlaybackClock("))

        transition = function_body(DISPLAY_SOURCE, "bool startSlideTransition(")
        self.assertIn("populatePageContent(", transition)

    def test_llm_configuration_test_is_async_isolated_and_sanitized(self) -> None:
        for public_api in (
            "bool llmNarrationTestStart(",
            "bool llmNarrationTestStatus(",
            "bool llmNarrationTestStatusByRequestToken(",
            "bool llmNarrationTestBusy();",
            "void llmNarrationLoop();",
        ):
            self.assertIn(public_api, LLM_HEADER)

        self.assertIn("llmNarrationLoop()", MAIN_SOURCE)
        self.assertIn("CONFIG_TEST_IMAGE", LLM_SOURCE)
        self.assertIn('"image/png"', LLM_SOURCE)
        self.assertIn("LlmConfigTestState::Queued", LLM_SOURCE)
        self.assertIn("LlmConfigTestState::Running", LLM_SOURCE)

        start_test = function_body(LLM_SOURCE, "bool llmNarrationTestStart(")
        get_test = function_body(LLM_SOURCE, "bool llmNarrationTestStatus(")
        self.assertIn("previousConfigTestStatus", start_test)
        self.assertIn("previousConfigTestStatus", get_test)
        self.assertLess(
            start_test.index("previousConfigTestStatus"),
            start_test.index("configTestStatus = {}"),
            "starting a new test must retain the preceding terminal result",
        )

        finish_test = function_body(LLM_SOURCE, "void finishConfigTest(")
        self.assertNotIn("resultPending", finish_test)
        self.assertNotIn("rememberFailure", finish_test)
        self.assertNotIn("pendingResult", finish_test)

        loop = function_body(LLM_SOURCE, "void llmNarrationLoop(")
        self.assertLess(
            loop.index("disposeNarrationJob(job)"),
            loop.index("finishConfigTest("),
            "worker-start failure must wipe the secret-bearing job before publishing idle",
        )
        dispose = function_body(LLM_SOURCE, "void disposeNarrationJob(")
        self.assertLess(dispose.index("wipeMemory(job, sizeof(*job))"),
                        dispose.index("heap_caps_free(job)"))

        normal_request = function_body(LLM_SOURCE, "bool llmNarrationRequest(")
        gif_request = function_body(
            LLM_SOURCE, "bool llmNarrationRequestRgb565Alpha("
        )
        self.assertIn("configTestBusy", normal_request)
        self.assertIn("configTestBusy", gif_request)

    def test_web_can_test_current_unsaved_llm_fields_without_persisting_them(self) -> None:
        self.assertIn('id="testLlmSettings"', WEB_UI_SOURCE)
        self.assertRegex(
            WEB_UI_SOURCE,
            r'id="llmTestState"[^>]*role="status"[^>]*aria-live="polite"',
        )
        self.assertIn('api("/api/llm/test"', WEB_UI_SOURCE)
        self.assertIn('api(`/api/llm/test?id=${encodeURIComponent(testId)}`', WEB_UI_SOURCE)
        self.assertRegex(
            WEB_UI_SOURCE,
            r'\$\("#testLlmSettings"\)\.onclick[\s\S]*?baseUrl:\$\("#llmBaseUrl"\)\.value\.trim\(\)[\s\S]*?apiKey:\$\("#llmApiKey"\)\.value\.trim\(\)[\s\S]*?model:\$\("#llmModel"\)\.value\.trim\(\)',
        )
        self.assertIn("setLlmFormBusy(true)", WEB_UI_SOURCE)
        self.assertIn("setLlmFormBusy(false)", WEB_UI_SOURCE)
        self.assertIn("Date.now()+105000", WEB_UI_SOURCE)
        self.assertIn("AbortController", function_body(WEB_UI_SOURCE, "async function pollLlmTest("))
        test_click = function_body(
            WEB_UI_SOURCE, '$("#testLlmSettings").onclick'
        )
        self.assertIn("crypto.getRandomValues", WEB_UI_SOURCE)
        self.assertIn("requestToken", test_click)
        self.assertIn("AbortController", test_click)
        self.assertLess(
            test_click.index("rememberPendingLlmRequestToken"),
            test_click.index('api("/api/llm/test"'),
            "the tab must remember ownership before the POST can become ambiguous",
        )
        poll_test = function_body(WEB_UI_SOURCE, "async function pollLlmTest(")
        self.assertIn(
            'api(`/api/llm/test?requestToken=${encodeURIComponent(requestToken)}`',
            poll_test,
        )
        self.assertIn("notFoundGraceUntil", poll_test)
        self.assertIn("llmTestPreviousConfig", WEB_UI_SOURCE)
        resume_test = function_body(
            WEB_UI_SOURCE, "function resumeRememberedLlmTest("
        )
        self.assertRegex(
            resume_test,
            r"llmTestResumed\s*=\s*true",
            "a resumed test must be visibly distinguished from the hydrated saved fields",
        )
        self.assertIn("rememberedLlmTestId", resume_test)
        self.assertIn("rememberedPendingLlmRequestToken", resume_test)
        self.assertLess(
            resume_test.index("rememberedLlmTestId"),
            resume_test.index("rememberedPendingLlmRequestToken"),
            "a tab-owned test id must take priority over an unconfirmed token",
        )
        self.assertNotIn("device.llmTestId", resume_test)
        self.assertIn(
            "llmTestPostPending",
            resume_test,
            "status refreshes must not start a second observer while POST is unresolved",
        )
        self.assertIn(
            "llmTestPostPending",
            function_body(WEB_UI_SOURCE, "async function loadStatus("),
            "status refreshes must not overwrite unsaved fields during the test POST",
        )
        self.assertIn(
            "llmFormDirty",
            function_body(WEB_UI_SOURCE, "async function loadStatus("),
            "unrelated status refreshes must preserve edited or tested LLM fields",
        )
        load_status = function_body(WEB_UI_SOURCE, "async function loadStatus(")
        self.assertIn("llmStatusRequestSequence", load_status)
        self.assertIn("requestSequence!==llmStatusRequestSequence", compact(load_status))
        self.assertNotIn(
            "!llmFormHydrated&&!llmFormDirty",
            compact(load_status),
            "a clean tab must accept LLM settings saved from another tab",
        )
        hydrate_llm = function_body(WEB_UI_SOURCE, "function hydrateLlmSettings(")
        self.assertIn("llmConfigRevision", hydrate_llm)
        self.assertIn("invalidateLlmTestResult", hydrate_llm)
        self.assertNotIn(
            "imageNarrationEnabled",
            hydrate_llm,
            "the narration switch must not be blocked by dirty LLM text fields",
        )
        hydrate_narration = function_body(
            WEB_UI_SOURCE, "function hydrateImageNarrationSetting("
        )
        self.assertIn(
            '$("#imageNarrationEnabled").checked=device.imageNarrationEnabled!==false',
            compact(hydrate_narration),
        )
        self.assertIn(
            "if(!llmNarrationTogglePending)hydrateImageNarrationSetting()",
            compact(load_status),
            "status refreshes must hydrate the switch independently of LLM form edits",
        )
        narration_toggle = function_body(
            WEB_UI_SOURCE, '$("#imageNarrationEnabled").onchange'
        )
        narration_toggle_compact = compact(narration_toggle)
        self.assertIn("llmNarrationTogglePending=true", narration_toggle_compact)
        self.assertIn("llmNarrationTogglePending=false", narration_toggle_compact)
        self.assertIn("conststatusRefresh=loadStatus()", narration_toggle_compact)
        self.assertIn("awaitstatusRefresh", narration_toggle_compact)
        status_refresh_at = narration_toggle_compact.index(
            "conststatusRefresh=loadStatus()"
        )
        self.assertGreater(
            narration_toggle_compact.index(
                "llmNarrationTogglePending=false", status_refresh_at
            ),
            status_refresh_at,
            "the new status request must invalidate older responses before hydration resumes",
        )
        self.assertIn("llmTestMatchesCurrentForm=true", compact(test_click))
        self.assertIn("llmTestSettingsChanged", WEB_UI_SOURCE)
        settings_changed = function_body(
            WEB_UI_SOURCE, "function markLlmSettingsChanged("
        )
        self.assertIn("llmFormDirty=true", compact(settings_changed))
        self.assertIn("invalidateLlmTestResult", settings_changed)
        invalidate_result = function_body(
            WEB_UI_SOURCE, "function invalidateLlmTestResult("
        )
        self.assertIn("llmTestMatchesCurrentForm", invalidate_result)
        self.assertIn("llmTestSettingsChanged", invalidate_result)
        self.assertRegex(
            WEB_UI_SOURCE,
            r'\["#llmBaseUrl","#llmApiKey","#llmModel","#llmAllowInsecureHttp"\][\s\S]*?markLlmSettingsChanged',
            "editing any tested LLM input must invalidate the displayed result",
        )
        self.assertIn("renderLlmTestState()", function_body(WEB_UI_SOURCE, "function applyLanguage("))
        self.assertRegex(
            WEB_UI_SOURCE,
            r"@media\s*\(prefers-reduced-motion:reduce\)[^{]*\{[^}]*\.llm-test-state\[data-state=running\]:before\s*\{[^}]*animation:none",
        )

        post_route = route_body(
            WEB_SOURCE, 'server.on("/api/llm/test", HTTP_POST'
        )
        self.assertIn('server.arg("csrfToken")', post_route)
        self.assertIn("llmSettingsToken", post_route)
        self.assertIn("llmNarrationTestStart", post_route)
        self.assertIn("llmNarrationTestBusy", post_route)
        self.assertIn('server.arg("requestToken")', post_route)
        self.assertIn("llmConfigTestRequestTokenValid", post_route)
        self.assertIn("previousApiKey", post_route)
        self.assertIn("ScopedStringWipe", post_route)
        self.assertNotIn("apiKey.trim()", post_route)
        self.assertIn("baseUrl != previousBaseUrl", post_route)
        self.assertIn("model != previousModel", post_route)
        self.assertNotIn("appConfig.save()", post_route)
        self.assertNotIn("appConfig.setLlmConfig", post_route)
        self.assertNotIn("appConfig.clearLlmConfig", post_route)

        save_route = route_body(WEB_SOURCE, 'server.on("/api/llm", HTTP_POST')
        self.assertIn("ScopedStringWipe", save_route)
        self.assertNotIn("apiKey.trim()", save_route)

        get_route = route_body(
            WEB_SOURCE, 'server.on("/api/llm/test", HTTP_GET'
        )
        self.assertIn("llmNarrationTestStatus", get_route)
        self.assertIn("llmNarrationTestStatusByRequestToken", get_route)
        self.assertIn('server.arg("requestToken")', get_route)
        self.assertIn('doc["state"]', get_route)
        self.assertIn('doc["reason"]', get_route)
        self.assertIn('doc["httpStatus"]', get_route)
        self.assertNotIn("apiKey", get_route)
        self.assertNotIn("baseUrl", get_route)
        self.assertNotIn("model", get_route)

        status_route = route_body(WEB_SOURCE, 'server.on("/api/status", HTTP_GET')
        self.assertNotIn("llmNarrationActiveTestStatus", status_route)
        self.assertNotIn('doc["llmTestId"]', status_route)
        self.assertIn('doc["llmConfigRevision"]', status_route)
        self.assertIn("llmConfigRevision", save_route)


if __name__ == "__main__":
    unittest.main(verbosity=2)
