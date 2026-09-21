"""Host behavior tests for long prompts, JSON capacity and Unicode UI counts."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_llm_narration_guards import function_body

ROOT = Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "src/app_config.cpp").read_text()
WEB = (ROOT / "include/web_ui.h").read_text()
PORTAL = (ROOT / "src/web_portal.cpp").read_text()
CONSTANTS = "\n".join(re.findall(
    r"constexpr size_t MAX_LLM_NARRATION_PROMPT_\w+\s*=[^;]+;",
    (ROOT / "include/app_config.h").read_text(),
)).replace("constexpr", "[[maybe_unused]] constexpr")


class LlmPromptLimitTests(unittest.TestCase):
    def cpp(self, source):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "prompt-test"
            compiled = subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I",
                 str(ROOT / ".pio/libdeps/esp32s3/ArduinoJson/src"),
                 "-x", "c++", "-", "-o", str(executable)],
                input=source, text=True, capture_output=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(executable)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_firmware_accepts_2048_unicode_characters_but_rejects_2049(self):
        decoder = function_body(CONFIG, "bool decodeUtf8CodePoint(")
        validator = function_body(CONFIG, "bool validLlmNarrationPrompt(")
        self.cpp(r'''
            #include <cassert>
            #include <cstdint>
            #include <string>
            #include <initializer_list>
            struct String:std::string {
              using std::string::string;
              bool isEmpty() const { return empty(); }
            };
        ''' + CONSTANTS + r'''
            bool decodeUtf8CodePoint(const String& value,size_t& offset,uint32_t& codePoint) {
        ''' + decoder + r'''}
            bool validLlmNarrationPrompt(const String& value) {
        ''' + validator + r'''}
            int main() {
              for (const char* character : {"A", u8"中", u8"🌌"}) {
                String prompt;
                for(int i=0;i<2048;++i) prompt+=character;
                assert(validLlmNarrationPrompt(prompt));
                assert(prompt.size()+1<=MAX_LLM_NARRATION_PROMPT_BYTES);
                prompt+=character;
                assert(!validLlmNarrationPrompt(prompt));
              }
              assert(!validLlmNarrationPrompt(""));
              assert(!validLlmNarrationPrompt(String("a\0b",3)));
              assert(!validLlmNarrationPrompt("\xc0\xaf"));
              assert(!validLlmNarrationPrompt("\xed\xa0\x80"));
              assert(!validLlmNarrationPrompt("\xf4\x90\x80\x80"));
              assert(!validLlmNarrationPrompt("\xe4\xb8"));
              assert(validLlmNarrationPrompt("准确描述主体\n不要抒情"));
            }
        ''')

    def test_status_json_keeps_long_prompt_and_following_fields(self):
        status = function_body(PORTAL, 'server.on("/api/status"')
        allocation = re.search(r"DynamicJsonDocument doc\([^;]+;", status).group()
        self.cpp(r'''
            #include <ArduinoJson.h>
            #include <cassert>
            #include <string>
        ''' + CONSTANTS + r'''
            int main() {
              std::string prompt;
              for(int i=0;i<2048;++i) prompt+=u8"🌌";
        ''' + allocation + r'''
              doc["ok"]=true;
              doc["llmBaseUrl"]=std::string(383,'u');
              doc["llmModel"]=std::string(127,'m');
              doc["llmNarrationPrompt"]=prompt;
              doc["llmConfigured"]=true;
              doc["llmSettingsToken"]=std::string(16,'c');
              doc["pageCount"]=16;
              assert(!doc.overflowed());
              std::string body; serializeJson(doc,body);
              DynamicJsonDocument parsed(16384);
              assert(!deserializeJson(parsed,body));
              assert(parsed["llmNarrationPrompt"].as<std::string>()==prompt);
              assert(parsed["llmConfigured"].as<bool>());
              assert(parsed["pageCount"].as<int>()==16);
            }
        ''')

    def test_long_prompt_survives_config_json_round_trip_with_full_playlist(self):
        save = function_body(CONFIG, "bool AppConfig::save() const")
        load = function_body(CONFIG, "bool AppConfig::load()")
        save_allocation = re.search(r"DynamicJsonDocument doc\([^;]+;", save).group()
        load_allocation = re.search(r"DynamicJsonDocument doc\([^;]+;", load).group().replace("doc(", "loaded(")
        self.cpp(r'''
            #include <ArduinoJson.h>
            #include <cassert>
            #include <string>
        ''' + CONSTANTS + r'''
            int main() {
              std::string prompt;
              for(int i=0;i<2048;++i) prompt+=u8"🌌";
        ''' + save_allocation + r'''
              doc["llmNarrationPrompt"]=prompt;
              doc["llmBaseUrl"]=std::string(383,'u');
              doc["llmApiKey"]=std::string(511,'k');
              doc["llmModel"]=std::string(127,'m');
              JsonArray pages=doc.createNestedArray("pages");
              for(int i=0;i<16;++i) {
                JsonObject page=pages.createNestedObject();
                page["id"]=i+1; page["type"]="text";
                page["text"]=std::string(1536,static_cast<char>('A'+i));
                page["path"]="";page["narration"]="";
                page["foreground"]=0xffffff;page["background"]=0;
              }
              assert(!doc.overflowed());
              std::string body; serializeJson(doc,body);
        ''' + load_allocation + r'''
              assert(!deserializeJson(loaded,body));
              assert(loaded["llmNarrationPrompt"].as<std::string>()==prompt);
              assert(loaded["pages"].size()==16);
              assert(loaded["pages"][15]["text"].as<std::string>()==std::string(1536,'P'));
            }
        ''')

    def test_counter_tracks_unicode_hydration_edits_and_validation(self):
        self.assertTrue("function refreshLlmPromptCounter(" in WEB,
                        "Live prompt counter is missing")
        counter = function_body(WEB, "function refreshLlmPromptCounter(")
        hydrate = function_body(WEB, "function hydrateLlmSettings(")
        changed = function_body(WEB, "function markLlmSettingsChanged(")
        translations = WEB[WEB.index("const translations="):WEB.index('const esc=')]
        script = r'''
            const assert=require("node:assert/strict");
            const localStorage={getItem:()=>null};
        ''' + translations + r'''
            const input={value:"",dataset:{maxCodepoints:"2048"},error:"",
              setCustomValidity(value){this.error=value},setAttribute(){}};
            const count={textContent:""};
            const elements={"#llmNarrationPrompt":input,"#llmNarrationPromptCount":count};
            const $=key=>elements[key]||(elements[key]={value:"",checked:false});
            let device={llmNarrationPrompt:"中文🌌A",llmConfigRevision:1};
            let llmFormHydrated=false,llmFormDirty=false,llmHydratedConfigRevision=0;
            const invalidateLlmTestResult=()=>{};
            const refreshLlmConfigState=()=>{};
            function refreshLlmPromptCounter(){''' + counter + r'''}
            function hydrateLlmSettings(){''' + hydrate + r'''}
            function markLlmSettingsChanged(){''' + changed + r'''}
            hydrateLlmSettings();
            assert.match(count.textContent,/4\s*\/\s*2048/);
            assert.equal(llmFormDirty,false);
            input.value="🌌".repeat(2048);markLlmSettingsChanged();
            assert.match(count.textContent,/2048\s*\/\s*2048/);
            assert.equal(input.error,"");assert.equal(llmFormDirty,true);
            input.value="A".repeat(2049);markLlmSettingsChanged();
            assert.match(count.textContent,/2049\s*\/\s*2048/);
            assert.notEqual(input.error,"");assert.equal(input.value.length,2049);
            currentLanguage="en";refreshLlmPromptCounter();
            assert.match(input.error,/2048/);assert.match(count.textContent,/characters/);
            input.value="短提示词";markLlmSettingsChanged();assert.equal(input.error,"");
            assert.match(count.textContent,/4\s*\/\s*2048/);
            device.llmNarrationPrompt="";hydrateLlmSettings();
            assert.match(count.textContent,/0\s*\/\s*2048/);
        '''
        ran = subprocess.run(["node", "-"], input=script, text=True, capture_output=True)
        self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == "__main__":
    unittest.main()
