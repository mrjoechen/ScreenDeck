// Real-browser checks against the embedded controller, with local API fixtures.
// No requests reach the physical device or an LLM provider.
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync } from "node:fs";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

const chrome = [
  process.env.CHROME_BIN,
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/usr/bin/google-chrome",
  "/usr/bin/chromium",
].find(candidate => candidate && existsSync(candidate));
if (!chrome) throw new Error("Set CHROME_BIN to run the prompt browser tests");

const source = await readFile(new URL("../../include/web_ui.h", import.meta.url), "utf8");
const html = source.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const fixture = {
  ok: true, mode: "online", firmwareVersion: "test", firmwareBuildTime: "test",
  firmwareChannel: "development", ssid: "Local test", ip: "127.0.0.1",
  url: "http://127.0.0.1/", rssi: -40, brightness: 80, language: "zh",
  pageTransition: "fade", timezoneOffsetMinutes: 480, epoch: 1788602400,
  showDateTime: false, showWeather: false, screenOffEnabled: false,
  screenOffStartMinutes: 1320, screenOffEndMinutes: 420,
  llmBaseUrl: "https://example.invalid/v1", llmModel: "vision-test",
  llmNarrationPrompt: "中".repeat(2048), llmApiKeyConfigured: true,
  llmConfigured: true, imageNarrationEnabled: true, llmSettingsToken: "local-test",
  llmConfigRevision: 1, storageTotal: 12000000, storageUsed: 1000, pageCount: 0,
  sdMounted: false, sdStatus: "missing", sdFilesystem: "unknown",
  sdSupportedFilesystems: "FAT16 / FAT32", sdTotal: 0, sdUsed: 0, sdClockHz: 0,
};
const checks = `<pre id="prompt-test-results">RUNNING</pre><script>
addEventListener("load", async () => {
  const results = document.getElementById("prompt-test-results");
  const assert = (condition, message) => { if (!condition) throw new Error(message); };
  const input = document.getElementById("llmNarrationPrompt");
  const counter = document.getElementById("llmNarrationPromptCount");
  const edit = value => { input.value = value; input.dispatchEvent(new Event("input", {bubbles:true})); };
  try {
    await loadStatus();
    const form = document.getElementById("llmSettingsForm");
    form.closest(".view").classList.add("active");
    assert(form.querySelector('[data-i18n="llmTitle"]').textContent === "AI 图片摘要", "Chinese AI summary title");
    assert(form.querySelector('[data-i18n="llmBody"]').textContent.includes("OpenAI 兼容 API"), "Explain compatible API configuration");
    const checkSpacing = () => {
      for (const selector of ['[data-i18n="llmBody"]', '#llmNarrationPromptHint', '.switch-copy span', '[data-i18n="narrationGestureHint"]']) {
        const style = getComputedStyle(form.querySelector(selector));
        assert(parseFloat(style.lineHeight) / parseFloat(style.fontSize) >= 1.7, "Readable description line height: " + selector);
      }
      assert(getComputedStyle(form.querySelector('.switch-copy span')).display === "block", "Switch descriptions occupy their own line");
      const hint = form.querySelector('[data-i18n="narrationGestureHint"]');
      const actions = form.querySelector('.inline-actions');
      assert(hint.getBoundingClientRect().top - actions.getBoundingClientRect().bottom >= 18, "Space between buttons and footer description");
      assert(parseFloat(getComputedStyle(form.querySelector('.field-grid')).rowGap) >= 20, "Space between configuration fields");
      assert(form.scrollWidth <= form.clientWidth, "No horizontal form overflow");
    };
    checkSpacing();
    assert(input.value === "中".repeat(2048), "Saved prompt must hydrate without truncation");
    assert(counter.textContent === "2048 / 2048 字符", "Initial count must reflect saved prompt");
    assert(input.maxLength >= 4096, "Native UTF-16 limit must allow 2048 astral characters");
    edit("🌌".repeat(2048));
    assert(input.checkValidity(), "Exactly 2048 code points must be accepted");
    assert(counter.textContent === "2048 / 2048 字符", "Astral code points count once");
    edit("中".repeat(2049));
    assert(!input.checkValidity(), "2049 code points must be rejected");
    assert(input.validationMessage.includes("2048"), "Validation must explain the prompt limit");
    assert(input.getAttribute("aria-invalid") === "true", "Overlong input must expose invalid state");
    document.getElementById("llmSettingsForm").dispatchEvent(new Event("submit", {bubbles:true,cancelable:true}));
    document.getElementById("testLlmSettings").click();
    assert((await (await fetch("/__requests")).json()).posts === 0, "Invalid prompts must not save or invoke the paid test API");
    applyLanguage("en");
    assert(form.querySelector('[data-i18n="llmTitle"]').textContent === "AI image summaries", "English AI summary title");
    assert(form.querySelector('[data-i18n="llmBody"]').textContent.includes("OpenAI-compatible API"), "English compatible API instructions");
    checkSpacing();
    assert(counter.textContent === "2049 / 2048 characters", "Counter must follow UI language");
    assert(input.validationMessage.includes("characters"), "Validation must follow UI language");
    edit("A中🌌");
    assert(counter.textContent === "3 / 2048 characters", "Mixed-script count must match firmware");
    assert(input.checkValidity(), "Shortening must clear the validation error");
    assert(input.getAttribute("aria-invalid") === "false", "Correction must clear invalid state");
    await loadStatus();
    assert(input.value === "A中🌌", "Status refresh must preserve unsaved prompt edits");
    assert(counter.textContent === "3 / 2048 字符", "Language refresh must preserve the edited count");
    hydrateLlmSettings();
    assert(input.value === "中".repeat(2048), "Explicit reload must restore the full saved prompt");
    assert(counter.textContent === "2048 / 2048 字符", "Reloaded count must be current");
    results.textContent = "PASS: bilingual AI summary copy and spacing; prompt hydration, Unicode limits, counters, validation, save/test guards, language and dirty-form preservation";
  } catch (error) { results.textContent = "FAIL: " + error.message; }
});
</script>`;

