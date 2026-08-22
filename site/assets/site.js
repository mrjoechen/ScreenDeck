const installer = document.querySelector("#webInstaller");
const flashButton = document.querySelector("#flashButton");
const confirmInput = document.querySelector("#deviceConfirm");
const confirmedModel = document.querySelector("#confirmedModel");
const deviceList = document.querySelector("#deviceList");
const registryState = document.querySelector("#registryState");
const helper = document.querySelector("#installHelper");
const secureCheck = document.querySelector("#secureCheck");
const serialCheck = document.querySelector("#serialCheck");
const languageToggle = document.querySelector("#languageToggle");
const languageToggleLabel = document.querySelector("#languageToggleLabel");
const githubLink = document.querySelector("#githubLink");
const descriptionMeta = document.querySelector('meta[name="description"]');

const translations = {
  zh: {
    documentTitle: "ScreenDeck · 浏览器刷入固件",
    metaDescription: "ScreenDeck 将 ESP32-S3-4848S040 变成可在局域网管理的 480×480 内容屏，并支持通过浏览器直接刷入固件。",
    skipLink: "跳到主要内容",
    navAria: "主导航",
    homeAria: "ScreenDeck 首页",
    navToolsAria: "页面工具",
    githubAria: "查看 GitHub",
    githubLabel: "GitHub",
    languageControl: "中英文切换",
    languageSwitchToEnglish: "切换到英文",
    languageSwitchToChinese: "切换到中文",
    donateAria: "支持 ScreenDeck",
    donateLabel: "捐赠",
    heroTitleLead: "把方屏，",
    heroTitleAccent: "刷成内容台。",
    heroLede: "ScreenDeck 为一块确定的硬件而写：连接 USB，在浏览器里刷入固件；首次启动扫码配网，此后在局域网直接管理文字、图片与播放顺序。",
    heroCta: "立即刷入",
    heroProofAria: "当前硬件配置",
    supportedNow: "当前已支持",
    brandLabel: "品牌",
    displayLabel: "显示",
    touchLabel: "触控",
    storageLabel: "存储",
    proofNote: "更多型号会以独立固件清单加入，不会让相同芯片的不同屏幕误刷同一镜像。",
    flashPathAria: "刷写链路",
    installTitle: "浏览器刷机台",
    installIntro: "不安装桌面软件。型号确认后，浏览器会让你选择串口并完成芯片识别、擦除确认、写入与校验。",
    releaseAria: "固件发布信息",
    channelLabel: "通道",
    versionLabel: "版本",
    updatedLabel: "更新",
    imageLabel: "镜像",
    channelRelease: "正式版",
    channelDevelopment: "开发版",
    chooseDevice: "选择设备",
    registryLoading: "读取型号清单…",
    deviceListAria: "支持的设备型号",
    confirmPrefix: "我已核对屏幕背面型号为",
    confirmSuffix: "，并知道全新安装可能清除设备中的内容与设置。",
    flashButton: "连接设备并刷入",
    unsupportedMessage: "当前浏览器不支持 Web Serial。请改用桌面版 Chrome、Edge 或支持该能力的 Firefox。",
    notAllowedMessage: "浏览器只允许在 HTTPS 或 localhost 页面刷写设备。GitHub Pages 部署后会自动满足 HTTPS 条件。",
    helperSelect: "先选择已支持的型号，并完成上方确认。",
    bootHelpTitle: "浏览器没有找到串口？",
    bootHelpCable: "换用可传输数据的 USB 线，并关闭正在占用串口的监视器。若设备未自动进入下载模式，按住 BOOT，再点击刷入；开始连接后可短按 RESET。",
    bootHelpBrowser: "Safari、iOS 与多数移动浏览器不提供 Web Serial；请在桌面浏览器完成刷写。",
    preflightAria: "刷写前置检查",
    preflightTitle: "开始前检查",
    securePage: "安全页面",
    checkingSecure: "正在检查 HTTPS / localhost",
    checkingSerial: "正在检查浏览器能力",
    baudRate: "串口速率",
    baudDetail: "固定使用 115200，匹配当前硬件链路",
    modelNotDetected: "型号不会自动判定",
    modelDetail: "浏览器只能识别 ESP32-S3，不能识别具体屏幕板型",
    railNote: "刷写过程约需数分钟。保持页面可见，期间不要拔线或关闭浏览器。",
    workflowTitle: "从 USB 到第一张画面",
    stepConnectTitle: "连接。",
    stepConnectBody: "用数据线连接设备，选择浏览器列出的串口。刷机台会先确认芯片家族为 ESP32-S3。",
    stepWriteTitle: "写入。",
    stepWriteBody: "Factory 镜像从 0x000000 写入，包含 bootloader、分区表与应用；空白设备不需要另刷其他文件。",
    stepProvisionTitle: "配网。",
    stepProvisionBody: "重启后扫描屏幕上的 Wi‑Fi 二维码，完成首次联网；之后通过屏幕显示的局域网地址管理内容。",
    featureTitle: "固件做什么",
    featureIntro: "页面展示的每一项都来自当前固件实现；没有云服务、虚构指标或尚未适配的设备承诺。",
    localTitle: "在局域网管理内容。",
    localBody: "首次启动时完成 Wi-Fi 配置，之后可在浏览器中添加文字和图片、调整播放顺序，并控制屏幕亮度。",
    localFact: "无需云账号 · 设置保存在设备中 · 中英文界面",
    mediaTitle: "上传图片与 GIF。",
    mediaBody: "PNG 和 JPG 自动居中裁切为 480 × 480；不超过 2 MB、480 × 480 的 GIF 保留动画并按原尺寸居中播放。",
    mediaFact: "最多 16 个内容页 · PNG · JPG · GIF",
    tfTitle: "直接播放 TF 卡内容。",
    tfBody: "可从 TF 卡直接添加 PNG、JPG（.jpg）、GIF 与 ScreenDeck 480 × 480（.rgb565）文件，无需复制到机身存储。",
    tfFact: "扫描 3 层目录 · .jpg 后缀 · GIF ≤ 480 × 480",
    playbackTitle: "切页更流畅。",
    playbackBody: "固件会缓存可预加载的静态图片，并采用整帧更新和淡入淡出，减少切换时的撕裂、闪烁与旧画面残留。",
    playbackFact: "静态图片预加载 · 整帧更新 · 淡入淡出",
    settingsTitle: "调节显示与作息。",
    settingsBody: "支持联网自动校时、日期时间叠层、5–100% 亮度和跨午夜定时息屏；息屏后可双击临时唤醒。",
    settingsFact: "自动校时 · 定时息屏 · 双击唤醒",
    hardwareTitle: "支持设备",
    hardwareIntro: "当前只列出有代码与构建配置依据的板型。后续新增型号时，每台设备使用独立清单与固件路径。",
    deviceAlt: "ESP32-S3-4848S040 方形触控屏的技术示意，标出 ESP32-S3、ST7701、GT911，以及右侧的 TF 卡槽和 USB Type-C 接口",
    figureCaption: "结构示意，不是产品实拍。刷写前仍需核对 PCB 或商品页上的完整型号。",
    supportedBuild: "已支持并提供构建配置",
    moreModels: "更多型号正在适配",
    moreModelsNote: "尚未验证的设备不会提前列入“已支持”。",
    footerStatement: "一块方屏，不必只显示默认画面。",
    footerAria: "页脚导航",
    footerFlash: "网页刷入",
    footerHardware: "支持设备",
    noscript: "网页刷机台需要 JavaScript。请启用 JavaScript 后重新载入页面。",
    secureReady: "已通过 HTTPS / localhost 安全上下文",
    secureError: "当前不是 HTTPS 或 localhost，浏览器会阻止串口访问",
    serialReady: "浏览器已提供串口访问能力",
    serialError: "请改用支持 Web Serial 的桌面浏览器",
    helperSecure: "当前页面不在安全上下文中。请通过 HTTPS 或 localhost 打开。",
    helperSerial: "当前浏览器不能访问串口。请使用桌面版 Chrome、Edge 或支持 Web Serial 的 Firefox。",
    helperManifestChecking: "正在核对该型号的固件清单与 Factory 镜像…",
    helperConfirm: "请勾选型号与数据清除确认。",
    helperReady: "已就绪。点击按钮后，在浏览器窗口中选择设备串口。",
    manifestFileError: "无法从 file:// 读取固件。请用本地 HTTP 服务打开本页，不要直接双击 index.html。",
    manifestUnavailable: "该型号的 Factory 镜像尚未就绪。请先完成构建与发布。",
    modelsAvailable: "{count} 个可用型号",
    registryFailed: "型号清单读取失败",
    helperRegistryFailed: "无法读取设备型号清单。请刷新页面，或检查站点资源是否完整。",
    moreDevices: "更多设备",
    moreDevicesSummary: "适配完成后会作为独立型号加入",
  },
  en: {
    documentTitle: "ScreenDeck · Flash firmware in your browser",
    metaDescription: "ScreenDeck turns the ESP32-S3-4848S040 into a locally managed 480×480 content display and flashes its firmware directly from a browser.",
    skipLink: "Skip to main content",
    navAria: "Main navigation",
    homeAria: "ScreenDeck home",
    navToolsAria: "Page tools",
    githubAria: "View GitHub",
    githubLabel: "GitHub",
    languageControl: "Chinese and English language switch",
    languageSwitchToEnglish: "Switch to English",
    languageSwitchToChinese: "切换到中文",
    donateAria: "Support ScreenDeck",
    donateLabel: "Donate",
    heroTitleLead: "Turn a square screen",
    heroTitleAccent: "into a content deck.",
    heroLede: "ScreenDeck is built for one specific board: connect USB and flash it in the browser, scan a code to set up Wi-Fi on first boot, then manage text, images, and playback order directly over your local network.",
    heroCta: "Flash now",
    heroProofAria: "Current hardware configuration",
    supportedNow: "Currently supported",
    brandLabel: "Brand",
    displayLabel: "Display",
    touchLabel: "Touch",
    storageLabel: "Storage",
    proofNote: "Future models will use separate firmware manifests, preventing different displays with the same chip from receiving the wrong image.",
    flashPathAria: "Flashing path",
    installTitle: "Browser flashing station",
    installIntro: "No desktop software required. After you confirm the model, the browser guides you through port selection, chip detection, erase confirmation, writing, and verification.",
    releaseAria: "Firmware release information",
    channelLabel: "Channel",
    versionLabel: "Version",
    updatedLabel: "Updated",
    imageLabel: "Image",
    channelRelease: "Release",
    channelDevelopment: "Development",
    chooseDevice: "Choose a device",
    registryLoading: "Loading device registry…",
    deviceListAria: "Supported device models",
    confirmPrefix: "I verified that the model printed on the back is",
    confirmSuffix: ", and understand that a fresh install may erase content and settings on the device.",
    flashButton: "Connect and flash",
    unsupportedMessage: "This browser does not support Web Serial. Use desktop Chrome, Edge, or a Firefox build that provides it.",
    notAllowedMessage: "Browsers only allow device flashing on HTTPS or localhost. A GitHub Pages deployment meets the HTTPS requirement automatically.",
    helperSelect: "Choose a supported model and complete the confirmation above.",
    bootHelpTitle: "Browser cannot find the serial port?",
    bootHelpCable: "Use a USB cable that supports data and close any serial monitor using the port. If the board does not enter download mode automatically, hold BOOT and click flash; briefly press RESET after connection begins.",
    bootHelpBrowser: "Safari, iOS, and most mobile browsers do not provide Web Serial. Complete the flash from a desktop browser.",
    preflightAria: "Preflight checks",
    preflightTitle: "Check before flashing",
    securePage: "Secure page",
    checkingSecure: "Checking HTTPS / localhost",
    checkingSerial: "Checking browser capability",
    baudRate: "Serial speed",
    baudDetail: "Fixed at 115200 to match the current hardware link",
    modelNotDetected: "Model is not auto-detected",
    modelDetail: "The browser can identify ESP32-S3, but not the specific display board",
    railNote: "Flashing takes a few minutes. Keep this page visible and do not unplug the cable or close the browser.",
    workflowTitle: "From USB to the first screen",
    stepConnectTitle: "Connect.",
    stepConnectBody: "Connect the board with a data cable and choose its serial port in the browser. The installer first verifies the ESP32-S3 chip family.",
    stepWriteTitle: "Write.",
    stepWriteBody: "The Factory image is written from 0x000000 and includes the bootloader, partition table, and application. A blank board needs no additional files.",
    stepProvisionTitle: "Provision.",
    stepProvisionBody: "After restart, scan the Wi-Fi QR code on the display for first-time setup. Then manage content through the local address shown on screen.",
    featureTitle: "What the firmware does",
    featureIntro: "Every capability shown here exists in the current firmware—no cloud service, invented metrics, or promises for unadapted hardware.",
    localTitle: "Manage content locally.",
    localBody: "Set up Wi-Fi on first boot, then add text and images, reorder playback, and control screen brightness from a browser on the same network.",
    localFact: "No cloud account · settings stay on device · Chinese and English UI",
    mediaTitle: "Upload images and GIFs.",
    mediaBody: "PNG and JPG files are center-cropped to 480 × 480. GIFs up to 2 MB and 480 × 480 keep their animation and play centered at native size.",
    mediaFact: "Up to 16 content pages · PNG · JPG · GIF",
    tfTitle: "Play directly from TF card.",
    tfBody: "Add PNG, JPG (.jpg), GIF, and ScreenDeck 480 × 480 (.rgb565) files directly from a TF card without copying them into internal storage.",
    tfFact: "Scans 3 directory levels · .jpg suffix · GIF ≤ 480 × 480",
    playbackTitle: "Switch pages smoothly.",
    playbackBody: "The firmware caches static images that can be preloaded and uses complete-frame updates with fades to reduce tearing, flashes, and stale frames during transitions.",
    playbackFact: "Static image preload · complete-frame updates · fades",
    settingsTitle: "Control display and schedule.",
    settingsBody: "Use network time sync, date and time overlays, 5–100% brightness, and scheduled screen-off periods that can cross midnight. Double-tap to wake temporarily.",
    settingsFact: "Automatic time sync · scheduled sleep · double-tap wake",
    hardwareTitle: "Supported hardware",
    hardwareIntro: "Only boards backed by current code and build configuration are listed. Every future model will have its own manifest and firmware path.",
    deviceAlt: "Technical diagram of the ESP32-S3-4848S040 square touchscreen, labeling ESP32-S3, ST7701, GT911, the TF card slot, and the USB Type-C port on the right",
    figureCaption: "Structural diagram, not a product photo. Verify the complete model number on the PCB or product page before flashing.",
    supportedBuild: "Supported with a dedicated build configuration",
    moreModels: "More models are being adapted",
    moreModelsNote: "Unverified hardware will not be listed as supported early.",
    footerStatement: "A square screen can show more than its default image.",
    footerAria: "Footer navigation",
    footerFlash: "Web flash",
    footerHardware: "Supported hardware",
    noscript: "The web flashing station requires JavaScript. Enable JavaScript and reload the page.",
    secureReady: "HTTPS / localhost secure context confirmed",
    secureError: "This is not HTTPS or localhost, so the browser will block serial access",
    serialReady: "Serial access is available in this browser",
    serialError: "Use a desktop browser that supports Web Serial",
    helperSecure: "This page is not in a secure context. Open it through HTTPS or localhost.",
    helperSerial: "This browser cannot access serial ports. Use desktop Chrome, Edge, or a Firefox build that supports Web Serial.",
    helperManifestChecking: "Checking this model's firmware manifest and Factory image…",
    helperConfirm: "Confirm the model and possible data erase above.",
    helperReady: "Ready. Click the button, then choose the device serial port in the browser window.",
    manifestFileError: "Firmware cannot be loaded from file://. Serve this folder over HTTP instead of opening index.html directly.",
    manifestUnavailable: "The Factory image for this model is not ready. Complete the build and release first.",
    modelsAvailable: "{count} model available",
    registryFailed: "Device registry failed to load",
    helperRegistryFailed: "The device registry could not be loaded. Refresh the page or check that all site assets are present.",
    moreDevices: "More devices",
    moreDevicesSummary: "Each adapted board will appear as a separate model",
  },
};

