#include "ui_font.h"

namespace {
bool sparseCmapContains(const uint16_t* values, uint16_t count,
                        uint16_t target) {
  uint16_t low = 0;
  uint16_t high = count;
  while (low < high) {
    const uint16_t middle = low + (high - low) / 2;
    if (values[middle] < target) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low < count && values[low] == target;
}

bool cmapContains(const lv_font_fmt_txt_cmap_t& cmap, uint32_t codePoint) {
  if (codePoint < cmap.range_start) {
    return false;
  }
  const uint32_t relative = codePoint - cmap.range_start;
  if (relative >= cmap.range_length) {
    return false;
  }

  switch (cmap.type) {
    case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY:
      return true;
    case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL: {
      if (!cmap.glyph_id_ofs_list || relative >= cmap.list_length) {
        return false;
      }
      const auto* offsets =
          static_cast<const uint8_t*>(cmap.glyph_id_ofs_list);
      // Offset zero represents both the first real glyph and holes later in
      // the range. The converter always starts a subtable at a real glyph.
      return relative == 0 || offsets[relative] != 0;
    }
    case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY:
      return cmap.unicode_list && relative <= UINT16_MAX &&
             sparseCmapContains(cmap.unicode_list, cmap.list_length,
                                static_cast<uint16_t>(relative));
    case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
      return cmap.unicode_list && cmap.glyph_id_ofs_list &&
             relative <= UINT16_MAX &&
             sparseCmapContains(cmap.unicode_list, cmap.list_length,
                                static_cast<uint16_t>(relative));
    default:
      return false;
  }
}

bool fontContains(const lv_font_t* font, uint32_t codePoint) {
  if (!font || !font->dsc ||
      font->get_glyph_dsc != lv_font_get_glyph_dsc_fmt_txt) {
    return false;
  }
  const auto* description =
      static_cast<const lv_font_fmt_txt_dsc_t*>(font->dsc);
  if (!description->cmaps) {
    return false;
  }
  for (uint16_t index = 0; index < description->cmap_num; ++index) {
    if (cmapContains(description->cmaps[index], codePoint)) {
      return true;
    }
  }
  return false;
}
}  // namespace

bool uiFontMiSansSupportsCodePoint(uint32_t codePoint) {
  // Narration uses MiSans. The generated sparse map describes exactly
  // what can render, without consulting LVGL's shared mutable glyph cache.
  return fontContains(&ui_font_misans_16, codePoint);
}
