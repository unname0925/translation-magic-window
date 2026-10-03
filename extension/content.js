// Translation Magic Window：網頁漫畫（注入到分頁裡的腳本）
//
// - 找出頁面上的漫畫圖（<img>、畫好的 <canvas>），只挑「主要內容欄」：最大的一群同寬的圖，
//   旁邊的廣告、縮圖、按鈕不翻（docs/proposal-speed-and-web-manga.md 第二部分第 9 點）
// - 延遲載入的圖不等它載入：直接讀 data-src 這類屬性裡的真正網址先翻，捲到之前就好了
// - 依離畫面多近決定順序，每次送出前重新排；捲動、換章時新出現的圖自動補上
// - 譯文放在緊跟著圖片的一個元素裡（和圖片在同一個捲動容器，捲動時由瀏覽器一起移動，不會落後），
//   內容在 shadow DOM 裡，網頁的 CSS 碰不到；不改原本的圖
// - 譯文依圖片網址記住：閱讀器把捲出畫面的圖拿掉、捲回來再建一個新的，直接蓋回去
// - 控制視窗（popup.js）用訊息問狀態、切換原文／譯文、重試、停止
(() => {
  if (window.__tmwWebManga) {
    return; // 已經在這一頁執行了（例如自動翻譯之後又按了「翻譯這一頁」）
  }

  const MIN_WIDTH = 200; // 顯示寬度比這小的不是漫畫頁
  const MIN_NATURAL = 300; // 原圖比這小的不是漫畫頁（也排除延遲載入的佔位圖）
  const IN_FLIGHT = 3; // 同時交給背景的張數：主程式一次處理一張，其餘先抓圖、解碼
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

  // 狀態列（右下角）
  const badgeHost = document.createElement("tmw-status");
  badgeHost.style.cssText =
    "all:initial;position:fixed;right:12px;bottom:12px;z-index:2147483647;pointer-events:none;";
  const badgeRoot = badgeHost.attachShadow({ mode: "closed" });
  badgeRoot.innerHTML = `<style>
    .badge { padding: 6px 10px; border-radius: 6px; background: rgba(32, 32, 32, 0.85); color: #fff;
             font: 13px "Microsoft JhengHei", sans-serif; }
    .hidden { display: none; }</style><div class="badge"></div>`;
  document.documentElement.appendChild(badgeHost);
  const badge = badgeRoot.querySelector(".badge");

  const pages = new Map(); // 元素 → { source, state, result, anchor, layer }
  const resultsBySource = new Map(); // 圖片網址 → 翻好的結果（元素被拿掉又建回來時直接用）
  let queue = [];
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
      return "canvas";
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

  function scan() {
    const elements = [...document.images, ...document.querySelectorAll("canvas")].filter(isCandidate);
    for (const element of mainColumn(elements)) {
      const source = sourceOf(element);
      const page = pages.get(element);
      if (page && page.source === source) {
        continue;
      }
      if (page) {
        removeOverlay(page); // 圖換了（換章、延遲載入換成另一張）
      }
      const known = resultsBySource.get(source);
      if (known) {
        // 翻過了（閱讀器把這張拿掉又建回來）：直接蓋回去，不用排隊
        const entry = { source, state: "done", result: known };
        pages.set(element, entry);
        showOverlay(element, entry);
        continue;
      }
      pages.set(element, { source, state: "queued" });
      queue.push(element);
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

  async function request(element, source) {
    if (isHttp(source)) {
      const reply = await chrome.runtime.sendMessage({ kind: "translate", url: source });
      if (!reply?.fetchFailed) {
        return reply; // 成功，或是和抓圖無關的錯誤（例如主程式沒開）
      }
      if (element instanceof HTMLImageElement && !loaded(element)) {
        return reply; // 還沒載入、背景也抓不到：等它載入了再從頁面讀（scan 會再排一次）
      }
    }
    return chrome.runtime.sendMessage({ kind: "translate", image: readInPage(element) });
  }

  async function process(element) {
    const page = pages.get(element);
    const source = page.source;
    page.state = "working";
    updateBadge();
    let reply;
    try {
      reply = await request(element, source);
    } catch (error) {
      reply = { type: "error", message: String(error?.message || error) };
    }
    if (reply?.type === "result") {
      resultsBySource.set(source, reply); // 元素換掉了也記住，之後建回來直接用
    }
    const now = pages.get(element);
    if (now !== page) {
      return; // 處理期間元素被拿掉或圖換了
    }
    if (reply?.type !== "result") {
      if (reply?.fetchFailed && element instanceof HTMLImageElement && !loaded(element)) {
        pages.delete(element); // 等它真的載入（load 事件會再掃到）
        return;
      }
      page.state = "failed";
      if (reply?.message === "host-not-installed" || reply?.message === "app-unavailable") {
        fatal = reply.message;
      }
      return;
    }
    page.state = "done";
    page.result = reply;
    showOverlay(element, page);
  }

  function pump() {
    if (running >= IN_FLIGHT || fatal) {
      return;
    }
    // 每次都依「現在」離畫面多近重新排：使用者捲到哪就先翻哪
    queue = queue.filter((element) => element.isConnected && pages.get(element)?.state === "queued");
    queue.sort((a, b) => distanceToViewport(a) - distanceToViewport(b));
    while (running < IN_FLIGHT && queue.length > 0) {
      const element = queue.shift();
      running += 1;
      process(element).finally(() => {
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
        // 閱讀器把這張拿掉了：譯文還記在 resultsBySource，建回來時直接用
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

  let badgeTimer = 0;
  function updateBadge() {
    const states = [...pages.values()].map((page) => page.state);
    const done = states.filter((state) => state === "done").length;
    const failed = states.filter((state) => state === "failed").length;
    clearTimeout(badgeTimer);
    if (fatal === "host-not-installed") {
      badge.textContent = "找不到 Translation Magic Window（請先安裝，並在設定裡啟用瀏覽器擴充功能）";
    } else if (fatal === "app-unavailable") {
      badge.textContent = "Translation Magic Window 沒有回應，請確認它已經開啟";
    } else if (states.length === 0) {
      badge.textContent = "這一頁沒有找到漫畫圖片";
    } else if (done + failed < states.length) {
      badge.textContent = `翻譯中 ${done}/${states.length}`;
    } else {
      badge.textContent = failed > 0 ? `完成 ${done} 張，${failed} 張失敗` : `完成 ${done} 張`;
      badgeTimer = setTimeout(() => badge.classList.add("hidden"), 4000);
    }
    badge.classList.remove("hidden");
  }

  // ---------------------------------------------------------------- 開始

  // 版面變了（插進新的圖、上面的圖載入後變高）：馬上重新對齊（只有用 JS 對齊的譯文需要），再找新的圖
  // （MutationObserver 在畫面更新之前執行：在這裡對齊，連一個畫格的落後都沒有）
  const observer = new MutationObserver(() => {
    alignNow();
    scheduleScan();
  });
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
      queue = [];
      scheduleScan();
    }
  }, 500));

  function toggle() {
    visible = !visible;
    for (const page of pages.values()) {
      page.layer?.classList.toggle("hidden", !visible);
    }
  }

  // 失敗的圖重新排隊（例如主程式剛剛沒開）
  function retry() {
    fatal = "";
    for (const [element, page] of pages) {
      if (page.state === "failed") {
        page.state = "queued";
        queue.push(element);
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
    clearTimeout(badgeTimer);
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
    queue = [];
    fatal = "stopped"; // 還在路上的請求回來時不再處理
    badgeHost.remove();
    delete window.__tmwWebManga;
  }

  function status() {
    const states = [...pages.values()].map((page) => page.state);
    return {
      active: true,
      visible,
      total: states.length,
      done: states.filter((state) => state === "done").length,
      failed: states.filter((state) => state === "failed").length,
      fatal: fatal && fatal !== "stopped" ? fatal : "",
    };
  }

  function onMessage(request, sender, sendResponse) {
    const actions = { toggle, retry, stop };
    if (request?.kind === "page-status") {
      sendResponse(status());
    } else if (actions[request?.kind]) {
      actions[request.kind]();
      sendResponse(status());
    }
    return false;
  }
  chrome.runtime.onMessage.addListener(onMessage);

  window.__tmwWebManga = { toggle, retry, stop, status };
  scan();
})();
