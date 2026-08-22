import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { mkdtemp, readFile, rm, stat } from "node:fs/promises";
import { existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const testsDirectory = dirname(fileURLToPath(import.meta.url));
const siteDirectory = normalize(join(testsDirectory, "..", "..", "site"));
const layoutFixture = join(testsDirectory, "layout.html");
const chromeCandidates = [
  process.env.CHROME_BIN,
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/usr/bin/google-chrome",
  "/usr/bin/chromium",
].filter(Boolean);
const chrome = chromeCandidates.find((candidate) => existsSync(candidate));

if (!chrome) {
  console.error("找不到 Chrome。请通过 CHROME_BIN 指定可执行文件。");
  process.exit(1);
}

const mimeTypes = new Map([
  [".bin", "application/octet-stream"],
  [".css", "text/css; charset=utf-8"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".svg", "image/svg+xml"],
  [".webp", "image/webp"],
]);

const server = createServer(async (request, response) => {
  try {
    const requestUrl = new URL(request.url, "http://127.0.0.1");
    const relativePath = decodeURIComponent(requestUrl.pathname === "/" ? "/index.html" : requestUrl.pathname);
    const filePath = relativePath === "/__layout-test.html"
      ? layoutFixture
      : normalize(join(siteDirectory, relativePath));

    if (filePath !== layoutFixture && !filePath.startsWith(`${siteDirectory}/`)) {
      response.writeHead(403).end("Forbidden");
      return;
    }

    const fileStat = await stat(filePath);
    if (!fileStat.isFile()) throw new Error("Not a file");
    const headers = {
      "Content-Length": fileStat.size,
      "Content-Type": mimeTypes.get(extname(filePath)) ?? "application/octet-stream",
    };
    response.writeHead(200, headers);
    if (request.method === "HEAD") response.end();
    else response.end(await readFile(filePath));
  } catch {
    response.writeHead(404).end("Not found");
  }
});

await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const address = server.address();
const profileDirectory = await mkdtemp(join(tmpdir(), "screendeck-layout-"));
const testUrl = `http://127.0.0.1:${address.port}/__layout-test.html`;

const chromeArguments = [
  "--headless=new",
  "--disable-gpu",
  "--disable-dev-shm-usage",
  "--disable-background-networking",
  "--disable-component-update",
  "--disable-sync",
  "--metrics-recording-only",
  "--no-first-run",
  "--no-default-browser-check",
  "--font-render-hinting=none",
  `--user-data-dir=${profileDirectory}`,
  "--host-resolver-rules=MAP unpkg.com ~NOTFOUND, MAP fonts.googleapis.com ~NOTFOUND, MAP fonts.gstatic.com ~NOTFOUND",
  "--virtual-time-budget=5000",
  "--window-size=1400,900",
  "--dump-dom",
  testUrl,
];

let stdout = "";
let stderr = "";
let timedOut = false;

try {
  const child = spawn(chrome, chromeArguments, { stdio: ["ignore", "pipe", "pipe"] });
  child.stdout.setEncoding("utf8");
  child.stderr.setEncoding("utf8");
  child.stdout.on("data", (chunk) => { stdout += chunk; });
  child.stderr.on("data", (chunk) => { stderr += chunk; });

  let forceKillTimeout;
  const timeout = setTimeout(() => {
    timedOut = true;
    child.kill("SIGTERM");
    forceKillTimeout = setTimeout(() => child.kill("SIGKILL"), 1000);
  }, 20000);
  const exitCode = await new Promise((resolve) => child.on("close", resolve));
  clearTimeout(timeout);
  clearTimeout(forceKillTimeout);

  const resultMatch = stdout.match(/<pre id="results">([\s\S]*?)<\/pre>/);
  const readableResults = resultMatch?.[1]
    .replaceAll("&amp;", "&")
    .replaceAll("&lt;", "<")
    .replaceAll("&gt;", ">")
    .trim();
  if (readableResults) console.log(readableResults);

  const finalized = stdout.includes('data-status="passed"') || stdout.includes('data-status="failed"');
  const passed = stdout.includes('data-status="passed"');
  if (!finalized || !passed) {
    if (!finalized && timedOut) console.error("布局测试未在时限内完成。");
    else if (!finalized && exitCode !== 0) console.error(`Chrome 异常退出：${exitCode}`);
    if (!finalized && stderr) console.error(stderr.trim());
    process.exitCode = 1;
  }
} finally {
  server.close();
  server.closeAllConnections?.();
  await rm(profileDirectory, { recursive: true, force: true });
}
