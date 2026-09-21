# Device fonts

The firmware uses MiSans Regular for Chinese and English, with Noto Emoji
for monochrome emoji. Xiaomi permits embedding with
an in-software attribution; the System settings footer credits MiSans / Xiaomi.
The original font retains its [MiSans license](MiSans-LICENSE.txt), separately
from the application license. The official font package is not vendored.

The 16px font contains all 6,763 GB2312 Han characters plus ASCII, fullwidth
English/digits, and common Chinese/English punctuation (6,976 glyphs total).
Other sizes (14, 20, 24, 28, 32, 40, 48px) contain the same English/punctuation
set; Chinese labels retain the existing 16px layout. The MiSans fonts exclude
Japanese kana, Korean, Greek, and Cyrillic glyphs. Weather icons are image
assets, not another typeface.

Emoji text pages use the restored 32px / 4-bit Noto Emoji subset (1,413 glyphs),
with MiSans 16px as the fallback for Chinese and English. Variation selector
FE0F and ZWJ are removed as before; complex sequences may render separately.
Emoji cmaps use separate sparse tables for BMP and supplementary codepoints
so unsupported symbols cannot shadow MiSans fallback glyphs. Noto Emoji retains
its [SIL Open Font License](NotoEmoji-OFL.txt).

## Regenerate

Download **MiSans** from <https://hyperos.mi.com/font/zh/download/> and extract
`MiSans/ttf/MiSans-Regular.ttf` outside the repository. Install `lv_font_conv`
version 1.5.3 outside the repository, then run:

```sh
python3 tools/generate_misans_fonts.py /path/to/MiSans-Regular.ttf /path/to/node_modules/lv_font_conv/lv_font_conv.js
```

The script verifies the source font SHA-256 before rasterizing. The Chinese
font uses 2-bit grayscale; other sizes use 4-bit grayscale. Generated C arrays
are compiled into firmware, with no filesystem or runtime font dependency.
Sparse character maps avoid LVGL 8's false glyph hits at holes and boundaries.
The root CMake configuration sets MiSans as LVGL's default font and disables
the built-in font selected by LVGL 8's built-in-only Kconfig default choice.
