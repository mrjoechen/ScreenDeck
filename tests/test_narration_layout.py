"""Exercise the firmware narration layout with real LVGL, font and PNG assets."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_llm_narration_guards import function_body

ROOT = Path(__file__).resolve().parents[1]
LVGL = ROOT / "managed_components/lvgl__lvgl"


class NarrationLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        directory = Path(cls.directory.name)
        library = directory / ("liblvgl.dylib" if sys.platform == "darwin" else "liblvgl.so")
        flags = [
            "-DLV_CONF_SKIP", "-DLV_KCONFIG_IGNORE", "-DLV_LVGL_H_INCLUDE_SIMPLE",
            "-DLV_MEM_CUSTOM=1", "-DLV_USE_PNG=1",
            "-DLV_FONT_MONTSERRAT_14=0", "-DLV_FONT_CUSTOM_DECLARE=LV_FONT_DECLARE(ui_font_misans_16)", "-DLV_FONT_DEFAULT=&ui_font_misans_16",
            "-I", str(LVGL), "-I", str(ROOT / "include"),
        ]
        compiled = subprocess.run(
            ["cc", "-std=c99", "-dynamiclib" if sys.platform == "darwin" else "-shared",
             "-fPIC", *flags, *map(str, sorted((LVGL / "src").rglob("*.c"))),
             *map(str, sorted((ROOT / "src").glob("ui_font_misans_*.c"))),
             str(ROOT / "src/ui_font_emoji_32.c"),
             str(ROOT / "src/narration_icon.c"), "-o", str(library)],
            text=True, capture_output=True, timeout=120,
        )
        if compiled.returncode:
            raise AssertionError(compiled.stderr)

        cls.library = library
        cls.flags = flags
        display = (ROOT / "src/display_ui.cpp").read_text()
        touch_source = r'''
            #include "image_narration_gesture.h"
            uint32_t now = 1000;
            uint32_t millis() { return now; }
            uint32_t eligiblePage = 7;
            uint32_t narrationGesturePageId() { return eligiblePage; }
            bool touchPressed=false, touchWasPressed=false;
            bool scheduledScreenOff=false, touchBeganWhileBlanked=false;
            bool touchMovedForNarration=false, provisioningScreen=false;
            bool deviceSettingsScreen=false, sdRescanRequested=false;
            bool pendingSystemPage=false;
            int currentPage=1, pendingSwipe=0;
            enum class ScreenRequest { None, DeviceSettings };
            ScreenRequest pendingScreenRequest=ScreenRequest::None;
            uint32_t touchStartAt=0, touchNarrationPageId=0, pendingNarrationPageId=0;
            uint32_t lastTouchAt=0, lastPageChangeAt=0, lastSwipeAt=0;
            int16_t touchX=0, touchY=0, touchStartX=0, touchStartY=0;
            int16_t lastTouchX=0, lastTouchY=0;
            constexpr int SCREEN_WIDTH=480, SCREEN_HEIGHT=480;
            constexpr int TAP_MAX_MOVEMENT=24, SWIPE_THRESHOLD=60;
            ImageNarrationGesture narrationGesture;
            int constrain(int n,int low,int high) { return std::clamp(n,low,high); }
            void updateTouchState() {}
            void noteWakeActivity(uint32_t) {}
            void registerBlankedTap(uint32_t,int,int,uint32_t) {}
            bool mediaStoreSdMounted() { return true; }
        '''
        for signature in (
            "bool narrationHitTest(lv_obj_t* obj, const lv_point_t& point)",
            "uint32_t narrationGestureHitPageId(int16_t x, int16_t y)",
        ):
            if signature in display:
                touch_source += signature + " {" + function_body(display, signature) + "}\n"
        touch_source += "void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {" + function_body(display, "void readTouch(") + r'''}
            void tap(int x, int y, int endX=-1, int endY=-1) {
              lv_indev_data_t data{};
              touchX=x; touchY=y; touchPressed=true; readTouch(nullptr,&data);
              now+=80;
              if(endX>=0) { touchX=endX; touchY=endY; readTouch(nullptr,&data); }
              touchPressed=false; readTouch(nullptr,&data); now+=100;
            }
        '''
        source = r'''
            #include <algorithm>
            #include <cassert>
            #include <cstdio>
            #include <cstdlib>
            #include <cstring>
            #include <string>
            #include "ui_font.h"
            #include "narration_icon.h"
            struct String : std::string {
              using std::string::string;
              bool isEmpty() const { return empty(); }
            };
            struct ContentPage { String narration; String imagePath="/a.jpg"; };
            bool narrationBusy=false;
            bool llmNarrationBusyForImage(const String& path) {
              return narrationBusy && path=="/a.jpg";
            }
            const char* uiText(const char* zh, const char*) { return zh; }
            struct {
              bool enabled = true;
              bool imageNarrationEnabled() const { return enabled; }
            } appConfig;
            bool imageNarrationVisible = false;
            int requests = 0;
            bool requestImageNarration(const ContentPage&) { ++requests; return true; }
            bool containsCjk(const String& text) {
        ''' + function_body(display, "bool containsCjk(const String& text) {") + r'''
            }
            lv_obj_t* addLabel(lv_obj_t* parent, const String& text,
                              const lv_font_t* font, uint32_t color, int width,
                              lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER) {
        ''' + function_body(display, "lv_obj_t* addLabel(") + r'''
            }
        ''' + touch_source + r'''
        ''' + ("void refreshImageNarration(lv_obj_t* obj, const ContentPage& page) {" +
               function_body(display, "void refreshImageNarration(") + "}\n"
               if "void refreshImageNarration(" in display else
               "void refreshImageNarration(lv_obj_t*, const ContentPage&) {}\n") + r'''
            void addImageNarration(lv_obj_t* parent, const ContentPage& page) {
        ''' + function_body(display, "void addImageNarration(") + r'''
            }
            int main(int argc, char** argv) {
              assert(argc == 2);
              const int scenario = std::atoi(argv[1]);
              lv_init();
              lv_disp_drv_t driver;
              lv_disp_drv_init(&driver);
              driver.hor_res = 480;
              driver.ver_res = 480;
              lv_disp_drv_register(&driver);
              lv_obj_t* screen = lv_obj_create(nullptr);
              lv_scr_load(screen);
              lv_obj_set_style_pad_all(screen, 0, 0);
              lv_obj_set_style_border_width(screen, 0, 0);
              lv_obj_t* parent = screen;
              if (scenario == 2) {
                // The same overlay also lives inside an offscreen sliding pane.
                parent = lv_obj_create(screen);
                lv_obj_set_style_pad_all(parent, 0, 0);
                lv_obj_set_style_border_width(parent, 0, 0);
                lv_obj_set_size(parent, 480, 480);
                lv_obj_set_x(parent, 480);
              }
              ContentPage page{"山间晨光"};
              if (scenario != 0) {
                page.narration.clear();
                for (int i = 0; i < 64; ++i) page.narration += "山";
              }
              addImageNarration(parent, page);
              lv_obj_update_layout(screen);
              assert(imageNarrationVisible && requests == 0);
              assert(lv_obj_get_child_cnt(parent) == 1);
              lv_obj_t* card = lv_obj_get_child(parent, 0);
              assert(lv_obj_get_child_cnt(card) == 3);
              lv_obj_t* icon = lv_obj_get_child(card, 0);
              lv_obj_t* label = lv_obj_get_child(card, 1);
              lv_obj_t* bold = lv_obj_get_child(card, 2);
              lv_area_t bounds, iconBox, textBox, boldBox, parentBox;
              lv_obj_get_coords(card, &bounds);
              lv_obj_get_coords(icon, &iconBox);
              lv_obj_get_coords(label, &textBox);
              lv_obj_get_coords(bold, &boldBox);
              lv_obj_get_coords(parent, &parentBox);
              const int left = iconBox.x1 - parentBox.x1;
              const int bottom = parentBox.y2 - std::max(iconBox.y2, textBox.y2);
              std::fprintf(stderr, "left=%d bottom=%d cardHeight=%d textHeight=%d\n",
                           left, bottom, lv_obj_get_height(card), lv_obj_get_height(label));
              // Break caught: matching only the empty card's margins while
              // centering short text inside a fixed-height box still adds a gap.
              assert(left == 28);
              assert(bottom == left);
              assert(lv_obj_get_height(icon) == 24);
              assert(lv_obj_get_height(label) == (scenario == 0 ? 19 : 67));
              assert(lv_obj_get_height(card) == (scenario == 0 ? 24 : 67));
              for (const auto& box : {iconBox, textBox, boldBox}) {
                assert(box.x1 >= bounds.x1 && box.x2 <= bounds.x2);
                assert(box.y1 >= bounds.y1 && box.y2 <= bounds.y2);
              }
              assert(lv_obj_get_style_bg_opa(card, 0) == LV_OPA_TRANSP);
              assert(std::strcmp(lv_label_get_text(label), lv_label_get_text(bold)) == 0);
              assert(lv_obj_get_width(label) == 387 && lv_obj_get_width(bold) == 387);
              assert(boldBox.x1 == textBox.x1 + 1);
              assert(boldBox.y1 == textBox.y1 && boldBox.y2 == textBox.y2);
              assert(iconBox.y1 + lv_obj_get_height(icon) / 2 ==
                     textBox.y1 + lv_obj_get_height(label) / 2);

              if (scenario != 2) {
                const String cached = page.narration;
                narrationBusy=true;
                refreshImageNarration(screen,page);
                lv_obj_update_layout(screen);
                assert(std::strcmp(lv_label_get_text(label),"正在重新生成摘要…")==0);
                assert(std::strcmp(lv_label_get_text(label),lv_label_get_text(bold))==0);
                assert(page.narration==cached); // Status never overwrites cache.
                // Revisiting a still-running image starts with status, even
                // while its new screen has not been made active yet.
                lv_obj_t* revisited=lv_obj_create(nullptr);
                addImageNarration(revisited,page);
                lv_obj_t* revisitedCard=lv_obj_get_child(revisited,0);
                assert(std::strcmp(lv_label_get_text(lv_obj_get_child(revisitedCard,1)),
                                   "正在重新生成摘要…")==0);
                lv_obj_del(revisited);
                ContentPage other{"其他图片摘要","/b.jpg"};
                refreshImageNarration(screen,other);
                assert(std::strcmp(lv_label_get_text(label),"其他图片摘要")==0);
                narrationBusy=false; // Failed/finished/reset: restore persisted text.
                refreshImageNarration(screen,page);
                lv_obj_update_layout(screen);
                assert(std::strcmp(lv_label_get_text(label),cached.c_str())==0);
                assert(lv_obj_get_height(card)==(scenario==0?24:67));
              }
              if (scenario == 2) {
                narrationBusy=true;
                refreshImageNarration(screen,page);
                assert(std::strcmp(lv_label_get_text(label),page.narration.c_str())==0);
                narrationBusy=false;
                tap(120,440); tap(120,440);
                assert(pendingNarrationPageId == 0); // Offscreen neighbour.
                lv_obj_set_x(parent,0); // Settled sliding pane is now visible.
                lv_obj_update_layout(screen);
                tap(120,440); tap(120,440);
                assert(pendingNarrationPageId == 7);
                pendingNarrationPageId=0;
                lv_obj_del(card); // Rebuilt page must not leave a stale target.
                tap(120,440); tap(120,440);
                assert(pendingNarrationPageId == 0);
                lv_obj_del(screen);
                return 0;
              } else {
                // Drive the real input callback, not just the gesture helper.
                tap(200,200); tap(200,200);
                assert(pendingNarrationPageId == 0); // Outside the summary.
                tap(40,440); tap(40,440);
                assert(pendingNarrationPageId == 7); // Left icon.
                pendingNarrationPageId=0;
                tap(120,440); tap(120,440);
                assert(pendingNarrationPageId == 7); // Text.
                pendingNarrationPageId=0;
                tap(120,440); tap(200,200); tap(120,440);
                assert(pendingNarrationPageId == 0); // Outside tap breaks pair.
                narrationGesture.reset();
                tap(120,450,120,460); tap(120,450,120,460);
                assert(pendingNarrationPageId == 0); // Released outside.
                tap(120,460,120,450); tap(120,460,120,450);
                assert(pendingNarrationPageId == 0); // Pressed outside.
                narrationGesture.reset();
                tap(120,440); eligiblePage=0; tap(120,440);
                assert(pendingNarrationPageId == 0); // Transition/settings gate.
                eligiblePage=7;
                lv_obj_add_flag(card,LV_OBJ_FLAG_HIDDEN);
                tap(120,440); tap(120,440);
                assert(pendingNarrationPageId == 0);
                lv_obj_clear_flag(card,LV_OBJ_FLAG_HIDDEN);
              }

              appConfig.enabled = false;
              addImageNarration(parent, page);
              assert(!imageNarrationVisible && requests == 0);
              assert(lv_obj_get_child_cnt(parent) == 1);
              appConfig.enabled = true;
              page.narration.clear();
              addImageNarration(parent, page);
              assert(!imageNarrationVisible && requests == 1);
              assert(lv_obj_get_child_cnt(parent) == 1);
              lv_obj_del(screen);
            }
        '''
        cls.executable = directory / "narration-layout"
        compiled = subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
             "-x", "c++", "-", "-x", "none", str(library), "-o", str(cls.executable)],
            input=source, text=True, capture_output=True, timeout=30,
        )
        if compiled.returncode:
            raise AssertionError(compiled.stderr)

    def run_layout(self, scenario):
        ran = subprocess.run([str(self.executable), str(scenario)],
                             text=True, capture_output=True, timeout=10)
        self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_short_summary_has_equal_visible_left_and_bottom_insets(self):
        self.run_layout(0)

    def test_64_character_summary_fits_without_extra_bottom_space(self):
        self.run_layout(1)

    def test_sliding_page_uses_the_same_narration_insets(self):
        self.run_layout(2)

    def test_misans_repertoire_matches_renderer_and_clock_fits(self):
        # Exercise actual LVGL lookups: sparse-map holes must never resolve to
        # a different Han glyph, and excluded scripts must remain unsupported.
        source = r'''
            #include <cassert>
            #include <initializer_list>
            #include "ui_font.h"
            int main() {
              lv_init();
              unsigned supported = 0;
              for (uint32_t cp = 32; cp <= 0xFFFF; ++cp) {
                lv_font_glyph_dsc_t glyph{};
                bool rendered = lv_font_get_glyph_dsc_fmt_txt(
                    &ui_font_misans_16, &glyph, cp, 0);
                assert(rendered == uiFontMiSansSupportsCodePoint(cp));
                supported += rendered;
                if (cp <= 126) assert(rendered);
                if ((cp >= 0x370 && cp <= 0x52F) ||
                    (cp >= 0x3040 && cp <= 0x30FF) ||
                    (cp >= 0xAC00 && cp <= 0xD7AF)) assert(!rendered);
              }
              assert(supported == 6976);
              lv_font_glyph_dsc_t glyph{};
              assert(!lv_font_get_glyph_dsc_fmt_txt(
                  &ui_font_misans_16, &glyph, 0x1F600, 0));
              const lv_font_t* fonts[] = {
                &ui_font_misans_14, &ui_font_misans_16, &ui_font_misans_20,
                &ui_font_misans_24, &ui_font_misans_28, &ui_font_misans_32,
                &ui_font_misans_40, &ui_font_misans_48
              };
              for (auto font : fonts) {
                assert(font->fallback == nullptr);
                for (uint32_t cp = 32; cp <= 126; ++cp)
                  assert(lv_font_get_glyph_dsc_fmt_txt(font, &glyph, cp, 0));
              }
              lv_point_t extent;
              lv_txt_get_size(&extent, "23:59", &ui_font_misans_48,
                              0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
              assert(extent.x <= 184 && extent.y <= 56);
              lv_txt_get_size(&extent, "2026-09-10", &ui_font_misans_24,
                              0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
              assert(extent.x <= 184 && extent.y <= 28);

              // Mixed Chinese/English/emoji resolves to MiSans for text and
              // Noto Emoji for symbols, with no legacy typeface in the chain.
              assert(ui_font_emoji_32.fallback == &ui_font_misans_16);
              for (uint32_t cp : {0x41u, 0x39u, 0x4E2Du, 0x6587u, 0xB0u}) {
                assert(lv_font_get_glyph_dsc(&ui_font_emoji_32, &glyph, cp, 0));
                assert(!glyph.is_placeholder);
                assert(glyph.resolved_font == &ui_font_misans_16);
              }
              for (uint32_t cp : {0x1F600u, 0x1F680u, 0x2764u, 0x20E3u}) {
                assert(lv_font_get_glyph_dsc(&ui_font_emoji_32, &glyph, cp, 0));
                assert(!glyph.is_placeholder && glyph.box_w > 0);
                assert(glyph.resolved_font == &ui_font_emoji_32);
              }
              unsigned emojiCount = 0;
              for (uint32_t cp = 32; cp <= 0x1FAFF; ++cp)
                emojiCount += lv_font_get_glyph_dsc_fmt_txt(
                    &ui_font_emoji_32, &glyph, cp, 0);
              assert(emojiCount == 1413);
              for (uint32_t cp : {0x3B1u, 0x3042u, 0xD55Cu, 0x1FAF9u}) {
                assert(!lv_font_get_glyph_dsc_fmt_txt(
                    &ui_font_emoji_32, &glyph, cp, 0));
                assert(!uiFontMiSansSupportsCodePoint(cp));
              }
            }
        '''
        executable = Path(self.directory.name) / "misans-repertoire"
        compiled = subprocess.run(
            ["c++", "-std=c++17", *self.flags, "-x", "c++", "-",
             str(ROOT / "src/ui_font_support.cpp"), "-x", "none",
             str(self.library), "-o", str(executable)],
            input=source, text=True, capture_output=True, timeout=30,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        ran = subprocess.run([str(executable)], text=True, capture_output=True,
                             timeout=10)
        self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == "__main__":
    unittest.main()
