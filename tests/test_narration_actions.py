"""Behavior tests for narration gestures, persistent reset and web confirmation."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_llm_narration_guards import function_body

ROOT = Path(__file__).resolve().parents[1]


class NarrationActionTests(unittest.TestCase):
    def cpp(self, source):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "test"
            compiled = subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I",
                 str(ROOT / "include"), "-x", "c++", "-", "-o", str(executable)],
                input=source, text=True, capture_output=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)

    def body(self, filename, signature):
        source = (ROOT / filename).read_text()
        self.assertTrue(signature in source, f"Missing implementation: {signature}")
        return function_body(source, signature)

    def test_double_tap_requires_same_image_short_nearby_taps(self):
        self.assertTrue((ROOT / "include/image_narration_gesture.h").exists(),
                        "Image narration double-tap recognition is missing")
        self.cpp(r'''
            #include <cassert>
            #include "image_narration_gesture.h"
            int main() {
              ImageNarrationGesture taps;
              assert(!taps.release(7, 1000, 100, 0, 0, 200, 200));
              assert(taps.release(7, 1300, 100, 2, 2, 204, 204));
              assert(!taps.release(7, 1500, 100, 0, 0, 200, 200));
              assert(!taps.release(8, 1700, 100, 0, 0, 200, 200));
              assert(!taps.release(8, 2400, 100, 0, 0, 200, 200));
              assert(!taps.release(8, 2600, 100, 100, 0, 300, 200));
              assert(!taps.release(8, 2800, 100, 0, 0, 200, 200));
              assert(!taps.release(8, 3000, 501, 0, 0, 200, 200));
              assert(!taps.release(8, 3200, 100, 0, 0, 200, 200));
              assert(!taps.release(0, 3400, 100, 0, 0, 200, 200));
              assert(!taps.release(8, 3500, 100, 0, 0, 200, 200));
              assert(!taps.release(8, 3600, 100, 0, 0, 400, 400));
              assert(taps.release(8, 3800, 100, 0, 0, 403, 401));
              assert(!taps.release(8, 0xFFFFFF00U, 100, 0, 0, 200, 200));
              assert(taps.release(8, 100, 100, 0, 0, 200, 200));
            }
        ''')

    def test_image_busy_status_covers_queue_worker_and_unsaved_result(self):
        body = self.body("src/llm_narration.cpp", "bool llmNarrationBusyForImage(")
        self.cpp(r'''
            #include <cassert>
            #include <cstring>
            #include <string>
            struct String:std::string { using std::string::string;
              bool isEmpty() const { return empty(); } };
            #define portENTER_CRITICAL(x) ((void)0)
            #define portEXIT_CRITICAL(x) ((void)0)
            struct Job { char imagePath[32]="/a.jpg"; };
            Job queued, pendingResult;
            Job* queuedRegenerationJob=nullptr;
            bool fetchRunning=false, activeInvalidated=false, resultPending=false;
            char activeImagePath[32]="/a.jpg";
            bool llmNarrationBusyForImage(const String& imagePath) {''' + body + r'''}
            int main() {
              assert(!llmNarrationBusyForImage("/a.jpg"));
              queuedRegenerationJob=&queued;
              assert(llmNarrationBusyForImage("/a.jpg"));
              assert(!llmNarrationBusyForImage("/b.jpg"));
              queuedRegenerationJob=nullptr;fetchRunning=true;
              assert(llmNarrationBusyForImage("/a.jpg"));
              activeInvalidated=true;
              assert(!llmNarrationBusyForImage("/a.jpg"));
              activeInvalidated=false;fetchRunning=false;resultPending=true;
              assert(llmNarrationBusyForImage("/a.jpg"));
              resultPending=false;
              assert(!llmNarrationBusyForImage("/a.jpg"));
              assert(!llmNarrationBusyForImage(""));
            }
        ''')

    def test_regeneration_refreshes_status_only_after_acceptance_and_ignores_busy_taps(self):
        body = self.body("src/display_ui.cpp", "if (pendingNarrationPageId != 0)")
        self.cpp(r'''
            #include <cassert>
            #include <cstdint>
            #include <string>
            struct ContentPage { std::string imagePath="/a.jpg"; } page;
            struct { const ContentPage& page(int) { return ::page; } } appConfig;
            struct { void println(const char*) {} } Serial;
            uint32_t pendingNarrationPageId=7, eligible=7;
            int currentPage=1, requests=0, refreshes=0;
            bool busy=false, accepted=true;
            uint32_t narrationGesturePageId() { return eligible; }
            bool llmNarrationBusyForImage(const std::string&) { return busy; }
            bool requestImageNarration(const ContentPage&,bool regenerate) {
              assert(regenerate); ++requests; return accepted;
            }
            void* lv_scr_act() { return nullptr; }
            void refreshImageNarration(void*,const ContentPage&) { ++refreshes; }
            void dispatch() {''' + body + r'''}
            int main() {
              dispatch(); assert(requests==1 && refreshes==1 && pendingNarrationPageId==0);
              pendingNarrationPageId=7; busy=true;
              dispatch(); assert(requests==1 && refreshes==1);
              pendingNarrationPageId=7; busy=false; accepted=false;
              dispatch(); assert(requests==2 && refreshes==1);
              pendingNarrationPageId=7; eligible=8;
              dispatch(); assert(requests==2 && refreshes==1);
            }
        ''')

    def test_reset_persists_all_image_entries_and_rolls_back_on_failure(self):
        body = self.body("src/app_config.cpp", "bool AppConfig::clearImageNarrations()")
        self.cpp(r'''
            #include <cassert>
            #include <string>
            using String = std::string;
            constexpr size_t MAX_CONTENT_PAGES = 16;
            enum class PageType { Text, Image };
            struct ContentPage { PageType type; String imagePath; String narration; };
            class AppConfig {
             public:
              ContentPage pages_[MAX_CONTENT_PAGES]; size_t pageCount_ = 4;
              bool writable = true; int writes = 0;
              ContentPage disk[MAX_CONTENT_PAGES];
              bool save() { ++writes; if (!writable) return false;
                for (size_t i=0; i<pageCount_; ++i) disk[i]=pages_[i]; return true; }
              bool clearImageNarrations();
            };
            bool AppConfig::clearImageNarrations() {''' + body + r'''}
            int main() {
              AppConfig c;
              c.pages_[0] = {PageType::Image, "/a.jpg", "first"};
              c.pages_[1] = {PageType::Image, "/a.jpg", "first"};
              c.pages_[2] = {PageType::Image, "/b.gif", "second"};
              c.pages_[3] = {PageType::Text, "", "untouched"};
              c.save(); c.writable = false;
              assert(!c.clearImageNarrations());
              assert(c.pages_[0].narration == "first");
              assert(c.pages_[1].narration == "first");
              assert(c.pages_[2].narration == "second");
              assert(c.disk[2].narration == "second");
              c.writable = true; assert(c.clearImageNarrations());
              for (int i=0; i<3; ++i) {
                assert(c.pages_[i].narration.empty());
                assert(c.disk[i].narration.empty());
              }
              assert(c.pages_[0].imagePath == "/a.jpg");
              assert(c.pages_[2].type == PageType::Image);
              assert(c.pages_[3].narration == "untouched");
              assert(c.pageCount_ == 4);
              assert(c.clearImageNarrations());
            }
        ''')

    def test_invalidated_worker_cannot_restore_reset_or_superseded_summary(self):
        invalidate = self.body("src/llm_narration.cpp",
                               "void invalidateNarrationsLocked(const char* imagePath)")
        finish = self.body("src/llm_narration.cpp", "void finishJob(")
        self.cpp(r'''
            #include <cassert>
            #include <cstdint>
            #include <cstring>
            using std::strcmp;
            #define portENTER_CRITICAL(x) ((void)0)
            #define portEXIT_CRITICAL(x) ((void)0)
            uint32_t millis() { return 1000; }
            struct NarrationResult { uint32_t pageId=0; char imagePath[512]="";
              char narration[257]=""; };
            struct FailureSlot { uint32_t pathHash=0; uint32_t retryAt=0; };
            FailureSlot failures[16];
            bool fetchRunning=true, resultPending=false, activeInvalidated=false;
            uint32_t resultRetryAt=0;
            bool activeRegeneration=false;
            uint32_t activePathHash=42;
            char activeImagePath[512]="/a.jpg";
            NarrationResult pendingResult;
            int remembered=0, forgotten=0;
            uint32_t hashPath(const char*) { return 42; }
            void rememberFailure(uint32_t, uint32_t) { ++remembered; }
            void forgetFailure(uint32_t) { ++forgotten; }
            void invalidateNarrationsLocked(const char* imagePath) {''' + invalidate + r'''}
            void finishJob(const NarrationResult& result, uint32_t pathHash, bool ok) {''' + finish + r'''}
            int main() {
              NarrationResult result; result.pageId=7;
              strcpy(result.imagePath, "/a.jpg"); strcpy(result.narration, "new");
              pendingResult=result; resultPending=true;
              invalidateNarrationsLocked("/b.jpg");
              assert(!activeInvalidated && resultPending && fetchRunning);
              invalidateNarrationsLocked("/a.jpg");
              assert(activeInvalidated && !resultPending && fetchRunning);
              finishJob(result, 42, true);
              assert(!resultPending && !fetchRunning);
              strcpy(activeImagePath, "/a.jpg"); activePathHash=42;
              fetchRunning=true; activeInvalidated=false;
              failures[0]={42, 50000};
              invalidateNarrationsLocked(nullptr);
              assert(fetchRunning && activeInvalidated && failures[0].pathHash==0);
              finishJob(result, 42, false);
              assert(remembered==0 && !resultPending);
              strcpy(activeImagePath, "/a.jpg"); activePathHash=42;
              fetchRunning=true; activeInvalidated=false;
              finishJob(result, 42, true);
              assert(resultPending && pendingResult.pageId==7);
              assert(strcmp(pendingResult.narration, "new")==0);
            }
        ''')

    def test_web_reset_cancellation_confirmation_busy_and_error(self):
        body = self.body("include/web_ui.h", '$("#resetAllNarrations").onclick=async')
        node = shutil.which("node")
        self.assertIsNotNone(node, "Node is required for web behavior tests")
        script = r'''
            const assert=require("node:assert/strict");
            let confirmed=false, confirmations=0, calls=[], busy=false, toasts=[], loads=0;
            let fail=false, release;
            const button={disabled:false};
            const $=()=>button;
            const device={llmSettingsToken:"device-csrf"};
            const t=key=>key;
            const confirm=message=>{assert.equal(message,"resetNarrationsConfirm");++confirmations;return confirmed};
            const setLlmFormBusy=value=>{busy=value;button.disabled=value};
            const toast=(...args)=>toasts.push(args);
            const loadPages=async()=>{++loads};
            const api=async(url,options)=>{
              assert.equal(url,"/api/llm/narration/reset");
              assert.equal(options.method,"POST");
              assert.equal(options.body.get("csrfToken"),"device-csrf");
              assert.equal(options.body.get("confirmed"),"1");
              calls.push(url); if(fail)throw new Error("disk full");
              await new Promise(resolve=>release=resolve);
            };
            const handler=async()=>{''' + body + r'''};
            (async()=>{
              await handler(); assert.equal(calls.length,0); assert.equal(busy,false);
              confirmed=true;
              const pending=handler(); assert.equal(busy,true); assert.equal(calls.length,1);
              await handler(); assert.equal(calls.length,1); assert.equal(confirmations,2);
              release(); await pending;
              assert.equal(loads,1); assert.equal(busy,false);
              assert.deepEqual(toasts.pop(),["narrationsReset"]);
              fail=true; await handler(); assert.equal(busy,false); assert.equal(loads,1);
              assert.deepEqual(toasts.pop(),["disk full",true]);
            })().catch(error=>{console.error(error);process.exitCode=1});
        '''
        ran = subprocess.run([node, "-"], input=script, text=True, capture_output=True)
        self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_reset_route_requires_csrf_and_confirmation_before_mutation(self):
        body = self.body("src/web_portal.cpp",
                         'server.on("/api/llm/narration/reset", HTTP_POST, []()')
        self.cpp(r'''
            #include <cassert>
            #include <map>
            #include <string>
            using String=std::string;
            struct { std::map<String,String> args;
              String arg(const char* key) { return args[key]; } } server;
            String llmSettingsToken="csrf";
            bool paused=false, writable=true;
            int writes=0, invalidations=0, dirty=0, status=0;
            struct { bool clearImageNarrations() {
              assert(paused); ++writes; return writable;
            } } appConfig;
            void displayBeginStorageWrite(bool blank) { assert(!blank); paused=true; }
            void displayEndStorageWrite() { paused=false; }
            void llmNarrationInvalidateAll() { assert(paused); ++invalidations; }
            void displayMarkContentDirty() { ++dirty; }
            void sendLocalizedError(int code, const char*, const char*) { status=code; }
            void sendOk() { status=200; }
            void route() {''' + body + r'''}
            int main() {
              route(); assert(status==403 && writes==0);
              server.args["csrfToken"]="csrf"; route();
              assert(status==400 && writes==0);
              server.args["confirmed"]="0"; route(); assert(status==400 && writes==0);
              server.args["confirmed"]="1"; writable=false; route();
              assert(status==500 && writes==1 && invalidations==0 && dirty==0 && !paused);
              writable=true; route();
              assert(status==200 && writes==2 && invalidations==1 && dirty==1 && !paused);
            }
        ''')

    def test_manual_queue_supersedes_same_image_and_deduplicates_pending_work(self):
        body = self.body("src/llm_narration.cpp", "bool queueRegenerationJob(NarrationJob* job)")
        self.cpp(r'''
            #include <cassert>
            #include <cstring>
            #define portENTER_CRITICAL(x) ((void)0)
            #define portEXIT_CRITICAL(x) ((void)0)
            struct NarrationJob { char imagePath[512]; };
            NarrationJob* queuedRegenerationJob=nullptr;
            bool activeRegeneration=false;
            char activeImagePath[512]="/a.jpg";
            int invalidations=0, disposals=0;
            void invalidateNarrationsLocked(const char* target) {
              assert(strcmp(target, "/a.jpg")==0); ++invalidations;
            }
            void disposeNarrationJob(NarrationJob*) { ++disposals; }
            bool queueRegenerationJob(NarrationJob* job) {''' + body + r'''}
            int main() {
              NarrationJob a; strcpy(a.imagePath, "/a.jpg");
              assert(queueRegenerationJob(&a));
              assert(queuedRegenerationJob==&a && invalidations==1);
              assert(!queueRegenerationJob(&a));
              assert(invalidations==1 && disposals==1);
              queuedRegenerationJob=nullptr; activeRegeneration=true;
              assert(!queueRegenerationJob(&a));
              assert(invalidations==1 && disposals==2);
            }
        ''')

    def test_only_settled_enabled_image_pages_accept_regeneration_gestures(self):
        body = self.body("src/display_ui.cpp", "uint32_t narrationGesturePageId()")
        self.cpp(r'''
            #include <cassert>
            #include <cstdint>
            #include <cstddef>
            #include <initializer_list>
            bool provisioningScreen=false, deviceSettingsScreen=false;
            bool scheduledScreenOff=false, storageWriteActive=false, contentDirty=false;
            int pendingSwipe=0;
            enum class PageTransitionPhase { Idle, Sliding, FadingIn };
            auto pageTransitionPhase=PageTransitionPhase::Idle;
            enum class ScreenRequest { None, DeviceSettings };
            auto pendingScreenRequest=ScreenRequest::None;
            size_t currentPage=1;
            enum class PageType { Text, Image };
            struct ContentPage { uint32_t id; PageType type; };
            struct {
              bool enabled=true, configured=true;
              ContentPage pages[2]={{7,PageType::Image},{8,PageType::Text}};
              size_t pageCount() { return 2; }
              const ContentPage& page(size_t i) { return pages[i]; }
              bool imageNarrationEnabled() { return enabled; }
              bool llmConfigured() { return configured; }
            } appConfig;
            uint32_t narrationGesturePageId() {''' + body + r'''}
            int main() {
              assert(narrationGesturePageId()==7);
              appConfig.enabled=false; assert(!narrationGesturePageId()); appConfig.enabled=true;
              appConfig.configured=false; assert(!narrationGesturePageId()); appConfig.configured=true;
              for (bool* blocked : {&provisioningScreen, &deviceSettingsScreen,
                                    &scheduledScreenOff, &storageWriteActive, &contentDirty}) {
                *blocked=true; assert(!narrationGesturePageId()); *blocked=false;
              }
              currentPage=0; assert(!narrationGesturePageId());
              currentPage=2; assert(!narrationGesturePageId());
              currentPage=3; assert(!narrationGesturePageId()); currentPage=1;
              pageTransitionPhase=PageTransitionPhase::Sliding; assert(!narrationGesturePageId());
              pageTransitionPhase=PageTransitionPhase::FadingIn; assert(!narrationGesturePageId());
              pageTransitionPhase=PageTransitionPhase::Idle;
              pendingSwipe=1; assert(!narrationGesturePageId()); pendingSwipe=0;
              pendingScreenRequest=ScreenRequest::DeviceSettings; assert(!narrationGesturePageId());
            }
        ''')

    def test_successful_result_survives_local_save_failure_without_new_http(self):
        take = self.body("src/llm_narration.cpp", "bool llmNarrationTakeResult(")
        self.cpp(r'''
            #include <cassert>
            #include <cstdint>
            #include <string>
            using String=std::string;
            #define portENTER_CRITICAL(x) ((void)0)
            #define portEXIT_CRITICAL(x) ((void)0)
            struct NarrationResult { uint32_t pageId=7; char imagePath[512]="/a.jpg";
              char narration[257]="already paid for"; };
            NarrationResult pendingResult;
            bool resultPending=true;
            uint32_t now=1000, resultRetryAt=0;
            [[maybe_unused]] constexpr uint32_t RESULT_SAVE_RETRY_MS=5000;
            uint32_t millis() { return now; }
            bool llmNarrationTakeResult(uint32_t& pageId, String& imagePath, String& narration) {''' + take + r'''}
            int main() {
              uint32_t id=0; String path, text;
              assert(llmNarrationTakeResult(id,path,text));
              assert(id==7 && path=="/a.jpg" && text=="already paid for");
              // No acknowledgement: flash save failed. Keep the result and
              // block automatic HTTP scheduling, then retry only persistence.
              assert(resultPending);
              now=1100; assert(!llmNarrationTakeResult(id,path,text));
              now=7000; assert(llmNarrationTakeResult(id,path,text));
              assert(resultPending && text=="already paid for");
            }
        ''')

    def test_save_retry_target_handles_deleted_duplicates_and_reused_ids(self):
        body = self.body("src/app_config.cpp", "bool AppConfig::imageNarrationTargetExists(")
        self.cpp(r'''
            #include <cassert>
            #include <string>
            struct String:std::string { using std::string::string;
              bool isEmpty() const { return empty(); } };
            enum class PageType { Image, Text };
            struct ContentPage { unsigned id; PageType type; String imagePath; };
            class AppConfig {
             public:
              ContentPage pages_[2]={{7,PageType::Image,"/a.jpg"},{8,PageType::Image,"/b.jpg"}};
              size_t pageCount_=2;
              int findPage(unsigned id) const {
                for(size_t i=0;i<pageCount_;++i) if(pages_[i].id==id)return i;
                return -1;
              }
              bool imageNarrationTargetExists(uint32_t pageId,const String& expectedPath) const;
            };
            bool AppConfig::imageNarrationTargetExists(uint32_t pageId,const String& expectedPath) const {''' + body + r'''}
            int main() {
              AppConfig c;
              assert(c.imageNarrationTargetExists(7,"/a.jpg"));
              assert(!c.imageNarrationTargetExists(7,"/b.jpg"));
              assert(!c.imageNarrationTargetExists(7,""));
              assert(c.imageNarrationTargetExists(99,"/a.jpg"));
              assert(!c.imageNarrationTargetExists(99,"/missing.jpg"));
              c.pages_[0].type=PageType::Text;
              assert(!c.imageNarrationTargetExists(7,"/a.jpg"));
            }
        ''')


if __name__ == "__main__":
    unittest.main()