const compatibility = {
  secure: window.isSecureContext,
  serial: "serial" in navigator,
};

let currentLanguage = loadLanguage();
let selectedDevice = null;
let availableDevices = [];
let registryStatus = "loading";
let manifestReady = false;
let manifestRequest = null;
let manifestErrorKey = "";
let releaseInfo = {
  channel: "development",
  version: "development",
  builtAt: "",
  builtOn: "",
};

function loadLanguage() {
  try {
    return localStorage.getItem("screenDeckSiteLanguage") === "en" ? "en" : "zh";
  } catch {
    return "zh";
  }
}

function t(key, values = {}) {
  const template = translations[currentLanguage][key] ?? translations.zh[key] ?? key;
  return Object.entries(values).reduce(
    (message, [name, value]) => message.replaceAll(`{${name}}`, String(value)),
    template,
  );
}

function persistLanguage(language) {
  try {
    localStorage.setItem("screenDeckSiteLanguage", language);
  } catch {
    // Language still applies for the current page when storage is unavailable.
  }
}

function setCheck(element, state, messageKey) {
  element.dataset.state = state;
  const detail = element.querySelector("span:last-child");
  if (detail) detail.textContent = t(messageKey);
}

function setHelper(messageKey, state = "idle") {
  helper.textContent = t(messageKey);
  helper.dataset.state = state;
}