let posts = 0;
const server = createServer((request, response) => {
  if (request.method !== "GET") posts++;
  if (request.url === "/") {
    response.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
    response.end(html.replace("</body>", checks + "</body>"));
    return;
  }
  response.writeHead(200, { "Content-Type": "application/json" });
  response.end(JSON.stringify(request.url === "/api/status" ? fixture :
    request.url === "/__requests" ? { posts } : { ok: true, pages: [] }));
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const profile = await mkdtemp(join(tmpdir(), "screendeck-prompt-"));
let stdout = "", stderr = "";
try {
  const child = spawn(chrome, [
    "--headless=new", "--disable-gpu", "--disable-dev-shm-usage",
    "--disable-background-networking", "--disable-component-update", "--disable-sync",
    "--metrics-recording-only", "--no-first-run", "--no-default-browser-check",
    `--window-size=${process.env.TEST_WINDOW_SIZE || "1280,1000"}`,
    `--user-data-dir=${profile}`, "--virtual-time-budget=10000", "--dump-dom",
    `http://127.0.0.1:${server.address().port}/`,
  ], { stdio: ["ignore", "pipe", "pipe"] });
  // On some macOS hosts Chrome finishes --dump-dom but hangs in display-link
  // teardown. End our disposable process once the complete test result arrives,
  // rather than mistaking a shutdown timeout for a page assertion failure.
  let stoppedAfterResult = false;
  child.stdout.on("data", data => {
    stdout += data;
    if (/<pre id="prompt-test-results">(?:PASS|FAIL):[^<]*<\/pre>/.test(stdout)) {
      stoppedAfterResult = child.kill("SIGTERM") || stoppedAfterResult;
    }
  });
  child.stderr.on("data", data => stderr += data);
  const timeout = setTimeout(() => child.kill("SIGKILL"), 20000);
  const code = await new Promise((resolve, reject) => {
    child.on("error", reject);
    child.on("close", resolve);
  }).finally(() => clearTimeout(timeout));
  const result = stdout.match(/<pre id="prompt-test-results">([^<]*)<\/pre>/)?.[1];
  if ((code !== 0 && !stoppedAfterResult) || !result?.startsWith("PASS:") || posts !== 0) {
    throw new Error(`${result || "No browser result"}; exit=${code}; posts=${posts}; ${stderr.slice(-1500)}`);
  }
  console.log(result);
} finally {
  await new Promise(resolve => server.close(resolve));
  await rm(profile, { recursive: true, force: true });
}
