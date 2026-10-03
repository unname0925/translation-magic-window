// Translation Magic Window：網頁漫畫（注入到分頁裡的腳本）
//
// - 找出頁面上的漫畫圖（<img>、畫好的 <canvas>），只挑「主要內容欄」：最大的一群同寬的圖，
//   旁邊的廣告、縮圖、按鈕不翻（docs/proposal-speed-and-web-manga.md 第二部分第 9 點）
// - 依離畫面多近決定順序；捲動、延遲載入、換章時新出現的圖會自動補上
// - 譯文畫在一層疊在圖上的 HTML（放在 shadow DOM 裡，網頁的 CSS 碰不到），不改原本的圖
// - 工具列按鈕再按一次：切換原文／譯文
(() => {
  if (window.__tmwWebManga) {
    window.__tmwWebManga.toggle();
    return;
  }

  const MIN_SIDE = 200; // 顯示大小比這小的不是漫畫頁
  const MIN_NATURAL = 300; // 原圖比這小的不是漫畫頁
  const IN_FLIGHT = 2; // 同時交給背景的張數（主程式一次處理一張，多送一張讓它不必等）

  const host = document.createElement("div");
  host.style.cssText =
    "position:fixed;left:0;top:0;width:0;height:0;z-index:2147483646;pointer-events:none;";
  const shadow = host.attachShadow({ mode: "closed" });
  shadow.innerHTML = `<style>
    .layer { position: fixed; container-type: size; overflow: hidden; }
    .item { position: absolute; display: flex; align-items: center; justify-content: center;
            box-sizing: border-box; overflow: hidden; }
    .patch { position: absolute; inset: 0; width: 100%; height: 100%; }
    .text { position: relative; margin: 0; padding: 0; line-height: 1.15; text-align: center;
            font-family: "Microsoft JhengHei", "Noto Sans TC", sans-serif; font-weight: 600;
            overflow-wrap: anywhere; white-space: pre-wrap; }
    .vertical { writing-mode: vertical-rl; text-orientation: mixed; }
    rt { font-size: 0.45em; }
    .badge { position: fixed; right: 12px; bottom: 12px; padding: 6px 10px; border-radius: 6px;
             background: rgba(32, 32, 32, 0.85); color: #fff; font: 13px "Microsoft JhengHei", sans-serif; }
    .hidden { display: none; }
  </style><div class="badge"></div>`;
  document.documentElement.appendChild(host);
  const badge = shadow.querySelector(".badge");

  const pages = new Map(); // 元素 → { source, state, layer, result }
  let queue = [];
  let running = 0;
  let visible = true;
  let fatal = "";

  // ---------------------------------------------------------------- 找圖

  function sourceOf(element) {
    return element instanceof HTMLImageElement ? element.currentSrc || element.src : "canvas";
  }

  function isCandidate(element) {
    if (host.contains(element)) {
      return false;
    }
    const rect = element.getBoundingClientRect();
    if (rect.width < MIN_SIDE || rect.height < MIN_SIDE) {
      return false;
    }
    if (element instanceof HTMLImageElement) {
      if (!element.complete || element.naturalWidth < MIN_NATURAL || element.naturalHeight < MIN_NATURAL) {
        return false; // 還沒載入完（延遲載入的圖載入時會再掃一次）
      }
    } else if (element.width < MIN_NATURAL || element.height < MIN_NATURAL) {
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

  // 主要內容欄：依顯示寬度分組，面積總和最大的那一組；寬度和它差不多的都算
  function mainColumn(elements) {
    const groups = new Map();
    for (const element of elements) {
      const rect = element.getBoundingClientRect();
      const key = Math.round(rect.width / 20);
      groups.set(key, (groups.get(key) || 0) + rect.width * rect.height);
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
      return -rect.bottom + 1; // 在上面（已經看過了，排後面一點）
    }
    if (rect.top > innerHeight) {
      return rect.top - innerHeight;
    }
    return 0;
  }

  function scan() {
    const elements = [...document.images, ...document.querySelectorAll("canvas")].filter(isCandidate);
    for (const element of mainColumn(elements)) {
      const page = pages.get(element);
      const source = sourceOf(element);
      if (page && page.source === source) {
        continue;
      }
      if (page) {
        page.layer?.remove(); // 圖換了（換章、延遲載入換成高解析度）
      }
      pages.set(element, { source, state: "queued" });
      queue.push(element);
    }
    queue.sort((a, b) => distanceToViewport(a) - distanceToViewport(b));
    pump();
    updateBadge();
  }

  let scanTimer = 0;
  function scheduleScan() {
    clearTimeout(scanTimer);
    scanTimer = setTimeout(scan, 300);
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

  async function request(element) {
    const source = sourceOf(element);
    if (/^https?:/.test(source)) {
      const reply = await chrome.runtime.sendMessage({ kind: "translate", url: source });
      if (!reply?.fetchFailed) {
        return reply; // 成功，或是和抓圖無關的錯誤（例如主程式沒開）
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
      reply = await request(element);
    } catch (error) {
      reply = { type: "error", message: String(error?.message || error) };
    }
    const now = pages.get(element);
    if (now !== page || sourceOf(element) !== source) {
      return; // 處理期間圖換了
    }
    if (reply?.type !== "result") {
      page.state = "failed";
      if (reply?.message === "host-not-installed" || reply?.message === "app-unavailable") {
        fatal = reply.message;
      }
      return;
    }
    page.state = "done";
    page.result = reply;
    page.layer = render(reply);
    place(element, page.layer);
  }

  function pump() {
    while (running < IN_FLIGHT && queue.length > 0 && !fatal) {
      const element = queue.shift();
      if (!element.isConnected || pages.get(element)?.state !== "queued") {
        continue;
      }
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

  function render(result) {
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
    layer.dataset.width = String(result.width);
    shadow.append(layer);
    return layer;
  }

  // 找放得進框的最大字級，換成「圖寬的百分比」（cqw）：之後圖怎麼縮放字都跟著縮放
  function fitText(layer, result) {
    const layerWidth = layer.getBoundingClientRect().width;
    if (layerWidth <= 0) {
      return;
    }
    const scale = layerWidth / result.width;
    [...layer.querySelectorAll(".item")].forEach((box, index) => {
      const item = result.items[index];
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
      text.style.fontSize = `${(low / layerWidth) * 100}cqw`;
    });
    layer.dataset.fitted = "1";
  }

  function place(element, layer) {
    if (!layer) {
      return;
    }
    const rect = element.getBoundingClientRect();
    const offscreen = rect.bottom < -innerHeight || rect.top > innerHeight * 2 || !element.isConnected;
    layer.style.display = offscreen ? "none" : "";
    layer.style.left = `${rect.left}px`;
    layer.style.top = `${rect.top}px`;
    layer.style.width = `${rect.width}px`;
    layer.style.height = `${rect.height}px`;
    if (!offscreen && !layer.dataset.fitted) {
      fitText(layer, pages.get(element).result);
    }
  }

  let placing = false;
  function placeAll() {
    if (placing) {
      return;
    }
    placing = true;
    requestAnimationFrame(() => {
      placing = false;
      for (const [element, page] of pages) {
        if (!element.isConnected) {
          page.layer?.remove();
          pages.delete(element);
          continue;
        }
        place(element, page.layer);
      }
    });
  }

  // ---------------------------------------------------------------- 狀態

  function updateBadge() {
    const states = [...pages.values()].map((page) => page.state);
    const done = states.filter((state) => state === "done").length;
    const failed = states.filter((state) => state === "failed").length;
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
      setTimeout(() => {
        if (badge.textContent.startsWith("完成")) {
          badge.classList.add("hidden");
        }
      }, 4000);
      return;
    }
    badge.classList.remove("hidden");
  }

  // ---------------------------------------------------------------- 開始

  const observer = new MutationObserver(scheduleScan);
  observer.observe(document.documentElement, {
    childList: true,
    subtree: true,
    attributes: true,
    attributeFilter: ["src", "srcset", "data-src"],
  });
  document.addEventListener("load", scheduleScan, true); // 圖片載入完成（延遲載入）
  addEventListener("scroll", () => { placeAll(); scheduleScan(); }, { capture: true, passive: true });
  addEventListener("resize", placeAll, { passive: true });
  setInterval(placeAll, 500); // 版面變動（廣告插進來、閱讀器切換單雙頁）不一定有事件

  // 單頁應用換章：網址變了就把舊的清掉
  let lastUrl = location.href;
  setInterval(() => {
    if (location.href !== lastUrl) {
      lastUrl = location.href;
      for (const page of pages.values()) {
        page.layer?.remove();
      }
      pages.clear();
      queue = [];
      scheduleScan();
    }
  }, 500);

  window.__tmwWebManga = {
    toggle() {
      visible = !visible;
      for (const page of pages.values()) {
        page.layer?.classList.toggle("hidden", !visible);
      }
      badge.textContent = visible ? "顯示譯文" : "顯示原文";
      badge.classList.remove("hidden");
      setTimeout(() => badge.classList.add("hidden"), 1500);
    },
  };
  scan();
})();