function updateCompatibility() {
  setCheck(
    secureCheck,
    compatibility.secure ? "ready" : "error",
    compatibility.secure ? "secureReady" : "secureError",
  );
  setCheck(
    serialCheck,
    compatibility.serial ? "ready" : "error",
    compatibility.serial ? "serialReady" : "serialError",
  );
}

function updateRegistryState() {
  if (registryStatus === "ready") {
    registryState.textContent = t("modelsAvailable", { count: availableDevices.length });
    registryState.dataset.state = "ready";
  } else if (registryStatus === "error") {
    registryState.textContent = t("registryFailed");
    registryState.dataset.state = "error";
  } else {
    registryState.textContent = t("registryLoading");
    registryState.removeAttribute("data-state");
  }
}

function updateInstallState() {
  const confirmed = confirmInput.checked;
  const ready = Boolean(
    selectedDevice &&
      manifestReady &&
      confirmed &&
      compatibility.secure &&
      compatibility.serial,
  );

  flashButton.disabled = !ready;
  flashButton.setAttribute("aria-disabled", String(!ready));

  if (!compatibility.secure) {
    setHelper("helperSecure", "error");
  } else if (!compatibility.serial) {
    setHelper("helperSerial", "error");
  } else if (registryStatus === "error") {
    setHelper("helperRegistryFailed", "error");
  } else if (!selectedDevice) {
    setHelper("helperSelect");
  } else if (manifestErrorKey) {
    setHelper(manifestErrorKey, "error");
  } else if (!manifestReady) {
    setHelper("helperManifestChecking");
  } else if (!confirmed) {
    setHelper("helperConfirm");
  } else {
    setHelper("helperReady", "ready");
  }
}

