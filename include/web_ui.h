#pragma once

#include <Arduino.h>

static const char WEB_UI_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="theme-color" content="#111719">
  <title>ScreenDeck</title>
  <style>
    :root {
      --ink:#111719;
      --ink-2:#20282a;
      --paper:#f4efe6;
      --acid:#e7ff54;
      --coral:#ff7058;
      --muted:#8e9a9d;
      --line:rgba(244,239,230,.16);
      --radius:22px;
    }
    *{box-sizing:border-box}
    html{background:var(--ink);color:var(--paper);font-family:"Avenir Next","PingFang SC","Hiragino Sans GB",sans-serif}
    body{margin:0;min-height:100vh;background:
      radial-gradient(circle at 82% 8%,rgba(231,255,84,.14),transparent 28rem),
      radial-gradient(circle at -8% 60%,rgba(255,112,88,.11),transparent 25rem),
      var(--ink)}
    body:before{content:"";position:fixed;inset:0;pointer-events:none;opacity:.22;
      background-image:radial-gradient(rgba(244,239,230,.38) .7px,transparent .7px);
      background-size:14px 14px}
    button,input,textarea,select{font:inherit}
    button{cursor:pointer}
    .shell{position:relative;max-width:1120px;margin:auto;padding:24px clamp(18px,4vw,48px) 70px}
    header{display:flex;align-items:center;justify-content:space-between;margin-bottom:54px}
    .brand{display:flex;align-items:center;gap:12px;font-weight:800;letter-spacing:.13em}
    .brand-mark{width:38px;height:10px;background:var(--acid);border-radius:10px}
    .status{display:flex;align-items:center;gap:9px;color:var(--muted);font-size:13px}
    .pulse{width:9px;height:9px;border-radius:50%;background:var(--coral);box-shadow:0 0 0 0 rgba(255,112,88,.7);animation:pulse 2s infinite}
    .pulse.online{background:var(--acid);box-shadow:none}
    @keyframes pulse{70%{box-shadow:0 0 0 9px rgba(255,112,88,0)}100%{box-shadow:0 0 0 0 rgba(255,112,88,0)}}
    .hero{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(260px,.75fr);gap:28px;align-items:end;margin-bottom:34px}
    .kicker{color:var(--acid);font-size:12px;font-weight:800;letter-spacing:.14em;margin-bottom:14px}
    h1{font-size:clamp(48px,9vw,104px);line-height:.84;letter-spacing:-.065em;margin:0;font-weight:800}
    h1 span{display:block;color:transparent;-webkit-text-stroke:1.5px var(--paper)}
    .hero-note{border-left:2px solid var(--acid);padding:4px 0 4px 18px;color:#c5cdce;line-height:1.6;max-width:360px}
    .ip{font-family:"SFMono-Regular",Consolas,monospace;color:var(--paper);font-size:22px;letter-spacing:-.03em;margin-top:8px;word-break:break-all}
    nav{display:flex;gap:7px;padding:6px;border:1px solid var(--line);border-radius:18px;width:max-content;margin-bottom:22px;background:rgba(17,23,25,.75);backdrop-filter:blur(12px)}
    nav button{border:0;background:transparent;color:var(--muted);padding:10px 18px;border-radius:12px;font-weight:700}
    nav button.active{background:var(--paper);color:var(--ink)}
    .view{display:none}.view.active{display:block;animation:rise .38s ease both}
    @keyframes rise{from{opacity:0;transform:translateY(10px)}}
    .grid{display:grid;grid-template-columns:repeat(12,1fr);gap:18px}
    .card{background:rgba(32,40,42,.88);border:1px solid var(--line);border-radius:var(--radius);padding:24px;box-shadow:0 18px 70px rgba(0,0,0,.16)}
    .card h2{margin:0 0 7px;font-size:22px;letter-spacing:-.03em}
    .card p{margin:0 0 20px;color:var(--muted);line-height:1.75;font-size:14px}
    .compose{grid-column:span 7}.upload{grid-column:span 5}.pages{grid-column:1/-1}.llm-card{grid-column:1/-1}.settings-card{grid-column:span 7}.network-card{grid-column:span 5}
    .settings-form{display:contents}.time-card{grid-column:span 7}.playback-card{grid-column:span 5}.sleep-card{grid-column:1/-1}
    label{display:block;font-size:12px;font-weight:800;letter-spacing:.08em;color:#bec7c8;margin-bottom:8px}
    textarea,input[type=text],input[type=password],input[type=url],input[type=datetime-local],input[type=time],select{width:100%;border:1px solid var(--line);border-radius:14px;background:#151c1e;color:var(--paper);padding:14px 15px;outline:none;transition:.2s;color-scheme:dark}
    textarea:focus,input:focus,select:focus{border-color:var(--acid);box-shadow:0 0 0 3px rgba(231,255,84,.08)}
    textarea{min-height:136px;resize:vertical;line-height:1.6}
    .colors{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin:14px 0 18px}
    .color{display:flex;align-items:center;gap:10px;background:#151c1e;border:1px solid var(--line);padding:9px 12px;border-radius:14px}
    input[type=color]{width:34px;height:34px;padding:0;border:0;background:transparent}
    .color label{margin:0;letter-spacing:0;text-transform:none}
    .btn{border:0;border-radius:14px;padding:13px 18px;background:var(--acid);color:var(--ink);font-weight:800;transition:.18s;display:inline-flex;align-items:center;justify-content:center;gap:8px}
    .btn:hover{transform:translateY(-2px);box-shadow:0 8px 24px rgba(231,255,84,.15)}
    .btn:focus-visible{outline:2px solid var(--paper);outline-offset:3px}
    .btn:disabled{cursor:wait;opacity:.48;transform:none;box-shadow:none}
    .btn.secondary{background:transparent;color:var(--paper);border:1px solid var(--line)}
    .btn.danger{background:transparent;color:var(--coral);border:1px solid rgba(255,112,88,.35)}
    .drop{position:relative;border:1.5px dashed rgba(231,255,84,.42);border-radius:18px;min-height:170px;display:grid;place-items:center;text-align:center;padding:22px;background:linear-gradient(145deg,rgba(231,255,84,.06),transparent);transition:.2s}
    .drop:hover,.drop:focus-within{border-color:var(--acid);background:linear-gradient(145deg,rgba(231,255,84,.1),transparent)}
    .drop input{position:absolute;inset:0;opacity:0;cursor:pointer}
    .drop b{display:block;font-size:27px;letter-spacing:-.04em;margin-bottom:8px}
    .drop small{color:var(--muted)}
    .file-name{margin-top:13px;color:var(--acid);font-size:13px;min-height:20px}
    .upload-previews{display:grid;grid-template-columns:repeat(auto-fill,minmax(92px,1fr));gap:10px;margin:0 0 16px}
    .upload-preview{position:relative;overflow:hidden;border:1px solid var(--line);border-radius:14px;background:#151c1e;animation:rise .25s ease both}
    .upload-preview img{display:block;width:100%;aspect-ratio:1;object-fit:cover;background:#0d1213}
    .upload-preview span{display:block;padding:8px 9px 9px;color:#bec7c8;font-size:11px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
    .upload-preview button{position:absolute;top:6px;right:6px;width:28px;height:28px;border:1px solid rgba(244,239,230,.28);border-radius:50%;background:rgba(17,23,25,.84);color:var(--paper);font-size:18px;line-height:1;backdrop-filter:blur(8px)}
    .upload-preview button:hover{background:var(--coral);border-color:var(--coral);color:var(--ink)}
    .upload .btn[type=submit]{width:100%}
    .upload .btn[type=submit]:disabled{cursor:not-allowed;opacity:.45;transform:none;box-shadow:none}
    .page-list{display:grid;gap:10px}
    .page-row{display:grid;grid-template-columns:48px 64px minmax(0,1fr) auto;gap:14px;align-items:center;padding:12px;border-radius:16px;background:#151c1e;border:1px solid rgba(244,239,230,.1)}
    .page-index{font-family:"SFMono-Regular",monospace;color:var(--acid);font-size:13px;text-align:center}
    .thumb{width:64px;height:54px;border-radius:11px;object-fit:cover;background:#293234;display:grid;place-items:center;color:var(--muted);font-size:11px;overflow:hidden}
    .page-copy{min-width:0}.page-copy b{display:block;margin-bottom:4px}.page-copy span{display:block;color:var(--muted);font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
    .row-actions{display:flex;gap:5px}.icon-btn{width:36px;height:36px;border:1px solid var(--line);border-radius:10px;background:transparent;color:var(--paper)}.icon-btn.delete{color:var(--coral)}
    .system-row{border-color:rgba(231,255,84,.25)}
    .empty{padding:38px;text-align:center;color:var(--muted)}
    .range-line{display:flex;align-items:center;gap:16px}
    input[type=range]{width:100%;accent-color:var(--acid)}
    .range-value{font:800 30px/1 "SFMono-Regular",monospace;color:var(--acid);min-width:75px}
    .field-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.field-grid .wide{grid-column:1/-1}
    .field-hint,.card .field-hint{display:block;margin:10px 0 0;color:var(--muted);font-size:12px;line-height:1.75}
    .card .inline-actions+.field-hint{margin-top:20px}
    .llm-card h2{margin-bottom:12px}
    .llm-card .field-grid{row-gap:22px;margin-bottom:20px}
    .llm-card textarea{line-height:1.75}
    .llm-card .inline-actions{margin-top:20px;row-gap:14px}
    .llm-card .sleep-summary{margin-top:0;line-height:1.75}
    .prompt-count{text-align:right;font-variant-numeric:tabular-nums}
    #llmNarrationPrompt[aria-invalid=true]{border-color:var(--coral)}
    .inline-actions{display:flex;gap:10px;align-items:center;margin-top:14px;flex-wrap:wrap}
    .llm-test-state{display:none;align-items:center;gap:8px;min-height:30px;padding:6px 10px;border:1px solid var(--line);border-radius:999px;color:var(--muted);font:700 12px/1.35 "SFMono-Regular",Consolas,monospace}
    .llm-test-state:not(:empty){display:inline-flex}.llm-test-state:before{content:"";width:7px;height:7px;flex:0 0 7px;border-radius:50%;background:currentColor}
    .llm-test-state[data-state=queued],.llm-test-state[data-state=running]{color:var(--acid);border-color:rgba(231,255,84,.25)}
    .llm-test-state[data-state=running]:before{animation:pulse 1.3s infinite}
    @media(prefers-reduced-motion:reduce){.llm-test-state[data-state=running]:before{animation:none}}
    .llm-test-state[data-state=passed]{color:var(--acid);background:rgba(231,255,84,.07);border-color:rgba(231,255,84,.35)}
    .llm-test-state[data-state=failed]{color:var(--coral);background:rgba(255,112,88,.07);border-color:rgba(255,112,88,.35)}
    .switch-line{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:15px 0;border-top:1px solid var(--line)}
    .switch-line:first-of-type{border-top:0}.switch-copy b{display:block;margin-bottom:8px;line-height:1.5}.switch-copy span{display:block;font-size:13px;color:var(--muted);line-height:1.75}
    .toggle{position:relative;display:block;flex:0 0 58px;width:58px;height:32px;margin:0}.toggle input{position:absolute;opacity:0;pointer-events:none}.toggle i{display:block;width:58px;height:32px;border-radius:20px;background:#111719;border:1px solid var(--line);transition:.2s}.toggle i:after{content:"";display:block;width:22px;height:22px;margin:4px;border-radius:50%;background:var(--muted);transition:.2s}.toggle input:checked+i{background:var(--acid);border-color:var(--acid)}.toggle input:checked+i:after{transform:translateX(26px);background:var(--ink)}.toggle input:focus-visible+i{outline:2px solid var(--paper);outline-offset:3px}.toggle input:disabled+i{opacity:.5;cursor:wait}
    .clock-preview{margin-top:22px;width:184px;height:104px;padding:8px 0 0;background:transparent;border:0;display:flex;flex-direction:column;align-items:center;gap:8px}
    .clock-preview span{font:700 24px/27px "SFMono-Regular",monospace;color:var(--muted);letter-spacing:.08em}.clock-preview b{width:100%;font:800 48px/52px "SFMono-Regular",monospace;color:var(--acid);letter-spacing:-.05em;text-align:center}
    .clock-preview span,.clock-preview b{text-shadow:2px 2px 0 rgba(0,0,0,.5)}
    .sleep-layout{display:grid;grid-template-columns:minmax(220px,.65fr) 1fr;gap:24px;align-items:end}.time-pair{display:grid;grid-template-columns:1fr auto 1fr;gap:12px;align-items:end;transition:opacity .2s}.time-pair.is-disabled{opacity:.42}.time-pair.is-disabled input{cursor:not-allowed}.time-arrow{color:var(--acid);font-size:24px;padding-bottom:12px}.sleep-summary{color:var(--muted);font-size:13px;margin-top:12px}
    .network-list{display:grid;gap:8px;margin:16px 0}
    .network{display:flex;justify-content:space-between;align-items:center;width:100%;text-align:left;background:#151c1e;color:var(--paper);border:1px solid var(--line);border-radius:13px;padding:12px 14px}
    .network:hover{border-color:var(--acid)}
    .wifi-form{display:grid;gap:12px}
    .provision{border-color:rgba(231,255,84,.5);background:linear-gradient(135deg,rgba(231,255,84,.11),rgba(32,40,42,.92))}
    .toast{position:fixed;right:20px;bottom:20px;max-width:330px;background:var(--paper);color:var(--ink);border-radius:14px;padding:14px 18px;font-weight:700;box-shadow:0 16px 50px rgba(0,0,0,.3);transform:translateY(30px);opacity:0;pointer-events:none;transition:.25s;z-index:20}
    .toast.show{transform:none;opacity:1}.toast.error{background:var(--coral)}
    .storage{margin-top:22px}.meter{height:7px;border-radius:9px;background:#111719;overflow:hidden;margin-top:9px}.meter i{display:block;height:100%;background:var(--acid);width:0}
    .language-line{margin-top:22px;padding-top:20px;border-top:1px solid var(--line)}
    .language-switch{display:grid;grid-template-columns:1fr 1fr;gap:5px;width:min(100%,280px);padding:5px;border:1px solid var(--line);border-radius:15px;background:#151c1e}
    .language-switch button{border:0;border-radius:10px;padding:10px 14px;background:transparent;color:var(--muted);font-weight:800;transition:.18s}
    .language-switch button.active{background:var(--acid);color:var(--ink);box-shadow:0 6px 18px rgba(231,255,84,.1)}
    .language-switch button:focus-visible{outline:2px solid var(--paper);outline-offset:2px}
    footer{margin-top:40px;color:#697577;font-size:12px;letter-spacing:.06em}
    .sd-card{grid-column:1/-1}
    .sd-head{display:flex;align-items:center;justify-content:space-between;gap:14px;flex-wrap:wrap;margin-bottom:16px}
    .sd-state{color:var(--muted);font-size:13px}
    .sd-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(148px,1fr));gap:12px}
    .sd-item{display:flex;flex-direction:column;gap:0;text-align:left;padding:0;overflow:hidden;background:#151c1e;border:1px solid var(--line);border-radius:16px;color:var(--paper)}
    .sd-item:hover{border-color:var(--acid)}
    .sd-item img{width:100%;aspect-ratio:1;object-fit:cover;background:#0d1213}
    .sd-item .sd-fallback{width:100%;aspect-ratio:1;display:grid;place-items:center;background:#0d1213;color:var(--muted);font:800 13px/1 "SFMono-Regular",monospace}
    .sd-meta{padding:10px 12px 12px;min-width:0}
    .sd-meta b{display:block;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
    .sd-meta span{display:block;color:var(--muted);font-size:12px;margin-top:3px}
    .badge{display:inline-block;margin-left:6px;padding:1px 7px;border-radius:9px;background:rgba(231,255,84,.16);color:var(--acid);font-size:11px;font-weight:800;vertical-align:middle}
    @media(max-width:760px){
      body{background:var(--ink)}body:before{display:none}.pulse{animation:none}.view.active{animation:none}nav{backdrop-filter:none}
      .shell{padding-top:18px}.hero{grid-template-columns:1fr;gap:18px;margin-bottom:28px}header{margin-bottom:42px}
      h1{font-size:clamp(52px,18vw,82px)}.hero-note{max-width:none}.grid{display:block}.settings-form{display:block}.card{margin-bottom:14px;padding:20px}
      nav{width:100%}nav button{flex:1}.page-row{grid-template-columns:38px 52px minmax(0,1fr)}.thumb{width:52px;height:48px}.row-actions{grid-column:2/-1;justify-content:flex-end}
      .field-grid,.sleep-layout{grid-template-columns:1fr}.time-pair{grid-template-columns:1fr auto 1fr}
    }
  </style>
</head>
<body>
<main class="shell">
  <header>
    <div class="brand"><span class="brand-mark"></span>ScreenDeck</div>
    <div class="status"><span id="pulse" class="pulse"></span><span id="statusText" data-i18n="statusConnecting">正在连接设备</span></div>
  </header>

  <section class="hero">
    <div>
      <div class="kicker">ESP32-S3 / 480 × 480</div>
      <h1><b data-i18n="heroTitle">屏幕</b><span data-i18n="heroOutline">内容台</span></h1>
    </div>
    <div class="hero-note">
      <span data-i18n="heroNote">在同一局域网内编辑屏幕。保存后立即生效，左右滑动屏幕切换内容。</span>
      <div class="ip" id="ipAddress">—</div>
    </div>
  </section>

  <section id="provisionPanel" class="card provision" hidden>
    <div class="kicker" data-i18n="provisionKicker">首次配置</div>
    <h2 data-i18n="provisionTitle">把屏幕接入你的 Wi-Fi</h2>
    <p data-i18n="provisionBody">选择网络并输入密码。凭据会保存在设备 NVS 中，重启后自动连接。</p>
    <button class="btn secondary" id="scanButton" data-i18n="scanNetworks">扫描附近网络</button>
    <div id="networkList" class="network-list"></div>
    <form id="wifiForm" class="wifi-form">
      <div><label for="ssid" data-i18n="wifiName">Wi-Fi 名称</label><input id="ssid" name="ssid" type="text" autocomplete="username" required></div>
      <div><label for="password" data-i18n="wifiPassword">Wi-Fi 密码</label><input id="password" name="password" type="password" autocomplete="current-password"></div>
      <button class="btn" type="submit" data-i18n="saveConnect">保存并连接</button>
    </form>
  </section>

  <div id="controller" hidden>
    <nav>
      <button class="active" data-view="contentView" data-i18n="navContent">内容</button>
      <button data-view="settingsView" data-i18n="navSettings">设置</button>
    </nav>

    <section id="contentView" class="view active">
      <div class="grid">
        <form id="textForm" class="card compose">
          <div class="kicker" data-i18n="textKicker">01 / 文字页面</div>
          <h2 data-i18n="textTitle">写一张全屏卡片</h2>
          <p data-i18n="textBody">内置 MiSans 中英文字体，支持换行、常用中文和单色 Emoji；纯英文会自动选择更大的字号。</p>
          <label for="pageText" data-i18n="displayText">显示文字</label>
          <textarea id="pageText" name="text" maxlength="1536" placeholder="在这里输入屏幕要展示的内容…" data-i18n-placeholder="textPlaceholder" required></textarea>
          <div class="colors">
            <div class="color"><input type="color" name="background" value="#111719"><label data-i18n="background">背景</label></div>
            <div class="color"><input type="color" name="foreground" value="#f4efe6"><label data-i18n="foreground">文字</label></div>
          </div>
          <button class="btn" type="submit" data-i18n="addTextPage">添加文字页</button>
        </form>

        <form id="uploadForm" class="card upload">
          <div class="kicker" data-i18n="imageKicker">02 / 图片页面</div>
          <h2 data-i18n="imageTitle">批量上传画面</h2>
          <p data-i18n="imageBody">可一次选择多张图片；PNG/JPG 会自动居中裁切并预转换成屏幕原生格式。检测到 TF 卡时保存到 ScreenDeck 文件夹，否则使用机身存储。</p>
          <div class="drop">
            <input id="imageFile" name="image" type="file" accept=".png,.jpg,.jpeg,.gif,image/png,image/jpeg,image/gif" multiple>
            <div><b data-i18n="imageDrop">拖入或点选多张图片</b><small>PNG · JPG · GIF</small></div>
          </div>
          <div id="fileName" class="file-name" aria-live="polite"></div>
          <div id="uploadPreviews" class="upload-previews"></div>
          <button class="btn" type="submit" data-i18n="uploadAdd" disabled>上传所选图片</button>
        </form>

        <section class="card pages">
          <div class="kicker" data-i18n="pagesKicker">03 / 播放顺序</div>
          <h2 data-i18n="pagesTitle">屏幕页面</h2>
          <p data-i18n="pagesBody">二维码入口页不参与播放；向下滑动打开设备设置，再点击二维码。内容页可排序或删除。</p>
          <div id="pageList" class="page-list"></div>
        </section>

        <section class="card sd-card">
          <div class="kicker" data-i18n="sdKicker">04 / TF 卡</div>
          <div class="sd-head">
            <div>
              <h2 data-i18n="sdTitle">从存储卡添加</h2>
              <p data-i18n="sdBody">网页不会自动扫描卡内文件。如需添加手动复制的素材，可按需浏览。</p>
            </div>
            <div class="inline-actions">
              <span id="sdState" class="sd-state"></span>
              <button id="sdRescan" class="btn secondary" type="button" data-i18n="sdRescan">浏览卡内文件</button>
            </div>
          </div>
          <div id="sdList" class="sd-grid"></div>
        </section>
      </div>
    </section>

    <section id="settingsView" class="view">
      <div class="grid">
        <form id="displaySettingsForm" class="settings-form">
          <section class="card time-card">
            <div class="kicker" data-i18n="timeKicker">05 / 时间基准</div>
            <h2 data-i18n="timeTitle">日期、时间与时区</h2>
            <p data-i18n="timeBody">设备通过网络自动校时，也可以在这里写入指定时间。时区使用固定 UTC 偏移。</p>
            <div class="field-grid">
              <div><label for="timezoneOffset" data-i18n="timezone">时区</label><select id="timezoneOffset" name="timezoneOffsetMinutes"></select></div>
              <div><label for="deviceDateTime" data-i18n="deviceLocalTime">设备当地时间</label><input id="deviceDateTime" type="datetime-local" min="2020-01-01T00:00" max="2100-12-31T23:59" step="60" required></div>
            </div>
            <div class="inline-actions">
              <button id="useBrowserTime" class="btn secondary" type="button" data-i18n="useBrowserTime">使用浏览器当前时间</button>
              <span id="timeSyncNote" class="sleep-summary" data-i18n="readingDeviceTime">等待读取设备时间…</span>
            </div>
          </section>

          <section class="card playback-card">
            <div class="kicker" data-i18n="overlayKicker">06 / 播放叠层</div>
            <h2 data-i18n="overlayTitle">播放页信息叠层</h2>
            <p data-i18n="overlayBody">日期时间可显示在图片和文字页；天气与日期时间合并为同一卡片。初始随机位于四角之一，此后每分钟在左上、右上、左下、右下间移动。</p>
            <div class="switch-line">
              <div class="switch-copy"><b data-i18n="dateTime">日期 + 时间</b><span data-i18n="overlayHint">二维码页不会重复显示</span></div>
              <label class="toggle" aria-label="播放页显示日期时间" data-i18n-aria="showDateTimeAria"><input id="showDateTime" type="checkbox"><i></i></label>
            </div>
            <div class="switch-line">
              <div class="switch-copy"><b data-i18n="weather">天气</b><span data-i18n="weatherHint">图片页 · 与时间同一卡片 · 每 30 分钟更新</span></div>
              <label class="toggle" aria-label="图片页显示天气" data-i18n-aria="showWeatherAria"><input id="showWeather" type="checkbox"><i></i></label>
            </div>
            <div id="clockPreview" class="clock-preview"><span id="clockPreviewDate">0000-00-00</span><b id="clockPreviewTime">00:00</b></div>
          </section>

          <section class="card sleep-card">
            <div class="sleep-layout">
              <div>
                <div class="kicker" data-i18n="sleepKicker">07 / 息屏计划</div>
                <h2 data-i18n="sleepTitle">让屏幕按时休息</h2>
                <p data-i18n="sleepBody">跨午夜时间段会自动识别；息屏后双击可临时点亮，停手 30 秒后重新息屏。开始与结束相同表示全天不息屏。</p>
                <div class="switch-line">
                  <div class="switch-copy"><b data-i18n="enableSleep">启用自动息屏</b><span data-i18n="sleepHint">到达结束时间后自动恢复显示</span></div>
                  <label class="toggle" aria-label="启用自动息屏" data-i18n-aria="enableSleepAria"><input id="screenOffEnabled" type="checkbox"><i></i></label>
                </div>
              </div>
              <div>
                <div id="screenOffTimes" class="time-pair">
                  <div><label for="screenOffStart" data-i18n="sleepAt">息屏时间</label><input id="screenOffStart" type="time" step="60" value="22:00" required></div>
                  <div class="time-arrow">→</div>
                  <div><label for="screenOffEnd" data-i18n="wakeAt">恢复时间</label><input id="screenOffEnd" type="time" step="60" value="07:00" required></div>
                </div>
                <div id="sleepSummary" class="sleep-summary">22:00 息屏，次日 07:00 恢复</div>
                <div class="inline-actions"><button class="btn" type="submit" data-i18n="saveDisplaySettings">保存时间与显示设置</button></div>
              </div>
            </div>
          </section>
        </form>

        <form id="llmSettingsForm" class="card llm-card">
          <div class="kicker" data-i18n="llmKicker">08 / LLM 设置</div>
          <h2 data-i18n="llmTitle">AI 图片摘要</h2>
          <p data-i18n="llmBody">请配置 OpenAI 兼容 API 的 Base URL、API Key 和支持图片理解的模型。设备会生成不超过 64 字的图片摘要并保存在本机，已有摘要会直接复用。请优先使用 HTTPS，并仅在可信 Wi-Fi 中打开本管理页。</p>
          <div class="field-grid">
            <div class="wide"><label for="llmBaseUrl" data-i18n="llmBaseUrl">Base URL</label><input id="llmBaseUrl" name="baseUrl" type="url" inputmode="url" maxlength="383" autocomplete="url" autocapitalize="none" spellcheck="false" placeholder="https://api.openai.com/v1" required></div>
            <div><label for="llmApiKey" data-i18n="llmApiKey">API Key</label><input id="llmApiKey" name="apiKey" type="password" maxlength="511" autocomplete="new-password" autocapitalize="none" spellcheck="false"></div>
            <div><label for="llmModel" data-i18n="llmModel">模型</label><input id="llmModel" name="model" type="text" maxlength="127" autocomplete="off" autocapitalize="none" spellcheck="false" placeholder="gpt-4.1-mini" required></div>
            <div class="wide">
              <label for="llmNarrationPrompt" data-i18n="llmNarrationPrompt">图片摘要提示词</label>
              <textarea id="llmNarrationPrompt" name="narrationPrompt" maxlength="4096" data-max-codepoints="2048" placeholder="例如：准确、自然地概括图片主体、环境与氛围。" data-i18n-placeholder="llmNarrationPromptPlaceholder" aria-describedby="llmNarrationPromptHint llmNarrationPromptCount" required></textarea>
              <span id="llmNarrationPromptCount" class="field-hint prompt-count" aria-live="polite" aria-atomic="true">0 / 2048 字符</span>
              <span id="llmNarrationPromptHint" class="field-hint" data-i18n="llmNarrationPromptHint">提示词最多 2048 个字符，可详细描述偏好；生成摘要最多 64 个字符，请求格式与输出结构由固件固定。</span>
            </div>
          </div>
          <div class="switch-line">
            <div class="switch-copy"><b id="imageNarrationEnabledLabel" data-i18n="imageNarrationEnabled">显示图片摘要</b><span id="imageNarrationEnabledHint" data-i18n="imageNarrationEnabledHint">开启时显示并获取缺失摘要；关闭会保留已保存的摘要</span></div>
            <label class="toggle" for="imageNarrationEnabled"><input id="imageNarrationEnabled" name="imageNarrationEnabled" type="checkbox" role="switch" aria-labelledby="imageNarrationEnabledLabel" aria-describedby="imageNarrationEnabledHint"><i aria-hidden="true"></i></label>
          </div>
          <div class="switch-line">
            <div class="switch-copy"><b data-i18n="allowInsecureHttp">允许不安全的 HTTP</b><span data-i18n="allowInsecureHttpHint">仅用于可信局域网服务；密钥和图片会明文传输</span></div>
            <label class="toggle" aria-label="允许 LLM 使用不安全的 HTTP" data-i18n-aria="allowInsecureHttpAria"><input id="llmAllowInsecureHttp" type="checkbox"><i></i></label>
          </div>
          <div class="inline-actions">
            <button class="btn" type="submit" data-i18n="saveLlmSettings">保存 LLM 设置</button>
            <button id="testLlmSettings" class="btn secondary" type="button" data-i18n="testLlmSettings" aria-describedby="llmTestState">测试配置</button>
            <button id="clearLlmSettings" class="btn danger" type="button" data-i18n="clearLlmSettings">清除配置</button>
            <button id="resetAllNarrations" class="btn danger" type="button" data-i18n="resetAllNarrations">重置所有图片摘要</button>
            <span id="llmConfigState" class="sleep-summary" aria-live="polite"></span>
            <span id="llmTestState" class="llm-test-state" role="status" aria-live="polite" aria-atomic="true"></span>
          </div>
          <p class="field-hint" data-i18n="narrationGestureHint">开启摘要后，可双击设备上的图片摘要区域（图标或文字）重新生成；成功后自动保存，失败保留原摘要。</p>
        </form>

        <section class="card settings-card">
          <div class="kicker" data-i18n="displayKicker">09 / 显示设置</div>
          <h2 data-i18n="brightnessTitle">屏幕亮度</h2>
          <p data-i18n="brightnessBody">拖动后实时调节背光，松手会保存到设备。</p>
          <div class="range-line">
            <input id="brightness" type="range" min="5" max="100" value="80">
            <output id="brightnessValue" class="range-value">80%</output>
          </div>
          <div class="language-line">
            <label data-i18n="pageTransition">页面切换动画</label>
            <div id="pageTransitionSwitch" class="language-switch" role="group" aria-label="页面切换动画" data-i18n-aria="pageTransitionAria">
              <button type="button" data-transition="fade" class="active" data-i18n="pageTransitionFade">淡入淡出</button>
              <button type="button" data-transition="slide" data-i18n="pageTransitionSlide">水平滑动</button>
            </div>
          </div>
          <div class="language-line">
            <label data-i18n="interfaceLanguage">界面语言</label>
            <div id="languageSwitch" class="language-switch" role="group" aria-label="界面语言" data-i18n-aria="interfaceLanguage">
              <button type="button" data-language="zh">中文</button>
              <button type="button" data-language="en">English</button>
            </div>
          </div>
          <div class="storage">
            <label data-i18n="contentStorage">内容存储</label>
            <div id="storageText">—</div>
            <div class="meter"><i id="storageMeter"></i></div>
            <div id="sdStorage" class="sleep-summary"></div>
          </div>
        </section>
        <section class="card network-card">
          <div class="kicker" data-i18n="networkKicker">10 / 网络设置</div>
          <h2 id="networkName">—</h2>
          <p data-i18n="networkBody">清除已保存的网络后，设备会重启并重新显示配网二维码。</p>
          <button id="resetWifi" class="btn danger" data-i18n="resetWifi">清除 Wi-Fi 并重启</button>
        </section>
      </div>
    </section>
  </div>

  <footer data-i18n="footer">Local control · settings stay on this device</footer>
</main>
<div id="toast" class="toast"></div>
<script>
const $=s=>document.querySelector(s);
const $$=s=>[...document.querySelectorAll(s)];
let device={};
let llmStatusRequestSequence=0;
let selectedImages=[];
let nextSelectedImageId=1;
let uploadInProgress=false;
const translations={
  zh:{
    resetAllNarrations:"重置所有图片摘要",resetNarrationsConfirm:"确定清空所有已保存的图片摘要吗？此操作不可撤销，但不会删除图片或 LLM 配置。开启摘要时，图片再次显示会重新生成摘要。",narrationsReset:"所有图片摘要已重置",narrationGestureHint:"开启摘要后，可双击设备上的图片摘要区域（图标或文字）重新生成；成功后自动保存，失败保留原摘要。",
    statusConnecting:"正在连接设备",heroTitle:"屏幕",heroOutline:"内容台",heroNote:"在同一局域网内编辑屏幕。保存后立即生效，左右滑动屏幕切换内容。",
    provisionKicker:"首次配置",provisionTitle:"把屏幕接入你的 Wi-Fi",provisionBody:"选择网络并输入密码。凭据会保存在设备 NVS 中，重启后自动连接。",scanNetworks:"扫描附近网络",wifiName:"Wi-Fi 名称",wifiPassword:"Wi-Fi 密码",saveConnect:"保存并连接",
    navContent:"内容",navSettings:"设置",textKicker:"01 / 文字页面",textTitle:"写一张全屏卡片",textBody:"内置 MiSans 中英文字体，支持换行、常用中文和单色 Emoji；纯英文会自动选择更大的字号。",displayText:"显示文字",textPlaceholder:"在这里输入屏幕要展示的内容…",background:"背景",foreground:"文字",addTextPage:"添加文字页",
    imageKicker:"02 / 图片页面",imageTitle:"批量上传画面",imageBody:"可一次选择多张图片；PNG/JPG 会自动居中裁切并预转换成屏幕原生格式。检测到 TF 卡时保存到 ScreenDeck 文件夹，否则使用机身存储；GIF 原样上传并按原尺寸播放。",imageDrop:"拖入或点选多张图片",uploadAdd:"上传所选图片",pagesKicker:"03 / 播放顺序",pagesTitle:"屏幕页面",pagesBody:"二维码入口页不参与播放；向下滑动打开设备设置，再点击二维码。内容页可排序或删除。",
    sdKicker:"04 / TF 卡",sdTitle:"从存储卡添加",sdBody:"网页不会自动扫描卡内文件。如需添加手动复制的 PNG、JPG、GIF 或 RGB565 素材，可按需浏览。",sdRescan:"浏览卡内文件",sdMissing:"未检测到 TF 卡",sdUnsupported:"已检测到 TF 卡，但当前文件系统不受支持",sdUnreadable:"已检测到 TF 卡，但文件系统无法读取",sdSupportedFormats:"支持 FAT16 / FAT32",sdUnknownFilesystem:"未知格式",sdScanning:"正在扫描卡内文件…",sdEmpty:"卡上没有找到可播放的文件",sdAdded:"已加入播放列表",sdReady:"已就绪",animatedBadge:"动图",sdStorageLabel:"TF 卡",
    timeKicker:"05 / 时间基准",timeTitle:"日期、时间与时区",timeBody:"设备通过网络自动校时，也可以在这里写入指定时间。时区使用固定 UTC 偏移。",timezone:"时区",deviceLocalTime:"设备当地时间",useBrowserTime:"使用浏览器当前时间",readingDeviceTime:"等待读取设备时间…",
    overlayKicker:"06 / 播放叠层",overlayTitle:"播放页信息叠层",overlayBody:"日期时间可显示在图片和文字页；天气与日期时间合并为同一卡片。初始随机位于四角之一，此后每分钟在左上、右上、左下、右下间移动。",dateTime:"日期 + 时间",overlayHint:"二维码页不会重复显示",showDateTimeAria:"播放页显示日期时间",weather:"天气",weatherHint:"图片页 · 与时间同一卡片 · 每 30 分钟更新",showWeatherAria:"图片页显示天气",
    sleepKicker:"07 / 息屏计划",sleepTitle:"让屏幕按时休息",sleepBody:"跨午夜时间段会自动识别；息屏后双击可临时点亮，持续操作不会熄灭，停手 30 秒后重新息屏。开始与结束相同表示全天不息屏。",enableSleep:"启用自动息屏",sleepHint:"到达结束时间后自动恢复显示",enableSleepAria:"启用自动息屏",sleepAt:"息屏时间",wakeAt:"恢复时间",saveDisplaySettings:"保存时间与显示设置",
    llmKicker:"08 / LLM 设置",llmTitle:"AI 图片摘要",llmBody:"请配置 OpenAI 兼容 API 的 Base URL、API Key 和支持图片理解的模型。设备会生成不超过 64 字的图片摘要并保存在本机，已有摘要会直接复用。请优先使用 HTTPS，并仅在可信 Wi-Fi 中打开本管理页。",llmBaseUrl:"Base URL",llmApiKey:"API Key",llmModel:"模型",llmNarrationPrompt:"图片摘要提示词",llmNarrationPromptHint:"提示词最多 2048 个字符，可详细描述偏好；生成摘要最多 64 个字符，请求格式与输出结构由固件固定。",llmNarrationPromptCount:"{count} / {limit} 字符",llmNarrationPromptTooLong:"提示词最多输入 {limit} 个字符，请缩短后重试。",llmNarrationPromptPlaceholder:"例如：准确、自然地概括图片主体、环境与氛围。",imageNarrationEnabled:"显示图片摘要",imageNarrationEnabledHint:"开启时显示并获取缺失摘要；关闭会保留已保存的摘要",imageNarrationEnabledOn:"图片摘要已开启",imageNarrationEnabledOff:"图片摘要已关闭",allowInsecureHttp:"允许不安全的 HTTP",allowInsecureHttpHint:"仅用于可信局域网服务；密钥和图片会明文传输",allowInsecureHttpAria:"允许 LLM 使用不安全的 HTTP",saveLlmSettings:"保存 LLM 设置",testLlmSettings:"测试配置",testingLlmSettings:"测试中…",clearLlmSettings:"清除配置",llmApiKeyPlaceholder:"输入 API Key",llmApiKeyStored:"API Key 已保存，留空保持不变",llmConfigured:"LLM 已配置",llmNotConfigured:"LLM 尚未配置",llmSaved:"LLM 设置已保存",llmCleared:"LLM 配置已清除，图片摘要提示词已恢复默认值",clearLlmConfirm:"清除 Base URL、API Key 和模型，并将图片摘要提示词恢复为默认值？",llmTestQueued:"测试已排队，等待当前图片请求完成…",llmTestRunning:"正在验证连接、模型与图片请求…",llmTestSuccess:"配置可用，LLM 已成功响应图片请求",llmTestAuthentication:"认证失败，请检查 API Key",llmTestNotFound:"服务地址或模型不存在，请检查 Base URL 与模型",llmTestRateLimited:"请求频率过高或额度不足，请稍后重试",llmTestTimeout:"测试超时，请检查服务状态与网络",llmTestConnection:"无法连接 LLM 服务，请检查 Base URL 与网络",llmTestUpstream:"上游 LLM 服务暂时不可用",llmTestRequestRejected:"LLM 服务拒绝了测试请求，请检查配置",llmTestInvalidResponse:"LLM 返回了无法识别的响应",llmTestInternal:"设备暂时无法执行测试，请稍后重试",llmTestStatusLost:"找不到这次测试记录，请重新测试",llmTestPreviousConfig:"刷新前填写的配置",llmTestInvalidSettings:"配置内容无效；修改 Base URL 或模型时请重新输入 API Key",llmTestPageExpired:"设置页面已过期，请刷新后重新测试",llmTestAlreadyRunning:"已有 LLM 配置测试正在进行，请稍候",llmTestSettingsChanged:"配置已更改，请重新测试",
    displayKicker:"09 / 显示设置",brightnessTitle:"屏幕亮度",brightnessBody:"拖动即可实时调节背光，松手保存到设备。",pageTransition:"页面切换动画",pageTransitionAria:"页面切换动画",pageTransitionFade:"淡入淡出",pageTransitionSlide:"水平滑动",interfaceLanguage:"界面语言",contentStorage:"内容存储",networkKicker:"10 / 网络设置",networkBody:"清除已保存的网络后，设备会重启并重新显示配网二维码。",resetWifi:"清除 Wi-Fi 并重启",footer:"本地控制 · 设置保存在此设备",
    requestFailed:"请求失败",sleepDisabled:"自动息屏未启用",nextDay:"次日 ",sleepOff:"息屏",sleepResume:"恢复",timeSynced:"设备时间已同步",timeWaiting:"设备仍在等待网络校时",imageReadError:"手机无法读取这张图片，请改用 PNG 或 JPG",
    waitingProvision:"等待配网",connected:"已连接",notConnected:"尚未连接",systemEntryTitle:"管理与设置入口",systemEntryHint:"向下滑打开设置，再点二维码 · 不参与自动播放",imagePage:"图片页面",animationPage:"动图页面",textPage:"文字页面",moveUp:"上移",moveDown:"下移",delete:"删除",emptyPages:"还没有内容页，先添加文字或图片。",
    scanning:"扫描中…",scanAgain:"重新扫描",noNetworks:"没有发现网络",deleteConfirm:"删除这个页面？",pageDeleted:"页面已删除",wifiSaved:"已保存，设备正在连接新网络…",textAdded:"文字页面已添加",selectedImages:"已选择 {count} 张图片",removeImage:"移除图片",processingImage:"正在处理第 {current}/{total} 张…",uploading:"正在上传第 {current}/{total} 张…",fastRgb:"快速 RGB565",uploadComplete:"已上传并添加 {count} 张图片",gifTooLarge:"动图不能超过 2 MB",browserTimeFilled:"已填入浏览器当前时间，保存后写入设备",invalidDate:"请选择有效的日期时间",settingsSaved:"时间与显示设置已同步到屏幕",brightnessSaved:"亮度已保存",pageTransitionSaved:"切换动画已保存",resetWifiConfirm:"清除 Wi-Fi 并重新进入配网模式？",restarting:"设备正在重启…",languageSaved:"界面语言已保存"
  },
  en:{
    resetAllNarrations:"Reset all image summaries",resetNarrationsConfirm:"Clear all saved image summaries? This cannot be undone. Images and LLM settings will be kept. With summaries enabled, they will be generated again when images are displayed.",narrationsReset:"All image summaries have been reset",narrationGestureHint:"With summaries enabled, double-tap the summary area (icon or text) on the device to regenerate. A successful result is saved; failure keeps the previous summary.",
    statusConnecting:"Connecting to device",heroTitle:"Screen",heroOutline:"Studio",heroNote:"Edit the display from the same local network. Changes appear immediately; swipe the screen to move through content.",
    provisionKicker:"First-time setup",provisionTitle:"Connect the display to Wi-Fi",provisionBody:"Choose a network and enter its password. Credentials stay in device NVS and reconnect after restart.",scanNetworks:"Scan nearby networks",wifiName:"Wi-Fi name",wifiPassword:"Wi-Fi password",saveConnect:"Save and connect",
    navContent:"Content",navSettings:"Settings",textKicker:"01 / Text page",textTitle:"Write a full-screen card",textBody:"MiSans supports Chinese and English, with line breaks and monochrome emoji. English-only text scales to larger type.",displayText:"Display text",textPlaceholder:"Type what the screen should show…",background:"Background",foreground:"Text",addTextPage:"Add text page",
    imageKicker:"02 / Image page",imageTitle:"Upload a batch",imageBody:"PNG and JPG files are center-cropped into the screen's native format. Uploads use the TF card's ScreenDeck folder when available and onboard storage otherwise; GIF files remain untouched.",imageDrop:"Drop or choose multiple images",uploadAdd:"Upload selected images",pagesKicker:"03 / Play order",pagesTitle:"Screen pages",pagesBody:"The QR entry is excluded from playback. Swipe down for device settings, then tap QR; reorder or remove content below.",
    sdKicker:"04 / TF card",sdTitle:"Add from the card",sdBody:"The controller does not scan card files automatically. Browse only when you need to add a manually copied PNG, JPG, GIF, or RGB565 file.",sdRescan:"Browse card files",sdMissing:"No TF card detected",sdUnsupported:"TF card detected, but its filesystem is not supported",sdUnreadable:"TF card detected, but its filesystem is unreadable",sdSupportedFormats:"Supported: FAT16 / FAT32",sdUnknownFilesystem:"Unknown format",sdScanning:"Scanning card files…",sdEmpty:"No playable files found on the card",sdAdded:"Added to the playlist",sdReady:"Ready",animatedBadge:"GIF",sdStorageLabel:"TF card",
    timeKicker:"05 / Time base",timeTitle:"Date, time, and timezone",timeBody:"The device syncs time over the network. You can also set it manually here. Timezone uses a fixed UTC offset.",timezone:"Timezone",deviceLocalTime:"Device local time",useBrowserTime:"Use browser time",readingDeviceTime:"Reading device time…",
    overlayKicker:"06 / Playback overlay",overlayTitle:"Playback information",overlayBody:"Date and time can appear on image and text pages. Weather shares the same card. It starts randomly in one corner, then moves among the top-left, top-right, bottom-left, and bottom-right each minute.",dateTime:"Date + time",overlayHint:"Not repeated on the QR page",showDateTimeAria:"Show date and time on playback pages",weather:"Weather",weatherHint:"Image pages · same card as the clock · updates every 30 minutes",showWeatherAria:"Show weather on image pages",
    sleepKicker:"07 / Sleep schedule",sleepTitle:"Let the display rest on time",sleepBody:"Overnight windows are detected automatically. Double-tap a sleeping screen to wake it; it stays awake while you keep using it and sleeps again 30 seconds after you stop. Matching times disable the window.",enableSleep:"Enable scheduled sleep",sleepHint:"The display resumes automatically at the end time",enableSleepAria:"Enable scheduled screen sleep",sleepAt:"Screen off",wakeAt:"Resume at",saveDisplaySettings:"Save time and display settings",
    llmKicker:"08 / LLM settings",llmTitle:"AI image summaries",llmBody:"Configure an OpenAI-compatible API with its Base URL, API key, and a model that supports image understanding. The device generates summaries of up to 64 characters, saves them locally, and reuses existing summaries. Prefer HTTPS and only open this controller on trusted Wi-Fi.",llmBaseUrl:"Base URL",llmApiKey:"API Key",llmModel:"Model",llmNarrationPrompt:"Image summary prompt",llmNarrationPromptHint:"Prompt: up to 2048 characters for detailed preferences. Generated summary: up to 64 characters. Request format and output structure are fixed in firmware.",llmNarrationPromptCount:"{count} / {limit} characters",llmNarrationPromptTooLong:"The prompt allows up to {limit} characters. Shorten it and try again.",llmNarrationPromptPlaceholder:"For example: Summarize the subject, setting, and mood accurately and naturally.",imageNarrationEnabled:"Show image summaries",imageNarrationEnabledHint:"Show summaries and fetch missing ones when on; saved summaries are kept when off",imageNarrationEnabledOn:"Image summaries enabled",imageNarrationEnabledOff:"Image summaries disabled",allowInsecureHttp:"Allow insecure HTTP",allowInsecureHttpHint:"Trusted LAN services only; the key and image travel in cleartext",allowInsecureHttpAria:"Allow insecure HTTP for the LLM",saveLlmSettings:"Save LLM settings",testLlmSettings:"Test configuration",testingLlmSettings:"Testing…",clearLlmSettings:"Clear configuration",llmApiKeyPlaceholder:"Enter an API key",llmApiKeyStored:"API key saved; leave blank to keep it",llmConfigured:"LLM configured",llmNotConfigured:"LLM not configured",llmSaved:"LLM settings saved",llmCleared:"LLM settings cleared; the image summary prompt was reset to its default",clearLlmConfirm:"Clear the Base URL, API key, and model, and reset the image summary prompt to its default?",llmTestQueued:"Test queued until the current image request finishes…",llmTestRunning:"Checking the connection, model, and image request…",llmTestSuccess:"Configuration works — the LLM successfully answered the image request",llmTestAuthentication:"Authentication failed. Check the API key.",llmTestNotFound:"The service or model was not found. Check the Base URL and model.",llmTestRateLimited:"Rate limit or quota reached. Try again later.",llmTestTimeout:"The test timed out. Check the service and network.",llmTestConnection:"Could not connect to the LLM service. Check the Base URL and network.",llmTestUpstream:"The upstream LLM service is temporarily unavailable.",llmTestRequestRejected:"The LLM service rejected the test request. Check the configuration.",llmTestInvalidResponse:"The LLM returned an unrecognized response.",llmTestInternal:"The device could not run the test. Try again shortly.",llmTestStatusLost:"This test record is no longer available. Run the test again.",llmTestPreviousConfig:"Values entered before this page was refreshed",llmTestInvalidSettings:"The settings are invalid. Re-enter the API key after changing the Base URL or model.",llmTestPageExpired:"The settings page expired. Refresh it and run the test again.",llmTestAlreadyRunning:"An LLM configuration test is already in progress.",llmTestSettingsChanged:"Configuration changed — run the test again.",
    displayKicker:"09 / Display settings",brightnessTitle:"Screen brightness",brightnessBody:"Drag to change the backlight live; release to save it on the device.",pageTransition:"Page animation",pageTransitionAria:"Page animation",pageTransitionFade:"Fade",pageTransitionSlide:"Slide",interfaceLanguage:"Interface language",contentStorage:"Content storage",networkKicker:"10 / Network settings",networkBody:"Clearing the saved network restarts the device and shows the Wi-Fi setup QR code again.",resetWifi:"Clear Wi-Fi and restart",footer:"Local control · settings stay on this device",
    requestFailed:"Request failed",sleepDisabled:"Scheduled sleep is off",nextDay:"next day ",sleepOff:"off",sleepResume:"resumes",timeSynced:"Device time is synchronized",timeWaiting:"The device is still waiting for network time",imageReadError:"This image cannot be read on your device. Try a PNG or JPG.",
    waitingProvision:"Waiting for setup",connected:"Connected",notConnected:"Not connected",systemEntryTitle:"Management and settings",systemEntryHint:"Swipe down for settings, then tap QR · excluded from autoplay",imagePage:"Image page",animationPage:"Animated page",textPage:"Text page",moveUp:"Move up",moveDown:"Move down",delete:"Delete",emptyPages:"No content pages yet. Add text or an image to begin.",
    scanning:"Scanning…",scanAgain:"Scan again",noNetworks:"No networks found",deleteConfirm:"Delete this page?",pageDeleted:"Page deleted",wifiSaved:"Saved. The device is connecting to the new network…",textAdded:"Text page added",selectedImages:"{count} images selected",removeImage:"Remove image",processingImage:"Processing image {current} of {total}…",uploading:"Uploading image {current} of {total}…",fastRgb:"fast RGB565",uploadComplete:"Uploaded and added {count} images",gifTooLarge:"A GIF must be 2 MB or smaller",browserTimeFilled:"Browser time filled in; save to write it to the device",invalidDate:"Choose a valid date and time",settingsSaved:"Time and display settings synced to the screen",brightnessSaved:"Brightness saved",pageTransitionSaved:"Page animation saved",resetWifiConfirm:"Clear Wi-Fi and return to setup mode?",restarting:"The device is restarting…",languageSaved:"Interface language saved"
  }
};
let currentLanguage="zh";
try{if(localStorage.getItem("screenDeckLanguage")==="en")currentLanguage="en"}catch(e){}
const t=key=>translations[currentLanguage][key]??translations.zh[key]??key;
const tf=(key,values={})=>Object.entries(values).reduce((message,[name,value])=>message.split(`{${name}}`).join(value),t(key));
const esc=s=>String(s??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]));
function toast(message,error=false){const el=$("#toast");el.textContent=message;el.className="toast show"+(error?" error":"");clearTimeout(el._t);el._t=setTimeout(()=>el.className="toast",2800)}
async function api(url,options={}){const r=await fetch(url,options);let data={};try{data=await r.json()}catch(e){}if(!r.ok||data.ok===false){const error=new Error(data.error||t("requestFailed"));error.status=r.status;throw error}return data}
function renderDeviceStatus(){if(!device.mode)return;$("#statusText").textContent=device.mode==="provisioning"?t("waitingProvision"):`${t("connected")} · ${device.ssid}`;$("#networkName").textContent=device.ssid||t("notConnected")}
function applyLanguage(language){currentLanguage=language==="en"?"en":"zh";document.documentElement.lang=currentLanguage==="en"?"en":"zh-CN";try{localStorage.setItem("screenDeckLanguage",currentLanguage)}catch(e){};$$('[data-i18n]').forEach(el=>el.textContent=t(el.dataset.i18n));$$('[data-i18n-placeholder]').forEach(el=>el.setAttribute("placeholder",t(el.dataset.i18nPlaceholder)));$$('[data-i18n-aria]').forEach(el=>el.setAttribute("aria-label",t(el.dataset.i18nAria)));$$("#languageSwitch button").forEach(button=>{const active=button.dataset.language===currentLanguage;button.classList.toggle("active",active);button.setAttribute("aria-pressed",String(active))});renderDeviceStatus();if(device.mode&&device.mode!=="provisioning")$("#timeSyncNote").textContent=Number(device.epoch)>=1577836800?t("timeSynced"):t("timeWaiting");renderSelectedImages();renderSdMedia();refreshSettingsPreview();refreshLlmConfigState();renderLlmTestState();refreshLlmPromptCounter()}
function formatBytes(n){if(!Number.isFinite(n))return"—";return n>1048576?(n/1048576).toFixed(1)+" MB":(n/1024).toFixed(0)+" KB"}
function sdIssueText(status,filesystem){const format=filesystem&&filesystem!=="unknown"?` · ${filesystem}`:"";if(status==="unsupported")return`${t("sdUnsupported")}${format} · ${t("sdSupportedFormats")}`;if(status==="unreadable")return`${t("sdUnreadable")}${format} · ${t("sdSupportedFormats")}`;return t("sdMissing")}
function timezoneName(minutes){const sign=minutes<0?"-":"+";const absolute=Math.abs(minutes);return`UTC${sign}${String(Math.floor(absolute/60)).padStart(2,"0")}:${String(absolute%60).padStart(2,"0")}`}
function buildTimezoneOptions(){const values=[];for(let value=-720;value<=840;value+=30)values.push(value);values.push(345);values.sort((a,b)=>a-b);$("#timezoneOffset").innerHTML=values.map(value=>`<option value="${value}">${timezoneName(value)}</option>`).join("")}
function timeFromMinutes(minutes){return`${String(Math.floor(minutes/60)).padStart(2,"0")}:${String(minutes%60).padStart(2,"0")}`}
function minutesFromTime(value){const parts=String(value).split(":").map(Number);return parts.length===2&&parts.every(Number.isFinite)?parts[0]*60+parts[1]:0}
function dateTimeForOffset(epoch,offset){const value=Number(epoch)>=1577836800?Number(epoch)*1000:Date.now();return new Date(value+offset*60000).toISOString().slice(0,16)}
function refreshSettingsPreview(){const parts=($("#deviceDateTime").value||"0000-00-00T00:00").split("T");$("#clockPreviewDate").textContent=parts[0];$("#clockPreviewTime").textContent=parts[1]||"00:00";const enabled=$("#screenOffEnabled").checked;$("#screenOffTimes").classList.toggle("is-disabled",!enabled);$("#screenOffStart").disabled=!enabled;$("#screenOffEnd").disabled=!enabled;const start=$("#screenOffStart").value;const end=$("#screenOffEnd").value;const crosses=start>end;if(!enabled){$("#sleepSummary").textContent=t("sleepDisabled");return}$("#sleepSummary").textContent=currentLanguage==="zh"?`${start} ${t("sleepOff")}，${crosses?t("nextDay"):""}${end} ${t("sleepResume")}`:`${start} ${t("sleepOff")}, ${t("sleepResume")} ${crosses?t("nextDay"):""}${end}`}
function hydratePageTransition(){const style=device.pageTransition==="slide"?"slide":"fade";$$("#pageTransitionSwitch button").forEach(button=>{const active=button.dataset.transition===style;button.classList.toggle("active",active);button.setAttribute("aria-pressed",String(active))})}
function hydrateDisplaySettings(){const offset=Number(device.timezoneOffsetMinutes??480);$("#timezoneOffset").value=String(offset);$("#deviceDateTime").value=dateTimeForOffset(device.epoch,offset);$("#showDateTime").checked=!!device.showDateTime;$("#showWeather").checked=!!device.showWeather;$("#screenOffEnabled").checked=!!device.screenOffEnabled;$("#screenOffStart").value=timeFromMinutes(Number(device.screenOffStartMinutes??1320));$("#screenOffEnd").value=timeFromMinutes(Number(device.screenOffEndMinutes??420));$("#timeSyncNote").textContent=Number(device.epoch)>=1577836800?t("timeSynced"):t("timeWaiting");hydratePageTransition();refreshSettingsPreview()}
function llmApiKeyConfigured(){return typeof device.llmApiKeyConfigured==="boolean"?device.llmApiKeyConfigured:!!device.llmConfigured}
function llmIsConfigured(){return typeof device.llmConfigured==="boolean"?device.llmConfigured:!!(device.llmBaseUrl&&device.llmModel&&llmApiKeyConfigured())}
function refreshLlmConfigState(){const key=$("#llmApiKey");if(!key)return;key.placeholder=llmApiKeyConfigured()?t("llmApiKeyStored"):t("llmApiKeyPlaceholder");$("#llmConfigState").textContent=llmIsConfigured()?t("llmConfigured"):t("llmNotConfigured")}
function refreshLlmPromptCounter(){
  const input=$("#llmNarrationPrompt"),counter=$("#llmNarrationPromptCount");
  const limit=Number(input.dataset.maxCodepoints);
  // HTML maxlength counts UTF-16 units (up to two per code point). Its 4096
  // safety cap leaves room for 2048 astral characters; validate the real limit
  // here to match the firmware's UTF-8 decoder, without truncating pasted text.
  const count=Array.from(input.value).length;
  const tooLong=count>limit;
  counter.textContent=tf("llmNarrationPromptCount",{count,limit});
  input.setCustomValidity(tooLong?tf("llmNarrationPromptTooLong",{limit}):"");
  input.setAttribute("aria-invalid",String(tooLong));
}
let llmFormHydrated=false;
let llmFormDirty=false;
let llmTestMatchesCurrentForm=false;
let llmHydratedConfigRevision=0;
let llmNarrationTogglePending=false;
function invalidateLlmTestResult(){if(!llmTestMatchesCurrentForm)return;llmTestMatchesCurrentForm=false;llmTestResumed=false;setLlmTestState("stale","llmTestSettingsChanged")}
function hydrateImageNarrationSetting(){$("#imageNarrationEnabled").checked=device.imageNarrationEnabled!==false}
function hydrateLlmSettings(){const nextRevision=Number(device.llmConfigRevision)||0;if(llmFormHydrated&&nextRevision&&llmHydratedConfigRevision&&nextRevision!==llmHydratedConfigRevision)invalidateLlmTestResult();$("#llmBaseUrl").value=device.llmBaseUrl||"";$("#llmModel").value=device.llmModel||"";$("#llmNarrationPrompt").value=device.llmNarrationPrompt||"";$("#llmApiKey").value="";$("#llmAllowInsecureHttp").checked=String(device.llmBaseUrl||"").startsWith("http://");llmHydratedConfigRevision=nextRevision;llmFormHydrated=true;llmFormDirty=false;refreshLlmConfigState();refreshLlmPromptCounter()}
let llmTestResumed=false;
function llmTestMessage(messageKey,httpStatus=0){const status=Number(httpStatus);const message=t(messageKey||"llmTestInternal");const result=Number.isInteger(status)&&status>=400&&status<=599?`HTTP ${status} · ${message}`:message;return llmTestResumed?`${t("llmTestPreviousConfig")} · ${result}`:result}
function renderLlmTestState(){const status=$("#llmTestState");if(!status)return;const messageKey=status.dataset.messageKey||"";const message=messageKey?llmTestMessage(messageKey,status.dataset.httpStatus):"";if(status.textContent!==message)status.textContent=message}
function setLlmTestState(state,messageKey,httpStatus=0){const status=$("#llmTestState");status.dataset.state=state||"";status.dataset.messageKey=messageKey||"";status.dataset.httpStatus=String(httpStatus||0);renderLlmTestState()}
function markLlmSettingsChanged(){llmFormDirty=true;invalidateLlmTestResult();refreshLlmPromptCounter()}
function llmTestFailureKey(result){const reasonKeys={offline:"llmTestConnection",authentication:"llmTestAuthentication",not_found:"llmTestNotFound",rate_limited:"llmTestRateLimited",timeout:"llmTestTimeout",connection:"llmTestConnection",upstream:"llmTestUpstream",request_rejected:"llmTestRequestRejected",invalid_response:"llmTestInvalidResponse",internal:"llmTestInternal"};return reasonKeys[result.reason]||"llmTestInternal"}
const wait=milliseconds=>new Promise(resolve=>setTimeout(resolve,milliseconds));
const LLM_TEST_ID_SESSION_KEY="screenDeck.llmTestId";
const LLM_TEST_PENDING_TOKEN_SESSION_KEY="screenDeck.llmTestPendingToken";
const LLM_TEST_TOKEN_NOT_FOUND_GRACE_MS=8000;
let llmTestObserverKey="";
let llmTestObserverPromise=null;
let llmTestPostPending=false;
function normalizeLlmTestId(value){const id=String(value??"");return /^[1-9]\d{0,9}$/.test(id)?id:""}
function normalizeLlmRequestToken(value){const token=String(value??"");return /^(?!0{32}$)[0-9a-fA-F]{32}$/.test(token)?token:""}
function createLlmRequestToken(){const bytes=new Uint8Array(16);let token="";do{crypto.getRandomValues(bytes);token=Array.from(bytes,value=>value.toString(16).padStart(2,"0")).join("")}while(/^0{32}$/.test(token));return token}
function rememberLlmTestId(id){const normalized=normalizeLlmTestId(id);if(!normalized)return;try{sessionStorage.setItem(LLM_TEST_ID_SESSION_KEY,normalized)}catch(e){}}
function rememberedLlmTestId(){try{return sessionStorage.getItem(LLM_TEST_ID_SESSION_KEY)||""}catch(e){return""}}
function clearRememberedLlmTestId(expected=""){try{if(!expected||sessionStorage.getItem(LLM_TEST_ID_SESSION_KEY)===expected)sessionStorage.removeItem(LLM_TEST_ID_SESSION_KEY)}catch(e){}}
function rememberPendingLlmRequestToken(requestToken){const normalized=normalizeLlmRequestToken(requestToken);if(!normalized)return;try{sessionStorage.setItem(LLM_TEST_PENDING_TOKEN_SESSION_KEY,normalized)}catch(e){}}
function rememberedPendingLlmRequestToken(){try{return sessionStorage.getItem(LLM_TEST_PENDING_TOKEN_SESSION_KEY)||""}catch(e){return""}}
function clearRememberedPendingLlmRequestToken(expected=""){try{if(!expected||sessionStorage.getItem(LLM_TEST_PENDING_TOKEN_SESSION_KEY)===expected)sessionStorage.removeItem(LLM_TEST_PENDING_TOKEN_SESSION_KEY)}catch(e){}}
function clearLlmTestMarkers(testId="",requestToken=""){clearRememberedLlmTestId(testId);clearRememberedPendingLlmRequestToken(requestToken)}
function llmTestStartError(error){const status=Number(error.status);if(error instanceof TypeError)return{messageKey:"llmTestConnection",httpStatus:0};const statusKeys={400:"llmTestInvalidSettings",403:"llmTestPageExpired",409:"llmTestAlreadyRunning",503:"llmTestInternal"};return{messageKey:statusKeys[status]||(Number.isInteger(status)?"llmTestRequestRejected":"llmTestInternal"),httpStatus:Number.isInteger(status)?status:0}}
function setLlmTestActivity(busy){const button=$("#testLlmSettings");setLlmFormBusy(busy);button.dataset.i18n=busy?"testingLlmSettings":"testLlmSettings";button.textContent=t(button.dataset.i18n);button.setAttribute("aria-busy",String(busy))}
async function pollLlmTest(testId,requestToken,notFoundGraceUntil=0){const deadline=Date.now()+105000;while(Date.now()<deadline){let result;const controller=new AbortController();const pollTimeout=setTimeout(()=>controller.abort(),Math.max(1,Math.min(5000,deadline-Date.now())));try{result=testId?await api(`/api/llm/test?id=${encodeURIComponent(testId)}`,{signal:controller.signal}):await api(`/api/llm/test?requestToken=${encodeURIComponent(requestToken)}`,{signal:controller.signal})}catch(error){if(Date.now()>=deadline){setLlmTestState("failed","llmTestTimeout");toast(llmTestMessage("llmTestTimeout"),true);return}const status=Number(error.status);if(!testId&&requestToken&&status===404&&Date.now()<notFoundGraceUntil){await wait(650);continue}const retryable=!Number.isInteger(status)||status===408||status===429||(status>=500&&status<=599);if(retryable){await wait(900);continue}const messageKey=status===404?"llmTestStatusLost":Number.isInteger(status)?"llmTestRequestRejected":"llmTestConnection";const httpStatus=Number.isInteger(status)?status:0;setLlmTestState("failed",messageKey,httpStatus);toast(llmTestMessage(messageKey,httpStatus),true);return}finally{clearTimeout(pollTimeout)}const returnedId=normalizeLlmTestId(result.id);if(!returnedId||(testId&&returnedId!==testId)){setLlmTestState("failed","llmTestInvalidResponse");toast(llmTestMessage("llmTestInvalidResponse"),true);return}if(!testId){testId=returnedId;rememberLlmTestId(testId)}if(result.state==="passed"){setLlmTestState("passed","llmTestSuccess");toast(llmTestMessage("llmTestSuccess"));return}if(result.state==="failed"){const messageKey=llmTestFailureKey(result);setLlmTestState("failed",messageKey,result.httpStatus);toast(llmTestMessage(messageKey,result.httpStatus),true);return}if(result.state==="queued")setLlmTestState("queued","llmTestQueued");else if(result.state==="running")setLlmTestState("running","llmTestRunning");else{setLlmTestState("failed","llmTestInvalidResponse");toast(llmTestMessage("llmTestInvalidResponse"),true);return}await wait(650)}setLlmTestState("failed","llmTestTimeout");toast(llmTestMessage("llmTestTimeout"),true)}
function observeLlmTest(testId="",requestToken="",notFoundGraceMs=0){const id=normalizeLlmTestId(testId);const token=normalizeLlmRequestToken(requestToken);if(!id&&!token)return Promise.reject(new Error());const observerKey=id?`id:${id}`:`token:${token}`;if(llmTestObserverPromise)return llmTestObserverKey===observerKey?llmTestObserverPromise:Promise.reject(new Error());llmTestObserverKey=observerKey;if(id)rememberLlmTestId(id);if(token)rememberPendingLlmRequestToken(token);setLlmTestActivity(true);if(!$("#llmTestState").textContent)setLlmTestState("queued","llmTestQueued");const notFoundGraceUntil=token?Date.now()+Math.max(0,notFoundGraceMs):0;const observerPromise=pollLlmTest(id,token,notFoundGraceUntil).finally(()=>{if(llmTestObserverPromise!==observerPromise)return;clearLlmTestMarkers(id,token);llmTestObserverKey="";llmTestObserverPromise=null;setLlmTestActivity(false)});llmTestObserverPromise=observerPromise;return observerPromise}
function resumeRememberedLlmTest(){if(llmTestObserverPromise||llmTestPostPending)return;const id=normalizeLlmTestId(rememberedLlmTestId());const requestToken=normalizeLlmRequestToken(rememberedPendingLlmRequestToken());if(!id&&!requestToken){clearLlmTestMarkers();return}llmTestResumed=true;setLlmTestState("queued","llmTestQueued");if(id){observeLlmTest(id,requestToken).catch(()=>{});return}clearRememberedLlmTestId();observeLlmTest("",requestToken,LLM_TEST_TOKEN_NOT_FOUND_GRACE_MS).catch(()=>{})}
function renderSelectedImages(){
  const count=selectedImages.length;
  $("#fileName").textContent=count?tf("selectedImages",{count}):"";
  $("#uploadPreviews").innerHTML=selectedImages.map(item=>`<div class="upload-preview"><img src="${item.previewUrl}" alt=""><span title="${esc(item.file.name)}">${esc(item.file.name)}</span><button type="button" data-remove-image="${item.id}" aria-label="${t("removeImage")}">×</button></div>`).join("");
  $$("#uploadPreviews [data-remove-image]").forEach(button=>button.onclick=()=>removeSelectedImage(Number(button.dataset.removeImage)));
  $("#imageFile").disabled=uploadInProgress;
  const submit=$("#uploadForm .btn[type=submit]");
  submit.disabled=uploadInProgress||count===0;
  if(!uploadInProgress)submit.textContent=t("uploadAdd");
}
function removeSelectedImage(id){
  if(uploadInProgress)return;
  const item=selectedImages.find(candidate=>candidate.id===id);
  if(item)URL.revokeObjectURL(item.previewUrl);
  selectedImages=selectedImages.filter(candidate=>candidate.id!==id);
  renderSelectedImages();
}
function clearSelectedImages(){
  selectedImages.forEach(item=>URL.revokeObjectURL(item.previewUrl));
  selectedImages=[];
  $("#imageFile").value="";
  renderSelectedImages();
}
function addSelectedImages(files){
  const existing=new Set(selectedImages.map(item=>`${item.file.name}:${item.file.size}:${item.file.lastModified}`));
  [...files].forEach(file=>{
    const key=`${file.name}:${file.size}:${file.lastModified}`;
    if(!(file.type.startsWith("image/")||/\.(png|jpe?g|gif)$/i.test(file.name))||existing.has(key))return;
    existing.add(key);
    selectedImages.push({id:nextSelectedImageId++,file,previewUrl:URL.createObjectURL(file)});
  });
  $("#imageFile").value="";
  renderSelectedImages();
}
function loadLocalImage(file){return new Promise((resolve,reject)=>{const url=URL.createObjectURL(file);const image=new Image();image.onload=()=>{URL.revokeObjectURL(url);resolve(image)};image.onerror=()=>{URL.revokeObjectURL(url);reject(new Error(t("imageReadError")))};image.src=url})}
async function prepareUploadImage(file){
  const image=await loadLocalImage(file);
  const canvas=document.createElement("canvas");
  canvas.width=480;
  canvas.height=480;
  const context=canvas.getContext("2d",{alpha:false});
  const scale=Math.max(480/image.naturalWidth,480/image.naturalHeight);
  const width=image.naturalWidth*scale;
  const height=image.naturalHeight*scale;
  context.imageSmoothingEnabled=true;
  context.imageSmoothingQuality="high";
  context.drawImage(image,(480-width)/2,(480-height)/2,width,height);
  const rgba=context.getImageData(0,0,480,480).data;
  const RAW_MAGIC="SDR5";
  const raw=new Uint8Array(8+480*480*2);
  for(let i=0;i<RAW_MAGIC.length;i++)raw[i]=RAW_MAGIC.charCodeAt(i);
  const view=new DataView(raw.buffer);
  view.setUint16(4,480,true);
  view.setUint16(6,480,true);
  for(let pixel=0;pixel<480*480;pixel++){
    const offset=pixel*4;
    const rgb565=((rgba[offset]>>3)<<11)|((rgba[offset+1]>>2)<<5)|(rgba[offset+2]>>3);
    view.setUint16(8+pixel*2,rgb565,true);
  }
  const blob=new Blob([raw],{type:"application/octet-stream"});
  const stem=(file.name||"image").replace(/\.[^.]+$/,"").replace(/[^a-zA-Z0-9_-]+/g,"-").slice(0,28)||"image";
  return{blob,name:stem+".rgb565",width:canvas.width,height:canvas.height};
}
async function hydrateRawThumbnails(){
  await Promise.all($$("canvas[data-raw]").map(async canvas=>{
    try{
      const response=await fetch(canvas.dataset.raw);
      if(!response.ok)throw new Error();
      const bytes=new Uint8Array(await response.arrayBuffer());
      if(bytes.length!==8+480*480*2)throw new Error();
      const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
      const context=canvas.getContext("2d",{alpha:false});
      const preview=context.createImageData(480,480);
      for(let pixel=0;pixel<480*480;pixel++){
        const rgb565=view.getUint16(8+pixel*2,true);
        const offset=pixel*4;
        preview.data[offset]=Math.round(((rgb565>>11)&31)*255/31);
        preview.data[offset+1]=Math.round(((rgb565>>5)&63)*255/63);
        preview.data[offset+2]=Math.round((rgb565&31)*255/31);
        preview.data[offset+3]=255;
      }
      context.putImageData(preview,0,0);
    }catch(e){canvas.replaceWith(Object.assign(document.createElement("div"),{className:"thumb",textContent:"RGB"}))}
  }));
}
async function loadStatus(){
  const requestSequence=++llmStatusRequestSequence;
  const nextDevice=await api("/api/status");
  if(requestSequence!==llmStatusRequestSequence)return;
  device=nextDevice;
  applyLanguage(device.language);
  $("#ipAddress").textContent=device.mode==="provisioning"?"192.168.4.1":device.url.replace(/^https?:\/\//,"").replace(/\/$/,"");
  renderDeviceStatus();
  $("#pulse").classList.toggle("online",device.mode!=="provisioning");
  $("#provisionPanel").hidden=device.mode!=="provisioning";
  $("#controller").hidden=device.mode==="provisioning";
  $("#brightness").value=device.brightness;$("#brightnessValue").textContent=device.brightness+"%";
  $("#storageText").textContent=formatBytes(device.storageUsed)+" / "+formatBytes(device.storageTotal);
  $("#storageMeter").style.width=Math.min(100,(device.storageUsed/device.storageTotal)*100||0)+"%";
  const sdClock=Number(device.sdClockHz)>0?` · ${Math.round(Number(device.sdClockHz)/1000000)} MHz`:"";
  $("#sdStorage").textContent=device.sdMounted?`${t("sdStorageLabel")} · ${t("sdReady")} · ${formatBytes(device.sdUsed)} / ${formatBytes(device.sdTotal)}${sdClock}`:sdIssueText(device.sdStatus,device.sdFilesystem);
  if(device.mode!=="provisioning"){hydrateDisplaySettings();if(!llmNarrationTogglePending)hydrateImageNarrationSetting();if(!llmFormDirty&&!llmTestObserverPromise&&!llmTestPostPending)hydrateLlmSettings();resumeRememberedLlmTest();await loadPages()}
}
const isSdPath=path=>String(path||"").startsWith("/sd/");
const isAnimatedPath=path=>/\.gif$/i.test(String(path||""));
const mediaUrlFor=path=>isSdPath(path)?`/sdmedia?path=${encodeURIComponent(path)}`:`/media/${encodeURIComponent(String(path).replace(/^\/img\//,""))}`;
async function loadPages(){
  const data=await api("/api/pages");
  const list=$("#pageList");
  const system=`<div class="page-row system-row"><div class="page-index">QR</div><div class="thumb">↓</div><div class="page-copy"><b>${t("systemEntryTitle")}</b><span>${t("systemEntryHint")}</span></div></div>`;
  const rows=data.pages.map((p,i)=>{
    const sdBacked=isSdPath(p.path);
    const mediaUrl=mediaUrlFor(p.path);
    const media=p.type==="image"?(sdBacked?`<div class="thumb sd-fallback">TF</div>`:(p.path.endsWith(".rgb565")?`<canvas class="thumb" width="480" height="480" data-raw="${mediaUrl}"></canvas>`:`<img class="thumb" src="${mediaUrl}" alt="">`)):`<div class="thumb" style="background:#${p.background.toString(16).padStart(6,"0")};color:#${p.foreground.toString(16).padStart(6,"0")}">Aa</div>`;
    const cachedNarration=device.imageNarrationEnabled!==false?String(p.narration||"").trim():"";
    const description=p.type==="image"?(cachedNarration||p.path):(p.text||"").replace(/\s+/g," ");
    const kind=p.type==="image"?(isAnimatedPath(p.path)?"animationPage":"imagePage"):"textPage";
    const badge=isSdPath(p.path)?`<span class="badge">${t("sdStorageLabel")}</span>`:"";
    return `<div class="page-row"><div class="page-index">${String(i+1).padStart(2,"0")}</div>${media}<div class="page-copy"><b>${t(kind)}${badge}</b><span>${esc(description)}</span></div><div class="row-actions"><button class="icon-btn" onclick="movePage(${p.id},-1)" aria-label="${t("moveUp")}">↑</button><button class="icon-btn" onclick="movePage(${p.id},1)" aria-label="${t("moveDown")}">↓</button><button class="icon-btn delete" onclick="deletePage(${p.id})" aria-label="${t("delete")}">×</button></div></div>`
  }).join("");
  list.innerHTML=system+(rows||`<div class="empty">${t("emptyPages")}</div>`);
  hydrateRawThumbnails();
}
let sdFiles=null;
let sdCardStatus="missing";
let sdCardFilesystem="unknown";
function renderSdMedia(){
  const state=$("#sdState");
  const list=$("#sdList");
  if(sdFiles===null){state.textContent="";list.innerHTML="";return}
  if(sdFiles===false){state.textContent=sdIssueText(sdCardStatus,sdCardFilesystem);list.innerHTML="";return}
  state.textContent=sdFiles.length?`${t("sdReady")} · ${sdFiles.length}`:"";
  list.innerHTML=sdFiles.map(file=>{
    const url=mediaUrlFor(file.path);
    // The device serves previews on the same single-threaded loop that drives
    // playback, so large originals are listed by name instead of streamed.
    const heavy=Number(file.size)>1500000;
    const label=file.kind==="raw"?"RGB565":String(file.name).split(".").pop().toUpperCase();
    const preview=(file.kind==="raw"||heavy)?`<div class="sd-fallback">${esc(label)}</div>`:`<img loading="lazy" src="${url}" alt="">`;
    const badge=file.kind==="animation"?`<span class="badge">${t("animatedBadge")}</span>`:"";
    return `<button class="sd-item" type="button" data-path="${esc(file.path)}">${preview}<span class="sd-meta"><b>${esc(file.name)}${badge}</b><span>${formatBytes(file.size)}</span></span></button>`;
  }).join("")||`<div class="empty">${t("sdEmpty")}</div>`;
  $$("#sdList .sd-item").forEach(el=>el.onclick=()=>addSdPage(el.dataset.path));
}
async function loadSdMedia(){
  $("#sdState").textContent=t("sdScanning");
  try{
    const data=await api("/api/sd");
    sdCardStatus=data.status||(data.mounted?"ready":"missing");
    sdCardFilesystem=data.filesystem||"unknown";
    sdFiles=data.mounted?(data.files||[]):false;
  }catch(e){sdCardStatus="missing";sdCardFilesystem="unknown";sdFiles=false}
  renderSdMedia();
}
async function addSdPage(path){
  try{
    await api("/api/pages/sd",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({path})});
    toast(t("sdAdded"));
    await loadStatus();
  }catch(e){toast(e.message,true)}
}
async function scanWifi(){
  const button=$("#scanButton");button.disabled=true;button.textContent=t("scanning");
  try{
    const data=await api("/api/wifi/scan");
    $("#networkList").innerHTML=data.networks.map(n=>`<button class="network" data-ssid="${esc(n.ssid)}"><span>${esc(n.ssid)}</span><small>${n.rssi} dBm ${n.secure?"🔒":""}</small></button>`).join("")||`<p>${t("noNetworks")}</p>`;
    $$("#networkList .network").forEach(el=>el.onclick=()=>{$("#ssid").value=el.dataset.ssid;$("#password").focus()});
  }catch(e){toast(e.message,true)}finally{button.disabled=false;button.textContent=t("scanAgain")}
}
async function movePage(id,direction){try{await api("/api/pages/move",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({id,direction})});await loadPages()}catch(e){toast(e.message,true)}}
async function deletePage(id){if(!confirm(t("deleteConfirm")))return;try{await api("/api/pages/delete",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({id})});toast(t("pageDeleted"));await loadStatus()}catch(e){toast(e.message,true)}}
async function setLanguage(language){const previous=currentLanguage;if(language===previous)return;let saved=false;applyLanguage(language);$$("#languageSwitch button").forEach(button=>button.disabled=true);try{await api("/api/language",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({language})});device.language=language;saved=true;toast(t("languageSaved"))}catch(err){applyLanguage(previous);toast(err.message,true)}finally{$$("#languageSwitch button").forEach(button=>button.disabled=false)}if(saved&&device.mode!=="provisioning")loadPages().catch(err=>toast(err.message,true))}
async function setPageTransition(style){const previous=device.pageTransition==="slide"?"slide":"fade";if(style===previous)return;device.pageTransition=style;hydratePageTransition();$$("#pageTransitionSwitch button").forEach(button=>button.disabled=true);try{await api("/api/page-transition",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({pageTransition:style})});toast(t("pageTransitionSaved"))}catch(err){device.pageTransition=previous;hydratePageTransition();toast(err.message,true)}finally{$$("#pageTransitionSwitch button").forEach(button=>button.disabled=false)}}
applyLanguage(currentLanguage);
buildTimezoneOptions();
$$("nav button").forEach(b=>b.onclick=()=>{$$("nav button").forEach(x=>x.classList.remove("active"));$$(".view").forEach(x=>x.classList.remove("active"));b.classList.add("active");$("#"+b.dataset.view).classList.add("active")});
$("#scanButton").onclick=scanWifi;
$("#wifiForm").onsubmit=async e=>{e.preventDefault();try{await api("/api/wifi",{method:"POST",body:new FormData(e.target)});toast(t("wifiSaved"));setTimeout(()=>location.reload(),9000)}catch(err){toast(err.message,true)}};
$("#textForm").onsubmit=async e=>{e.preventDefault();const button=e.submitter;button.disabled=true;try{await api("/api/pages/text",{method:"POST",body:new FormData(e.target)});e.target.reset();toast(t("textAdded"));await loadStatus()}catch(err){toast(err.message,true)}finally{button.disabled=false}};
$("#imageFile").onchange=e=>addSelectedImages(e.target.files);
$("#uploadForm").onsubmit=async e=>{
  e.preventDefault();
  const button=e.submitter||$("#uploadForm .btn[type=submit]");
  const queue=[...selectedImages];
  if(!queue.length||uploadInProgress)return;
  uploadInProgress=true;
  renderSelectedImages();
  let uploaded=0;
  try{
    for(let index=0;index<queue.length;index++){
      const file=queue[index].file;
      const form=new FormData();
      button.textContent=tf("processingImage",{current:index+1,total:queue.length});
      // A GIF has to reach the device byte for byte: re-encoding it through a
      // canvas would flatten it to a single still frame.
      if(isAnimatedPath(file.name)||file.type==="image/gif"){
        if(file.size>2*1024*1024)throw new Error(`${file.name}: ${t("gifTooLarge")}`);
        const stem=(file.name||"animation").replace(/\.[^.]+$/," ").trim().replace(/[^a-zA-Z0-9_-]+/g,"-").slice(0,28)||"animation";
        form.append("image",file,stem+".gif");
      }else{
        const prepared=await prepareUploadImage(file);
        form.append("image",prepared.blob,prepared.name);
      }
      button.textContent=tf("uploading",{current:index+1,total:queue.length});
      await api("/api/upload",{method:"POST",body:form});
      uploaded++;
      URL.revokeObjectURL(queue[index].previewUrl);
      selectedImages=selectedImages.filter(item=>item.id!==queue[index].id);
      renderSelectedImages();
    }
    clearSelectedImages();
    toast(tf("uploadComplete",{count:uploaded}));
  }catch(err){
    toast(uploaded?`${err.message} (${uploaded}/${queue.length})`:err.message,true);
  }finally{
    uploadInProgress=false;
    renderSelectedImages();
    if(uploaded)await loadStatus();
  }
};
$("#useBrowserTime").onclick=()=>{const offset=Number($("#timezoneOffset").value);$("#deviceDateTime").value=dateTimeForOffset(Date.now()/1000,offset);$("#timeSyncNote").textContent=t("browserTimeFilled");refreshSettingsPreview()};
["#deviceDateTime","#showDateTime","#showWeather","#screenOffEnabled","#screenOffStart","#screenOffEnd"].forEach(selector=>$(selector).addEventListener("input",refreshSettingsPreview));
$("#displaySettingsForm").onsubmit=async e=>{e.preventDefault();const button=e.submitter;const offset=Number($("#timezoneOffset").value);const wallTime=Date.parse($("#deviceDateTime").value+"Z");if(!Number.isFinite(wallTime)){toast(t("invalidDate"),true);return}const epoch=Math.round(wallTime/1000-offset*60);const body=new URLSearchParams({timezoneOffsetMinutes:String(offset),epoch:String(epoch),showDateTime:$("#showDateTime").checked?"1":"0",showWeather:$("#showWeather").checked?"1":"0",screenOffEnabled:$("#screenOffEnabled").checked?"1":"0",screenOffStartMinutes:String(minutesFromTime($("#screenOffStart").value)),screenOffEndMinutes:String(minutesFromTime($("#screenOffEnd").value))});button.disabled=true;try{await api("/api/settings",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});toast(t("settingsSaved"));await loadStatus()}catch(err){toast(err.message,true)}finally{button.disabled=false}};
function setLlmFormBusy(busy){const form=$("#llmSettingsForm");form.setAttribute("aria-busy",String(busy));$$("#llmSettingsForm button,#llmSettingsForm input,#llmSettingsForm textarea").forEach(control=>control.disabled=busy)}
["#llmBaseUrl","#llmApiKey","#llmModel","#llmAllowInsecureHttp"].forEach(selector=>$(selector).addEventListener("input",markLlmSettingsChanged));
$("#llmNarrationPrompt").addEventListener("input",markLlmSettingsChanged);
$("#llmSettingsForm").onsubmit=async e=>{e.preventDefault();refreshLlmPromptCounter();if(!e.target.reportValidity())return;const body=new URLSearchParams({baseUrl:$("#llmBaseUrl").value.trim(),apiKey:$("#llmApiKey").value.trim(),model:$("#llmModel").value.trim(),narrationPrompt:$("#llmNarrationPrompt").value.trim(),allowInsecureHttp:$("#llmAllowInsecureHttp").checked?"1":"0",csrfToken:device.llmSettingsToken||""});setLlmFormBusy(true);try{await api("/api/llm",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});$("#llmApiKey").value="";toast(t("llmSaved"));llmFormHydrated=false;llmFormDirty=false;await loadStatus()}catch(err){toast(err.message,true)}finally{setLlmFormBusy(false)}};
$("#testLlmSettings").onclick=async()=>{const form=$("#llmSettingsForm");refreshLlmPromptCounter();if(!form.reportValidity())return;let requestToken="";try{requestToken=createLlmRequestToken()}catch(error){setLlmTestState("failed","llmTestInternal");toast(llmTestMessage("llmTestInternal"),true);return}llmTestMatchesCurrentForm=true;rememberPendingLlmRequestToken(requestToken);llmTestPostPending=true;const body=new URLSearchParams({baseUrl:$("#llmBaseUrl").value.trim(),apiKey:$("#llmApiKey").value.trim(),model:$("#llmModel").value.trim(),narrationPrompt:$("#llmNarrationPrompt").value.trim(),allowInsecureHttp:$("#llmAllowInsecureHttp").checked?"1":"0",requestToken,csrfToken:device.llmSettingsToken||""});llmTestResumed=false;setLlmTestActivity(true);setLlmTestState("queued","llmTestQueued");const postController=new AbortController();const postTimeout=setTimeout(()=>postController.abort(),8000);try{const result=await api("/api/llm/test",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body,signal:postController.signal});clearTimeout(postTimeout);llmTestPostPending=false;const testId=normalizeLlmTestId(result.id);if(!testId){await observeLlmTest("",requestToken,LLM_TEST_TOKEN_NOT_FOUND_GRACE_MS);return}rememberLlmTestId(testId);await observeLlmTest(testId,requestToken)}catch(err){clearTimeout(postTimeout);llmTestPostPending=false;if(err?.name==="AbortError"||err instanceof TypeError){await observeLlmTest("",requestToken,LLM_TEST_TOKEN_NOT_FOUND_GRACE_MS);return}clearLlmTestMarkers("",requestToken);const failure=llmTestStartError(err);setLlmTestState("failed",failure.messageKey,failure.httpStatus);toast(llmTestMessage(failure.messageKey,failure.httpStatus),true)}finally{clearTimeout(postTimeout);llmTestPostPending=false;if(!llmTestObserverPromise)setLlmTestActivity(false)}};
$("#imageNarrationEnabled").onchange=async e=>{const input=e.currentTarget;const previous=device.imageNarrationEnabled!==false;const enabled=input.checked;llmNarrationTogglePending=true;setLlmFormBusy(true);try{await api("/api/llm/narration",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({enabled:enabled?"1":"0",csrfToken:device.llmSettingsToken||""})})}catch(err){input.checked=previous;toast(err.message,true);llmNarrationTogglePending=false;setLlmFormBusy(false);return}device.imageNarrationEnabled=enabled;const statusRefresh=loadStatus();llmNarrationTogglePending=false;toast(t(enabled?"imageNarrationEnabledOn":"imageNarrationEnabledOff"));try{await statusRefresh}catch(err){loadPages().catch(pageError=>toast(pageError.message,true))}finally{setLlmFormBusy(false)}};
$("#clearLlmSettings").onclick=async()=>{if(!confirm(t("clearLlmConfirm")))return;setLlmFormBusy(true);try{await api("/api/llm",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({clear:"1",csrfToken:device.llmSettingsToken||""})});device.llmBaseUrl="";device.llmModel="";device.llmApiKeyConfigured=false;device.llmConfigured=false;llmFormHydrated=false;llmFormDirty=false;llmTestMatchesCurrentForm=false;llmTestResumed=false;setLlmTestState("","");toast(t("llmCleared"));await loadStatus()}catch(err){toast(err.message,true)}finally{setLlmFormBusy(false)}};
$("#resetAllNarrations").onclick=async()=>{
  if($("#resetAllNarrations").disabled||!confirm(t("resetNarrationsConfirm")))return;
  setLlmFormBusy(true);
  try{
    await api("/api/llm/narration/reset",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({confirmed:"1",csrfToken:device.llmSettingsToken||""})});
    await loadPages();
    toast(t("narrationsReset"));
  }catch(err){toast(err.message,true)}finally{setLlmFormBusy(false)}
};
let brightnessThrottle=0;
function previewBrightness(value){fetch("/api/brightness",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({value,preview:"1"})}).catch(()=>{})}
$("#brightness").oninput=e=>{
  $("#brightnessValue").textContent=e.target.value+"%";
  if(brightnessThrottle)return;
  brightnessThrottle=setTimeout(()=>{brightnessThrottle=0;previewBrightness($("#brightness").value)},110);
};
$("#brightness").onchange=async e=>{try{await api("/api/brightness",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({value:e.target.value})});toast(t("brightnessSaved"))}catch(err){toast(err.message,true)}};
$("#sdRescan").onclick=async()=>{const button=$("#sdRescan");button.disabled=true;$("#sdState").textContent=t("sdScanning");try{await api("/api/sd/rescan",{method:"POST"})}catch(e){}finally{button.disabled=false;await loadStatus();await loadSdMedia()}};
$$("#languageSwitch button").forEach(button=>button.onclick=()=>setLanguage(button.dataset.language));
$$("#pageTransitionSwitch button").forEach(button=>button.onclick=()=>setPageTransition(button.dataset.transition));
$("#resetWifi").onclick=async()=>{if(!confirm(t("resetWifiConfirm")))return;try{await api("/api/wifi/reset",{method:"POST"});toast(t("restarting"))}catch(e){toast(e.message,true)}};
loadStatus().catch(e=>toast(e.message,true));
</script>
</body>
</html>
)HTML";
