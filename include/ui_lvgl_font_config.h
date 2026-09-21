#pragma once

// LVGL 8's Kconfig default-font choice only lists built-in fonts. Apply these
// overrides to every component, including widgets that use LV_FONT_DEFAULT.
#define LV_FONT_DEFAULT (&ui_font_misans_16)
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(ui_font_misans_16)
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