function createDeviceOption(device) {
  const label = document.createElement("label");
  label.className = "device-option";

  const input = document.createElement("input");
  input.type = "radio";
  input.name = "device";
  input.value = device.id;
  input.checked = selectedDevice?.id === device.id;
  input.setAttribute("aria-label", `${device.vendor} ${device.name}`);

  const copy = document.createElement("span");
  copy.className = "device-option__copy";

  const name = document.createElement("strong");
  name.textContent = device.name;

  const summary = document.createElement("span");
  summary.textContent = `${device.vendor} · ${device.summary}`;

  copy.append(name, summary);
  label.append(input, copy);

  input.addEventListener("change", () => selectDevice(device));
  return label;
}

function createComingSoonOption() {
  const label = document.createElement("div");
  label.className = "device-option device-option--coming";
  label.setAttribute("aria-disabled", "true");

  const spacer = document.createElement("span");
  spacer.setAttribute("aria-hidden", "true");

  const copy = document.createElement("span");
  copy.className = "device-option__copy";

  const name = document.createElement("strong");
  name.textContent = t("moreDevices");

  const summary = document.createElement("span");
  summary.textContent = t("moreDevicesSummary");

  copy.append(name, summary);
  label.append(spacer, copy);
  return label;
}

function renderDeviceList() {
  if (registryStatus !== "ready") return;
  deviceList.replaceChildren(
    ...availableDevices.map(createDeviceOption),
    createComingSoonOption(),
  );
}

