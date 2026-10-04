// Translation Magic Window：網頁漫畫（注入到分頁裡的腳本）
//
// - 找出頁面上的漫畫圖（<img>、畫好的 <canvas>），只挑「主要內容欄」：最大的一群同寬的圖，
//   旁邊的廣告、縮圖、按鈕不翻（docs/proposal-speed-and-web-manga.md 第二部分第 9 點）
// - 延遲載入的圖不等它載入：直接讀 data-src 這類屬性裡的真正網址先翻，捲到之前就好了
// - 依離畫面多近決定順序，每次送出前重新排；捲動、換章時新出現的圖自動補上
// - 譯文放在緊跟著圖片的一個元素裡（和圖片在同一個捲動容器，捲動時由瀏覽器一起移動，不會落後），
//   內容在 shadow DOM 裡，網頁的 CSS 碰不到；不改原本的圖
// - 進度依圖片網址記：閱讀器把捲出畫面的圖拿掉、捲回來再建一個新的，總數不會變少，
//   有網址的圖照樣在背景翻完；建回來時直接蓋上譯文
// - 失敗會記下原因（控制面板列出來）；翻譯引擎可以在控制面板換（主程式記得用過的引擎）
// - 控制視窗（popup.js）用訊息問狀態、切換原文／譯文、重試、停止
(() => {
  if (window.__tmwWebManga) {
    return; // 已經在這一頁執行了（例如自動翻譯之後又按了「翻譯這一頁」）
  }

  const MIN_WIDTH = 200; // 顯示寬度比這小的不是漫畫頁
  const MIN_NATURAL = 300; // 原圖比這小的不是漫畫頁（也排除延遲載入的佔位圖）
  // 同時交給背景的張數：主程式一頁一頁做 OCR、好幾頁同時翻譯（core/web_pipeline_worker.h），
  // 送多一點它才不會閒著；先後順序還是這裡決定（離畫面近的先送）
  const IN_FLIGHT = 6;
  // 延遲載入的寫法各家不同，真正的網址常放在這些屬性裡
  const LAZY_ATTRIBUTES = ["data-src", "data-original", "data-lazy-src", "data-lazy", "data-url",
                           "data-echo", "data-full", "data-srcset", "data-lazy-srcset"];

  const STYLE = `
    :host { all: initial; }
    .layer { position: absolute; inset: 0; container-type: size; overflow: hidden; }
    .item { position: absolute; display: flex; align-items: center; justify-content: center;
            box-sizing: border-box; overflow: hidden; }
    .patch { position: absolute; inset: 0; width: 100%; height: 100%; }
    .text { position: relative; margin: 0; padding: 0; line-height: 1.15; text-align: center;
            font-family: "Microsoft JhengHei", "Noto Sans TC", sans-serif; font-weight: 600;
            overflow-wrap: anywhere; white-space: pre-wrap; }
    .vertical { writing-mode: vertical-rl; text-orientation: mixed; }
    rt { font-size: 0.45em; }
    .hidden { display: none; }`;

  // 控制面板：浮在網頁上，可以拖動、調透明度、縮小；位置和透明度記在 chrome.storage
  const panelHost = document.createElement("tmw-panel");
  panelHost.style.cssText = "all:initial;position:fixed;left:0;top:0;width:0;height:0;z-index:2147483647;";
  const panelRoot = panelHost.attachShadow({ mode: "closed" });
  panelRoot.innerHTML = `<style>
    .panel { position: fixed; width: 250px; max-width: calc(100vw - 16px); box-sizing: border-box; border-radius: 8px; background: #202124; color: #f1f1f1;
             font: 12px/1.5 "Microsoft JhengHei", "Noto Sans TC", sans-serif; box-shadow: 0 4px 16px rgba(0,0,0,.35);
             user-select: none; }
    .head { display: flex; align-items: center; gap: 6px; padding: 6px 8px; cursor: move; border-bottom: 1px solid #3c3c3c; }
    .head strong { flex: 1; font-size: 12px; }
    .icon { width: 20px; height: 20px; border: 0; border-radius: 4px; background: transparent; color: #ccc;
            font: 14px/1 sans-serif; cursor: pointer; }
    .icon:hover { background: #3c3c3c; color: #fff; }
    .body { padding: 8px; display: grid; grid-template-columns: minmax(0, 1fr); gap: 7px;
            overflow-wrap: anywhere; }
    .body > * { min-width: 0; }
    .bar { height: 4px; border-radius: 2px; background: #3c3c3c; overflow: hidden; }
    .bar > div { height: 100%; width: 0; background: #63a4e8; transition: width .3s; }
    .buttons { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; }
    button.action { padding: 4px 6px; border: 1px solid #4a4a4a; border-radius: 5px; background: #2b2c2f;
                    color: #f1f1f1; font: inherit; cursor: pointer; }
    button.action:hover:not(:disabled) { border-color: #63a4e8; }
    button.action:disabled { opacity: .45; cursor: default; }
    .row { display: flex; align-items: center; gap: 6px; min-width: 0; }
    .head strong { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .row input[type=range] { flex: 1; }
    label { display: flex; align-items: center; gap: 5px; cursor: pointer; }
    .muted { color: #a8a8a8; }
    select { flex: 1; min-width: 0; width: 100%; text-overflow: ellipsis; padding: 2px 4px; border: 1px solid #4a4a4a; border-radius: 4px;
             background: #2b2c2f; color: #f1f1f1; font: inherit; }
    .reasons { display: grid; gap: 2px; color: #f6c35b; font-size: 11px; }
    .notice { color: #9ecbff; font-size: 11px; }
    .mini .body { display: none; }
    .mini { width: auto; }
    .mini .head { border-bottom: 0; }
    .hidden { display: none !important; }
  </style>
  <div class="panel">
    <div class="head"><strong id="title">網頁漫畫翻譯</strong>
      <button class="icon" id="minimize" title="縮小／展開">–</button>
      <button class="icon" id="close" title="關閉面板（翻譯繼續）">×</button></div>
    <div class="body">
      <div><span id="status">準備中…</span> <span id="detail" class="muted"></span></div>
      <div class="bar"><div id="progress"></div></div>
      <div id="reasons" class="reasons"></div>
      <div id="notice" class="notice"></div>
      <div class="row"><span>翻譯</span><select id="engine" title="主程式用過的翻譯引擎"></select></div>
      <div class="buttons">
        <button class="action" id="toggle">顯示原文</button>
        <button class="action" id="retry">重試失敗的圖</button>
        <button class="action" id="preload">載入整頁</button>
        <button class="action" id="stop">停止並移除</button>
      </div>
      <label><input type="checkbox" id="autoPreload"> 開始時先載入整頁的圖</label>
      <div class="row"><span>透明度</span><input type="range" id="opacity" min="20" max="100" step="5">
        <span id="opacityText" class="muted"></span></div>
    </div>
  </div>`;
  document.documentElement.appendChild(panelHost);
  const panel = panelRoot.querySelector(".panel");
  const $panel = (id) => panelRoot.getElementById(id);
  const panelState = { left: null, top: 24, opacity: 95, minimized: false, hidden: false, autoPreload: true };

  function applyPanel() {
    panel.classList.toggle("mini", panelState.minimized);
    panel.classList.toggle("hidden", panelState.hidden);
    panel.style.opacity = String(panelState.opacity / 100);
    const width = panel.offsetWidth || 230;
    const left = panelState.left ?? innerWidth - width - 24;
    panel.style.left = `${Math.max(0, Math.min(left, innerWidth - width))}px`;
    panel.style.top = `${Math.max(0, Math.min(panelState.top, innerHeight - 32))}px`;
    $panel("opacity").value = String(panelState.opacity);
    $panel("opacityText").textContent = `${panelState.opacity}%`;
    $panel("minimize").textContent = panelState.minimized ? "+" : "–";
    $panel("autoPreload").checked = panelState.autoPreload;
  }

  function savePanel() {
    const { left, top, opacity, minimized, autoPreload } = panelState;
    chrome.storage?.local.set({ panel: { left, top, opacity, minimized, autoPreload } }).catch(() => {});
  }

  // 拖動標題列
  $panel("title").parentElement.addEventListener("pointerdown", (event) => {
    if (event.target.closest("button")) {
      return;
    }
    const head = event.currentTarget;
    head.setPointerCapture(event.pointerId);
    const rect = panel.getBoundingClientRect();
    const dx = event.clientX - rect.left;
    const dy = event.clientY - rect.top;
    const move = (e) => {
      panelState.left = e.clientX - dx;
      panelState.top = e.clientY - dy;
      applyPanel();
    };
    const up = () => {
      head.removeEventListener("pointermove", move);
      head.removeEventListener("pointerup", up);
      savePanel();
    };
    head.addEventListener("pointermove", move);
    head.addEventListener("pointerup", up);
  });
  $panel("opacity").addEventListener("input", () => {
    panelState.opacity = Number($panel("opacity").value);
    applyPanel();
  });
  $panel("opacity").addEventListener("change", savePanel);
  $panel("minimize").addEventListener("click", () => {
    panelState.minimized = !panelState.minimized;
    applyPanel();
    savePanel();
  });
  $panel("close").addEventListener("click", () => {
    panelState.hidden = true; // 只關面板，翻譯繼續；控制視窗可以再叫出來
    applyPanel();
  });
  $panel("autoPreload").addEventListener("change", () => {
    panelState.autoPreload = $panel("autoPreload").checked;
    savePanel();
  });
  addEventListener("resize", applyPanel, { passive: true });
  applyPanel();

  const pages = new Map(); // 元素 → { source, result, anchor, layer }（譯文畫在哪）
  // 這一章的每一張圖（依網址）：state 是 queued／working／waiting（等圖片載入）／done／failed，
  // reason 是失敗的原因。元素被拿掉也留著：總數不會變少，有網址的照樣翻完
  let sources = new Map();
  const doneBySource = new Map(); // 翻好的結果（換章後回來、元素重建時直接用）；失敗的不記
  const canvasIds = new WeakMap(); // canvas 沒有網址：每一個給一個編號
  let canvasSerial = 0;
  let lastNotice = ""; // 主程式沒有用首選引擎時的說明（例如改用 Google）
  let queue = []; // 等著送出的圖片網址
  let running = 0;
  let visible = true;
  let fatal = "";

  // ---------------------------------------------------------------- 找圖

  function isHttp(url) {
    return /^https?:/i.test(url || "");
  }

  // srcset 裡最寬的那張
  function largestInSrcset(srcset) {
    let best = "";
    let bestWidth = -1;
    for (const part of (srcset || "").split(",")) {
      const [url, descriptor = ""] = part.trim().split(/\s+/);
      const width = parseFloat(descriptor) || 0;
      if (url && width > bestWidth) {
        best = url;
        bestWidth = width;
      }
    }
    return best;
  }

  function absolute(url) {
    try {
      return new URL(url, location.href).href;
    } catch {
      return "";
    }
  }

  function loaded(img) {
    return img.complete && img.naturalWidth >= MIN_NATURAL && img.naturalHeight >= MIN_NATURAL;
  }

  // 還沒載入的圖：從延遲載入的屬性找真正的網址
  function lazyUrl(img) {
    for (const name of LAZY_ATTRIBUTES) {
      const value = img.getAttribute(name);
      if (!value) {
        continue;
      }
      const url = absolute(name.includes("srcset") ? largestInSrcset(value) : value.trim());
      if (isHttp(url)) {
        return url;
      }
    }
    const fromSrcset = absolute(largestInSrcset(img.getAttribute("srcset")));
    if (isHttp(fromSrcset)) {
      return fromSrcset;
    }
    // loading="lazy" 的圖 src 就是真的網址，只是瀏覽器還沒下載
    return isHttp(img.src) && img.loading === "lazy" ? img.src : "";
  }

  function sourceOf(element) {
    if (!(element instanceof HTMLImageElement)) {
      if (!canvasIds.has(element)) {
        canvasIds.set(element, ++canvasSerial);
      }
      return `canvas:${canvasIds.get(element)}`;
    }
    return loaded(element) ? element.currentSrc || element.src : lazyUrl(element);
  }

  function isCandidate(element) {
    if (element.closest("tmw-overlay")) {
      return false;
    }
    const rect = element.getBoundingClientRect();
    if (rect.width < MIN_WIDTH) {
      return false;
    }
    if (element instanceof HTMLImageElement) {
      if (!loaded(element) && !lazyUrl(element)) {
        return false; // 小圖示，或延遲載入但找不到網址（載入後會再掃一次）
      }
    } else if (element.width < MIN_NATURAL || element.height < MIN_NATURAL || rect.height < MIN_WIDTH) {
      return false;
    }
    // 連到別的網站的圖多半是廣告
    const link = element.closest("a[href]");
    if (link) {
      try {
        if (new URL(link.href, location.href).origin !== location.origin) {
          return false;
        }
      } catch {
        // 不是網址：不管它
      }
    }
    return true;
  }

  // 主要內容欄：依顯示寬度分組，面積總和最大的那一組；寬度和它差不多的都算。
  // 還沒載入的圖高度可能是 0，用寬度的 1.4 倍估計（漫畫頁的比例）
  function mainColumn(elements) {
    const groups = new Map();
    for (const element of elements) {
      const rect = element.getBoundingClientRect();
      const key = Math.round(rect.width / 20);
      const height = rect.height >= MIN_WIDTH ? rect.height : rect.width * 1.4;
      groups.set(key, (groups.get(key) || 0) + rect.width * height);
    }
    let best = null;
    for (const [key, area] of groups) {
      if (best === null || area > groups.get(best)) {
        best = key;
      }
    }
    const width = best * 20;
    return elements.filter((element) => {
      const w = element.getBoundingClientRect().width;
      return w >= width * 0.8 && w <= width * 1.25;
    });
  }

  function distanceToViewport(element) {
    const rect = element.getBoundingClientRect();
    if (rect.bottom < 0) {
      return -rect.bottom * 2 + 1; // 在上面（已經看過了）：往下看的機會比較大，排後面一點
    }
    if (rect.top > innerHeight) {
      return rect.top - innerHeight;
    }
    return 0;
  }

  function readable(element) {
    return !(element instanceof HTMLImageElement) || loaded(element);
  }

  function track(source) {
    const known = doneBySource.get(source);
    const entry = known ? { state: "done", result: known, reason: "" } : { state: "queued", reason: "" };
    sources.set(source, entry);
    if (!known) {
      queue.push(source);
    }
    return entry;
  }

  function scan() {
    const elements = [...document.images, ...document.querySelectorAll("canvas")].filter(isCandidate);
    for (const element of mainColumn(elements)) {
      const source = sourceOf(element);
      const page = pages.get(element);
      const entry = sources.get(source) || track(source);
      entry.element = element; // 送出的先後依這個元素離畫面多近
      if (entry.state === "waiting" && readable(element)) {
        entry.state = "queued"; // 剛剛抓不到、現在載入了：從頁面讀
        queue.push(source);
      }
      if (page && page.source === source) {
        continue;
      }
      if (page) {
        removeOverlay(page); // 圖換了（換章、延遲載入換成另一張）
      }
      const fresh = { source };
      pages.set(element, fresh);
      if (entry.state === "done") {
        fresh.result = entry.result; // 翻過了（閱讀器把這張拿掉又建回來）：直接蓋回去
        showOverlay(element, fresh);
      }
    }
    pump();
    updateBadge();
  }

  let scanTimer = 0;
  function scheduleScan() {
    clearTimeout(scanTimer);
    scanTimer = setTimeout(scan, 200);
  }

  // ---------------------------------------------------------------- 送去翻譯

  function readInPage(element) {
    // 頁面本身讀得到像素時（同網域、有 CORS、或沒被污染的 canvas）直接讀
    const width = element instanceof HTMLImageElement ? element.naturalWidth : element.width;
    const height = element instanceof HTMLImageElement ? element.naturalHeight : element.height;
    const scale = Math.min(1, Math.sqrt(4_000_000 / (width * height)));
    const canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.round(width * scale));
    canvas.height = Math.max(1, Math.round(height * scale));
    const context = canvas.getContext("2d", { willReadFrequently: true });
    context.drawImage(element, 0, 0, canvas.width, canvas.height);
    const data = context.getImageData(0, 0, canvas.width, canvas.height).data; // 被污染時丟出 SecurityError
    let binary = "";
    for (let i = 0; i < data.length; i += 0x8000) {
      binary += String.fromCharCode.apply(null, data.subarray(i, i + 0x8000));
    }
    return { width: canvas.width, height: canvas.height, pixels: btoa(binary) };
  }

  // 叫網頁現在就載入這張圖（不必等使用者捲到）：延遲載入的網址搬到 src，lazy 改成立刻載入
  function forceLoad(element, source) {
    if (!(element instanceof HTMLImageElement) || !element.isConnected) {
      return;
    }
    element.loading = "eager";
    if (isHttp(source) && element.currentSrc !== source && !loaded(element)) {
      element.src = source;
    }
  }

  async function sendInPage(element) {
    try {
      return await chrome.runtime.sendMessage({ kind: "translate", image: readInPage(element) });
    } catch (error) {
      return { type: "error", message: error?.name === "SecurityError" ? "page-protected" : String(error?.message || error) };
    }
  }

  // 失敗的原因（給人看）
  function reasonOf(reply) {
    if (!reply) {
      return "沒有回應";
    }
    if (reply.type === "result" && reply.error) {
      return `翻譯失敗：${reply.error}`; // 主程式的說明，例如「被限流或額度用完：HTTP 429」
    }
    const message = reply.message || "";
    const known = {
      "host-not-installed": "找不到 Translation Magic Window（請先安裝）",
      "app-unavailable": "Translation Magic Window 沒有回應",
      "page-protected": "網頁不讓讀取這張圖（跨網域保護）",
      disconnected: "和主程式的連線斷了",
    };
    if (known[message]) {
      return known[message];
    }
    if (reply.fetchFailed) {
      return `抓不到圖片（${message}）`;
    }
    if (/size|pixels/.test(message)) {
      return `圖片格式不對（${message}）`;
    }
    return message || "不明的錯誤";
  }

  async function process(source) {
    const entry = sources.get(source);
    if (!entry) {
      return;
    }
    entry.state = "working";
    updateBadge();
    const element = entry.element;
    let reply;
    try {
      if (isHttp(source)) {
        reply = await chrome.runtime.sendMessage({ kind: "translate", url: source, page: location.href });
        if (reply?.fetchFailed) {
          if (!element?.isConnected || !readable(element)) {
            // 背景抓不到、頁面也還沒載入：叫網頁先載入它，載入後 scan 會再排
            entry.state = "waiting";
            entry.reason = `抓不到圖片（${reply.message}），等網頁載入`;
            forceLoad(element, source);
            return;
          }
          reply = await sendInPage(element);
        }
      } else if (element?.isConnected) {
        reply = await sendInPage(element);
      } else {
        entry.state = "waiting"; // canvas 被拿掉了：建回來時再讀
        return;
      }
    } catch (error) {
      reply = { type: "error", message: String(error?.message || error) };
    }
    if (sources.get(source) !== entry) {
      return; // 換章或停止了
    }
    if (reply?.message === "restarted") {
      entry.state = "queued"; // 主程式改了設定、重建工作佇列：再送一次
      queue.push(source);
      return;
    }
    if (reply?.notice) {
      lastNotice = reply.notice;
    }
    if (reply?.type === "result" && !reply.error) {
      entry.state = "done";
      entry.result = reply;
      entry.reason = "";
      doneBySource.set(source, reply);
      for (const [shown, page] of pages) {
        if (page.source === source) {
          page.result = reply;
          showOverlay(shown, page);
        }
      }
      return;
    }
    entry.state = "failed";
    entry.reason = reasonOf(reply);
    if (reply?.message === "host-not-installed" || reply?.message === "app-unavailable") {
      fatal = reply.message;
    }
  }

  function pump() {
    if (running >= IN_FLIGHT || fatal) {
      return;
    }
    // 每次都依「現在」離畫面多近重新排：使用者捲到哪就先翻哪；元素被拿掉的排最後
    queue = [...new Set(queue)].filter((source) => sources.get(source)?.state === "queued");
    const distance = (source) => {
      const element = sources.get(source)?.element;
      return element?.isConnected ? distanceToViewport(element) : Number.MAX_SAFE_INTEGER;
    };
    queue.sort((a, b) => distance(a) - distance(b));
    while (running < IN_FLIGHT && queue.length > 0) {
      const source = queue.shift();
      running += 1;
      process(source).finally(() => {
        running -= 1;
        updateBadge();
        pump();
      });
    }
  }

  // ---------------------------------------------------------------- 畫譯文

  function textWithRuby(text, ruby) {
    const characters = [...text];
    const fragment = document.createDocumentFragment();
    const sorted = [...(ruby || [])].sort((a, b) => a.start - b.start);
    let at = 0;
    for (const annotation of sorted) {
      if (annotation.start < at || annotation.start + annotation.length > characters.length) {
        continue;
      }
      fragment.append(characters.slice(at, annotation.start).join(""));
      const node = document.createElement("ruby");
      node.append(characters.slice(annotation.start, annotation.start + annotation.length).join(""));
      const reading = document.createElement("rt");
      reading.textContent = annotation.text;
      node.append(reading);
      fragment.append(node);
      at = annotation.start + annotation.length;
    }
    fragment.append(characters.slice(at).join(""));
    return fragment;
  }

  function outlineShadow(color) {
    const d = "0.07em";
    return [
      `${d} 0 ${color}`, `-${d} 0 ${color}`, `0 ${d} ${color}`, `0 -${d} ${color}`,
      `${d} ${d} ${color}`, `-${d} ${d} ${color}`, `${d} -${d} ${color}`, `-${d} -${d} ${color}`,
    ].join(",");
  }

  function buildLayer(result) {
    const layer = document.createElement("div");
    layer.className = visible ? "layer" : "layer hidden";
    for (const item of result.items) {
      const [left, top, right, bottom] = item.rect;
      const box = document.createElement("div");
      box.className = "item";
      box.style.left = `${(left / result.width) * 100}%`;
      box.style.top = `${(top / result.height) * 100}%`;
      box.style.width = `${((right - left) / result.width) * 100}%`;
      box.style.height = `${((bottom - top) / result.height) * 100}%`;
      if (item.patch) {
        const patch = document.createElement("img");
        patch.className = "patch";
        patch.src = `data:image/png;base64,${item.patch}`;
        box.append(patch);
      } else {
        box.style.background = item.background;
      }
      const text = document.createElement("p");
      text.className = item.vertical ? "text vertical" : "text";
      text.style.color = item.foreground;
      if (item.outline) {
        text.style.textShadow = outlineShadow(item.outline);
      }
      text.append(textWithRuby(item.text, item.ruby));
      box.append(text);
      layer.append(box);
    }
    return layer;
  }

  // 找放得進框的最大字級，換成「圖寬的百分比」（cqw）：之後圖怎麼縮放字都跟著縮放
  function fitText(page) {
    const width = page.layer.getBoundingClientRect().width;
    if (width <= 0) {
      return false;
    }
    const scale = width / page.result.width;
    [...page.layer.querySelectorAll(".item")].forEach((box, index) => {
      const item = page.result.items[index];
      const text = box.querySelector(".text");
      const limit = item.lineThickness > 0 ? item.lineThickness * scale * 1.05 : 200;
      let low = 6;
      let high = Math.max(low, Math.min(limit, 200));
      while (high - low > 0.5) {
        const middle = (low + high) / 2;
        text.style.fontSize = `${middle}px`;
        const fits = text.scrollWidth <= box.clientWidth + 1 && text.scrollHeight <= box.clientHeight + 1;
        if (fits) {
          low = middle;
        } else {
          high = middle;
        }
      }
      text.style.fontSize = `${(low / width) * 100}cqw`;
    });
    return true;
  }

  const IMPORTANT = ["position:absolute", "display:block", "margin:0", "padding:0", "border:0",
                     "transform:none", "pointer-events:none", "z-index:2147483647", "float:none",
                     "box-sizing:border-box", "background:transparent", "min-width:0", "min-height:0",
                     "max-width:none", "max-height:none"].map((rule) => `${rule} !important`).join(";");

  // CSS anchor positioning（Chrome／Edge 125 起）：譯文層「錨定」在圖片上，版面怎麼變、怎麼捲
  // 都由瀏覽器對齊，不必用 JS 算座標。不支援、或這張圖當不了錨點（例如定位基準不同）時改用 JS
  const ANCHORING = CSS.supports("anchor-name: --tmw-test");
  let anchorSerial = 0;

  function anchoredStyle(name) {
    return `${IMPORTANT};position-anchor:${name};left:anchor(left);top:anchor(top);` +
           "width:anchor-size(width);height:anchor-size(height)";
  }

  // 譯文放在緊跟著圖片的元素裡：和圖片在同一個捲動容器，捲動時一起動
  function showOverlay(element, page) {
    removeOverlay(page);
    const anchor = document.createElement("tmw-overlay");
    page.manual = !ANCHORING;
    if (page.manual) {
      anchor.style.cssText = `${IMPORTANT};left:0;top:0;width:0;height:0`;
    } else {
      page.anchorName ||= `--tmw-${++anchorSerial}`;
      element.style.setProperty("anchor-name", page.anchorName, "important");
      anchor.style.cssText = anchoredStyle(page.anchorName);
    }
    const root = anchor.attachShadow({ mode: "closed" });
    const style = document.createElement("style");
    style.textContent = STYLE;
    page.layer = buildLayer(page.result);
    root.append(style, page.layer);
    page.anchor = anchor;
    page.fitted = false;
    page.left = 0;
    page.top = 0;
    attach(element, page);
    align(element, page);
  }

  function attach(element, page) {
    const target = element.closest("picture") || element;
    if (target.nextSibling !== page.anchor) {
      target.after(page.anchor);
    }
  }

  function removeOverlay(page) {
    page.anchor?.remove();
    page.anchor = null;
    page.layer = null;
  }

  // 把譯文對齊到圖片上。只讀一次版面：錨點現在的位置減掉它目前的 left/top，就是它的原點
  function align(element, page) {
    if (!page.anchor) {
      return;
    }
    if (!page.anchor.isConnected) {
      attach(element, page); // 網頁的程式重繪時把它拿掉了
    }
    const rect = element.getBoundingClientRect();
    if (!page.manual) {
      if (element.style.getPropertyValue("anchor-name") !== page.anchorName) {
        element.style.setProperty("anchor-name", page.anchorName, "important"); // 網頁改了 style
      }
      const placed = page.anchor.getBoundingClientRect();
      const off = Math.abs(placed.left - rect.left) + Math.abs(placed.top - rect.top) +
                  Math.abs(placed.width - rect.width);
      if (off <= 2) {
        fitWhenNear(page, rect);
        return;
      }
      // 這張圖當不了錨點：改用 JS 對齊
      page.manual = true;
      page.anchor.style.cssText = `${IMPORTANT};left:0;top:0;width:0;height:0`;
      page.left = 0;
      page.top = 0;
    }
    const anchor = page.anchor.getBoundingClientRect();
    const left = rect.left - (anchor.left - page.left);
    const top = rect.top - (anchor.top - page.top);
    const changed = Math.abs(left - page.left) > 0.5 || Math.abs(top - page.top) > 0.5 ||
                    Math.abs(rect.width - anchor.width) > 0.5 || Math.abs(rect.height - anchor.height) > 0.5;
    if (changed) {
      page.left = left;
      page.top = top;
      page.anchor.style.setProperty("left", `${left}px`, "important");
      page.anchor.style.setProperty("top", `${top}px`, "important");
      page.anchor.style.setProperty("width", `${rect.width}px`, "important");
      page.anchor.style.setProperty("height", `${rect.height}px`, "important");
    }
    fitWhenNear(page, rect);
  }

  // 字級要在版面上量：快捲到時才做（離畫面很遠的先不量，省時間）
  function fitWhenNear(page, rect) {
    const near = rect.bottom > -innerHeight && rect.top < innerHeight * 2;
    if (!page.fitted && near && rect.width > 0 && rect.height > 0) {
      page.fitted = fitText(page);
    }
  }

  function alignNow() {
    for (const [element, page] of pages) {
      if (!element.isConnected) {
        // 閱讀器把這張拿掉了：進度和譯文還記在 sources／doneBySource，建回來時直接用
        removeOverlay(page);
        pages.delete(element);
        continue;
      }
      align(element, page);
    }
  }

  // 捲動、縮放：一個畫格最多對齊一次
  let aligning = false;
  function alignAll() {
    if (aligning) {
      return;
    }
    aligning = true;
    requestAnimationFrame(() => {
      aligning = false;
      alignNow();
    });
  }

  // ---------------------------------------------------------------- 狀態

  function counts() {
    const all = [...sources.values()];
    const done = all.filter((entry) => entry.state === "done").length;
    const failed = all.filter((entry) => entry.state === "failed");
    const waiting = all.filter((entry) => entry.state === "waiting");
    // 同樣的原因合在一起：「翻譯失敗：被限流或額度用完：HTTP 429（3 張）」
    const reasons = new Map();
    for (const entry of [...failed, ...waiting]) {
      reasons.set(entry.reason, (reasons.get(entry.reason) || 0) + 1);
    }
    return {
      total: all.length,
      done,
      failed: failed.length,
      waiting: waiting.length,
      reasons: [...reasons].map(([reason, count]) => `${reason}（${count} 張）`),
    };
  }

  function updateBadge() {
    const { total, done, failed, reasons } = counts();
    const finished = done + failed;
    let text;
    if (fatal === "host-not-installed") {
      text = "找不到 Translation Magic Window（請先安裝）";
    } else if (fatal === "app-unavailable") {
      text = "Translation Magic Window 沒有回應，請確認它已經開啟";
    } else if (preloading) {
      text = `載入整頁中…（找到 ${total} 張）`;
    } else if (total === 0) {
      text = "這一頁沒有找到漫畫圖片";
    } else if (finished < total) {
      text = `翻譯中 ${done} / ${total}`;
    } else {
      text = `完成 ${done} 張`;
    }
    $panel("status").textContent = text;
    const { waiting } = counts();
    $panel("detail").textContent = [failed ? `${failed} 張失敗` : "", waiting ? `${waiting} 張等圖片載入` : ""]
      .filter(Boolean).join("，");
    $panel("progress").style.width = total ? `${(finished / total) * 100}%` : "0";
    const list = $panel("reasons");
    list.replaceChildren(...reasons.map((reason) => {
      const line = document.createElement("div");
      line.textContent = reason;
      return line;
    }));
    $panel("notice").textContent = lastNotice;
    $panel("title").textContent = panelState.minimized && total ? `翻譯 ${done}/${total}` : "網頁漫畫翻譯";
    $panel("retry").disabled = failed === 0;
    $panel("preload").disabled = preloading;
    $panel("toggle").textContent = visible ? "顯示原文" : "顯示譯文";
  }

  // ---------------------------------------------------------------- 翻譯引擎

  // 主程式用過的翻譯引擎（core/engine_history.h），選一個就切換。現在用的那個選起來
  async function loadEngines() {
    let reply;
    try {
      reply = await chrome.runtime.sendMessage({ kind: "engines" });
    } catch {
      reply = null;
    }
    const select = $panel("engine");
    const engines = Array.isArray(reply?.engines) ? reply.engines : [];
    select.replaceChildren(...engines.map((engine) => {
      const option = document.createElement("option");
      option.value = String(engine.index);
      option.textContent = engine.label;
      option.selected = engine.label === reply.current;
      return option;
    }));
    select.disabled = engines.length === 0;
    select.title = reply?.current ? `現在用：${reply.current}` : "主程式用過的翻譯引擎";
  }

  async function chooseEngine() {
    const index = Number($panel("engine").value);
    $panel("engine").disabled = true;
    $panel("notice").textContent = "切換翻譯引擎中…";
    try {
      await chrome.runtime.sendMessage({ kind: "set-engine", index });
    } catch {
      // 連不上主程式：loadEngines 會把選單還原
    }
    lastNotice = "";
    await loadEngines();
    retry(); // 換了引擎：失敗的圖用新的引擎再翻一次
  }

  // ---------------------------------------------------------------- 開始

  // 版面變了（插進新的圖、上面的圖載入後變高）：馬上重新對齊（只有用 JS 對齊的譯文需要），再找新的圖
  // （MutationObserver 在畫面更新之前執行：在這裡對齊，連一個畫格的落後都沒有）
  const observer = new MutationObserver((mutations) => {
    for (const mutation of mutations) {
      if (mutation.type === "attributes") {
        attachKnown(mutation.target);
      } else {
        mutation.addedNodes.forEach(attachKnown);
      }
    }
    alignNow();
    scheduleScan();
  });

  // 已經翻好的圖：新的元素（閱讀器捲動時拿掉又重建）或網址換了，當下就蓋上譯文，不等延遲掃描。
  // MutationObserver 在畫面更新之前執行，所以一個畫格的原文都不會露出來
  function attachKnown(node) {
    if (!(node instanceof Element) || node.closest("tmw-overlay, tmw-panel")) {
      return;
    }
    const images = node instanceof HTMLImageElement ? [node] : node.querySelectorAll("img");
    for (const element of images) {
      const source = sourceOf(element);
      const result = doneBySource.get(source);
      if (!result) {
        continue; // 還沒翻：交給一般的掃描
      }
      const page = pages.get(element);
      if (page?.source === source && page.anchor) {
        continue;
      }
      if (page) {
        removeOverlay(page);
      }
      const entry = sources.get(source) || track(source);
      entry.element = element;
      const fresh = { source, result };
      pages.set(element, fresh);
      showOverlay(element, fresh);
    }
  }
  observer.observe(document.documentElement, {
    childList: true,
    subtree: true,
    attributes: true,
    attributeFilter: ["src", "srcset", ...LAZY_ATTRIBUTES],
  });
  // 捲動：整個視窗捲動時譯文本來就跟著動（align 量到沒變就不寫）；
  // 圖片在另一個捲動容器裡、而譯文的定位基準在容器外面時，才需要重新對齊
  const onScroll = () => {
    alignAll();
    scheduleScan();
    pump();
  };
  document.addEventListener("load", scheduleScan, true); // 圖片載入完成
  addEventListener("scroll", onScroll, { capture: true, passive: true });
  addEventListener("resize", alignAll, { passive: true });
  const resized = new ResizeObserver(alignAll);
  const timers = [];
  timers.push(setInterval(() => {
    for (const element of pages.keys()) {
      resized.observe(element); // 重複 observe 同一個元素不會有事
    }
    alignNow(); // 版面變動（廣告插進來、閱讀器切換單雙頁）不一定有事件；被網頁拿掉的譯文也在這裡補回去
  }, 1000));

  // 單頁應用換章：網址變了就把舊的清掉（翻好的結果還記著，回上一章時直接用）
  let lastUrl = location.href;
  timers.push(setInterval(() => {
    if (location.href !== lastUrl) {
      lastUrl = location.href;
      for (const page of pages.values()) {
        removeOverlay(page);
      }
      pages.clear();
      sources = new Map(); // 新的一章重新計數（翻好的還記在 doneBySource）
      queue = [];
      scheduleScan();
    }
  }, 500));

  function toggle() {
    visible = !visible;
    for (const page of pages.values()) {
      page.layer?.classList.toggle("hidden", !visible);
    }
    updateBadge();
  }

  // ---------------------------------------------------------------- 載入整頁

  // 主要內容欄所在的捲動容器（閱讀器常把漫畫放在自己的捲動區塊裡），沒有就是整個視窗
  function scrollerOf(element) {
    for (let node = element?.parentElement; node && node !== document.body; node = node.parentElement) {
      const style = getComputedStyle(node);
      if (/(auto|scroll)/.test(style.overflowY) && node.scrollHeight > node.clientHeight + 10) {
        return node;
      }
    }
    return document.scrollingElement || document.documentElement;
  }

  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  let preloading = false;

  // 有些網站要圖片真的捲進畫面才載入（而且網址不放在屬性裡）：自動捲到底一次再捲回來。
  // 已經在屬性裡的網址 scan 本來就讀得到；這裡處理剩下的。最多 30 秒、到底且高度不再變就停
  async function preloadAll() {
    if (preloading || fatal === "stopped") {
      return;
    }
    preloading = true;
    updateBadge();
    for (const img of document.images) {
      if (img.loading === "lazy") {
        img.loading = "eager"; // 瀏覽器內建的延遲載入：直接改成立刻載入
      }
    }
    const first = [...pages.keys()][0] || [...document.images].find((img) => img.getBoundingClientRect().width >= MIN_WIDTH);
    const scroller = scrollerOf(first);
    const startTop = scroller.scrollTop;
    const startUrl = location.href;
    const deadline = Date.now() + 30_000;
    let lastHeight = -1;
    let stable = 0;
    while (Date.now() < deadline && fatal !== "stopped" && location.href === startUrl) {
      const step = Math.max(200, scroller.clientHeight * 0.9);
      if (scroller.scrollTop + scroller.clientHeight >= scroller.scrollHeight - 4) {
        // 到底了：等一下看會不會再長出新的內容（無限捲動）
        stable = scroller.scrollHeight === lastHeight ? stable + 1 : 0;
        lastHeight = scroller.scrollHeight;
        if (stable >= 2) {
          break;
        }
        await sleep(500);
        continue;
      }
      scroller.scrollTo({ top: scroller.scrollTop + step, behavior: "instant" });
      await sleep(180);
      scan();
    }
    if (location.href === startUrl) {
      scroller.scrollTo({ top: startTop, behavior: "instant" });
    }
    preloading = false;
    scan();
    updateBadge();
  }

  // 失敗的圖重新排隊（例如主程式剛剛沒開）
  function retry() {
    fatal = "";
    for (const [source, entry] of sources) {
      if (entry.state === "failed") {
        entry.state = "queued";
        entry.reason = "";
        queue.push(source);
      }
    }
    pump();
    updateBadge();
  }

  // 停止：拿掉所有譯文和監聽，這一頁回到原樣（再按「翻譯這一頁」重新開始）
  function stop() {
    observer.disconnect();
    resized.disconnect();
    timers.forEach(clearInterval);
    clearTimeout(scanTimer);
    document.removeEventListener("load", scheduleScan, true);
    removeEventListener("scroll", onScroll, { capture: true });
    removeEventListener("resize", alignAll);
    chrome.runtime.onMessage.removeListener(onMessage);
    for (const [element, page] of pages) {
      removeOverlay(page);
      if (page.anchorName) {
        element.style.removeProperty("anchor-name");
      }
    }
    pages.clear();
    sources = new Map();
    queue = [];
    fatal = "stopped"; // 還在路上的請求回來時不再處理
    panelHost.remove();
    chrome.runtime.sendMessage({ kind: "stopped" }).catch(() => {}); // 換頁後不再自動開啟
    delete window.__tmwWebManga;
  }

  function status() {
    const { total, done, failed, waiting, reasons } = counts();
    return {
      active: true,
      visible,
      total,
      done,
      failed,
      waiting,
      reasons,
      notice: lastNotice,
      fatal: fatal && fatal !== "stopped" ? fatal : "",
      preloading,
      panelHidden: panelState.hidden,
    };
  }

  function onMessage(request, sender, sendResponse) {
    const actions = {
      toggle,
      retry,
      stop,
      preload: preloadAll,
      "show-panel": () => {
        panelState.hidden = false;
        applyPanel();
      },
    };
    if (request?.kind === "page-status") {
      sendResponse(status());
    } else if (actions[request?.kind]) {
      actions[request.kind]();
      sendResponse(status());
    }
    return false;
  }
  chrome.runtime.onMessage.addListener(onMessage);

  $panel("toggle").addEventListener("click", toggle);
  $panel("retry").addEventListener("click", retry);
  $panel("preload").addEventListener("click", preloadAll);
  $panel("stop").addEventListener("click", stop);
  $panel("engine").addEventListener("change", chooseEngine);
  loadEngines();

  window.__tmwWebManga = { toggle, retry, stop, status, preload: preloadAll };
  scan();
  // 面板的設定（位置、透明度、要不要先載入整頁）讀回來之後才決定要不要自動載入整頁
  (chrome.storage?.local.get({ panel: {} }) ?? Promise.resolve({ panel: {} }))
    .then(({ panel: saved }) => {
      Object.assign(panelState, saved, { hidden: false });
      applyPanel();
      updateBadge();
      if (panelState.autoPreload) {
        preloadAll();
      }
    })
    .catch(() => {});
})();
