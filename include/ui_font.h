#pragma once

#include <lvgl.h>

LV_FONT_DECLARE(ui_font_misans_14);
LV_FONT_DECLARE(ui_font_misans_16);
LV_FONT_DECLARE(ui_font_misans_20);
LV_FONT_DECLARE(ui_font_misans_24);
LV_FONT_DECLARE(ui_font_misans_28);
LV_FONT_DECLARE(ui_font_misans_32);
LV_FONT_DECLARE(ui_font_misans_40);
LV_FONT_DECLARE(ui_font_misans_48);
LV_FONT_DECLARE(ui_font_emoji_32);

// Checks the generated narration font without touching LVGL's mutable
// last-glyph cache. Narration validation runs on a worker task while LVGL may
// be rendering on another core, so the public glyph lookup is not safe here.
bool uiFontMiSansSupportsCodePoint(uint32_t codePoint);