function applyLanguage(language) {
  currentLanguage = language === "en" ? "en" : "zh";
  document.documentElement.lang = currentLanguage === "en" ? "en" : "zh-CN";
  document.title = t("documentTitle");
  descriptionMeta?.setAttribute("content", t("metaDescription"));
  persistLanguage(currentLanguage);

  document.querySelectorAll("[data-i18n]").forEach((element) => {
    element.textContent = t(element.dataset.i18n);
  });
  document.querySelectorAll("[data-i18n-aria]").forEach((element) => {
    element.setAttribute("aria-label", t(element.dataset.i18nAria));
  });
  document.querySelectorAll("[data-i18n-title]").forEach((element) => {
    element.setAttribute("title", t(element.dataset.i18nTitle));
  });
  document.querySelectorAll("[data-i18n-alt]").forEach((element) => {
    element.setAttribute("alt", t(element.dataset.i18nAlt));
  });

  const switchingToEnglish = currentLanguage === "zh";
  const languageLabel = switchingToEnglish ? "EN" : "中";
  const languageAria = t(
    switchingToEnglish ? "languageSwitchToEnglish" : "languageSwitchToChinese",
  );
  languageToggleLabel.textContent = languageLabel;
  languageToggle.setAttribute("aria-label", languageAria);
  languageToggle.setAttribute("title", languageAria);
  languageToggle.setAttribute("lang", switchingToEnglish ? "en" : "zh-CN");

  updateCompatibility();
  updateRegistryState();
  updateReleaseLine();
  renderDeviceList();
  updateInstallState();
}

function channelDisplayName() {
  return releaseInfo.channel === "release" ? t("channelRelease") : t("channelDevelopment");
}

function formatReleaseDate() {
  const iso = releaseInfo.builtAt || (releaseInfo.builtOn ? `${releaseInfo.builtOn}T00:00:00Z` : "");
  if (!iso) return "—";
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return releaseInfo.builtOn || iso;
  if (currentLanguage === "zh") {
    return `${date.getUTCFullYear()}年${date.getUTCMonth() + 1}月${date.getUTCDate()}日`;
  }
  return new Intl.DateTimeFormat("en-US", {
    year: "numeric",
    month: "short",
    day: "numeric",
    timeZone: "UTC",
  }).format(date);
}

function updateReleaseLine() {
  const channelEl = document.querySelector("#releaseChannel");
  const versionEl = document.querySelector("#releaseVersion");
  const updatedEl = document.querySelector("#releaseUpdated");
  if (channelEl) channelEl.textContent = channelDisplayName();
  if (versionEl) versionEl.textContent = releaseInfo.version || "—";
  if (updatedEl) {
    updatedEl.textContent = formatReleaseDate();
    if (releaseInfo.builtAt) updatedEl.setAttribute("title", releaseInfo.builtAt);
    else updatedEl.removeAttribute("title");
  }
}

async function loadReleaseInfo() {
  const releaseUrl = new URL("../firmware/release.json", import.meta.url);
  try {
    const response = await fetch(releaseUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`release HTTP ${response.status}`);
    const payload = await response.json();
    releaseInfo = {
      channel: payload.channel === "release" ? "release" : "development",
      version: payload.version || "development",
      builtAt: payload.builtAt || "",
      builtOn: payload.builtOn || "",
    };
  } catch {
    // Keep the committed development placeholders when the file is missing.
  }
  updateReleaseLine();
}

function resolveGithubLink() {
  if (!githubLink) return;
  const githubPagesHost = window.location.hostname.match(/^([^.]+)\.github\.io$/i);
  const projectName = window.location.pathname.split("/").filter(Boolean)[0];
  if (githubPagesHost && projectName) {
    githubLink.href = `https://github.com/${githubPagesHost[1]}/${projectName}`;
  } else {
    githubLink.href = "https://github.com/mrjoechen/ScreenDeck";
  }
}

async function verifyManifest(device) {
  manifestRequest?.abort();
  manifestRequest = new AbortController();
  manifestReady = false;
  manifestErrorKey = "";
  updateInstallState();

  const manifestUrl = new URL(device.manifest, document.baseURI);

  try {
    const response = await fetch(manifestUrl, {
      cache: "no-store",
      signal: manifestRequest.signal,
    });
    if (!response.ok) throw new Error(`manifest HTTP ${response.status}`);

    const manifest = await response.json();
    const build = manifest.builds?.find(
      (candidate) => candidate.chipFamily === device.chipFamily,
    );
    const factoryPart = build?.parts?.find((part) => part.offset === 0);
    if (!factoryPart?.path) throw new Error("factory image is missing from manifest");

    const firmwareUrl = new URL(factoryPart.path, manifestUrl);
    const firmwareResponse = await fetch(firmwareUrl, {
      method: "HEAD",
      cache: "no-store",
      signal: manifestRequest.signal,
    });
    if (!firmwareResponse.ok) {
      throw new Error(`firmware HTTP ${firmwareResponse.status}`);
    }

    installer.setAttribute("manifest", manifestUrl.href);
    manifestReady = true;
    manifestErrorKey = "";
  } catch (error) {
    if (error.name === "AbortError") return;
    manifestReady = false;
    manifestErrorKey =
      window.location.protocol === "file:"
        ? "manifestFileError"
        : "manifestUnavailable";
  } finally {
    updateInstallState();
  }
}

function selectDevice(device) {
  selectedDevice = device;
  confirmInput.checked = false;
  confirmInput.disabled = false;
  confirmedModel.textContent = device.name;
  verifyManifest(device);
}

async function loadRegistry() {
  const registryUrl = new URL("../firmware/devices.json", import.meta.url);

  try {
    const response = await fetch(registryUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`registry HTTP ${response.status}`);
    const registry = await response.json();
    if (!Array.isArray(registry.devices) || registry.devices.length === 0) {
      throw new Error("registry has no devices");
    }

    availableDevices = registry.devices;
    registryStatus = "ready";
    renderDeviceList();
    updateRegistryState();
  } catch {
    availableDevices = [];
    registryStatus = "error";
    deviceList.replaceChildren();
    updateRegistryState();
    updateInstallState();
  }
}

languageToggle.addEventListener("click", () => {
  applyLanguage(currentLanguage === "zh" ? "en" : "zh");
});
confirmInput.addEventListener("change", updateInstallState);

resolveGithubLink();
applyLanguage(currentLanguage);
loadReleaseInfo();
loadRegistry();
