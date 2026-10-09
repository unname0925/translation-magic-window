// Translation Magic Window：網頁漫畫（背景 service worker）
//
// - 工具列按鈕打開控制視窗（popup.html）；開始翻譯時由這裡注入 content.js
// - 「這個網站自動翻譯」：用 chrome.scripting.registerContentScripts 在那些網站載入時自動注入
// - 和電腦上的主程式溝通：chrome.runtime.connectNative → tmw_web_host.exe → 主程式（core/web_protocol.h）
// - 幫 content.js 抓圖：擴充功能有權限時不受網頁的跨網域限制；解碼、縮小、轉成 RGBA 再送給主程式
// - 同一張圖（內容的雜湊相同）只翻一次：結果記在記憶體裡

const HOST = "io.github.unname0925.tmw";
// 送給主程式的圖最多幾個像素：再大就等比例縮小（OCR 不需要更高的解析度，訊息也比較小）
const MAX_PIXELS = 4_000_000;

let port = null;
const waiting = new Map(); // id → [resolve…]：等主程式回覆
const results = new Map(); // id → 成功的回覆（這次瀏覽器執行期間的快取；失敗的不記，重試才會真的重送）
const engineWaiters = []; // 等主程式回「用過的翻譯引擎」
// 主程式的狀態（控制視窗顯示）："unknown" | "connected" | "host-not-installed" | "app-unavailable" | 其他錯誤
let appState = "unknown";
const stateWaiters = [];

function setAppState(state) {
  appState = state;
  for (const resolve of stateWaiters.splice(0)) {
    resolve(state);
  }
}

function connect() {
  if (port) {
    return port;
  }
  port = chrome.runtime.connectNative(HOST);
  port.onMessage.addListener(onHostMessage);
  port.onDisconnect.addListener(() => {
    // 主機沒註冊、主程式沒裝或結束了
    const reason = chrome.runtime.lastError?.message || "disconnected";
    port = null;
    const state = reason.includes("not found") ? "host-not-installed" : appState === "connected" ? "unknown" : "app-unavailable";
    setAppState(state);
    failAll(state === "unknown" ? reason : state);
  });
  port.postMessage({ type: "hello", protocol: 1 });
  return port;
}

function onHostMessage(message) {
  if (message.type === "hello") {
    setAppState("connected");
    return;
  }
  if (message.type === "engines" || (message.type === "error" && message.message === "no-such-engine")) {
    for (const resolve of engineWaiters.splice(0)) {
      resolve(message);
    }
    return;
  }
  if (message.type === "error" && !message.id) {
    setAppState(message.message);
    failAll(message.message); // 整條連線不能用（例如主程式叫不起來）
    return;
  }
  if (message.type === "result" && !message.error) {
    results.set(message.id, message);
  }
  const resolvers = waiting.get(message.id) || [];
  waiting.delete(message.id);
  for (const resolve of resolvers) {
    resolve(message);
  }
}

function failAll(reason) {
  for (const resolve of engineWaiters.splice(0)) {
    resolve({ type: "error", message: reason });
  }
  for (const [id, resolvers] of waiting) {
    for (const resolve of resolvers) {
      resolve({ type: "error", id, message: reason });
    }
  }
  waiting.clear();
}

function base64(bytes) {
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

async function sha256(text) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// 圖片 → 送給主程式的 RGBA（太大的先縮小）
function pixelsOf(bitmap) {
  const scale = Math.min(1, Math.sqrt(MAX_PIXELS / (bitmap.width * bitmap.height)));
  const width = Math.max(1, Math.round(bitmap.width * scale));
  const height = Math.max(1, Math.round(bitmap.height * scale));
  const canvas = new OffscreenCanvas(width, height);
  const context = canvas.getContext("2d", { willReadFrequently: true });
  context.drawImage(bitmap, 0, 0, width, height);
  const data = context.getImageData(0, 0, width, height).data;
  return { width, height, pixels: base64(data) };
}

// 很多漫畫網站的圖片伺服器有防盜連：只給帶著自己網站「來源頁」（Referer）的請求。
// 擴充功能的 service worker 不能自己指定別的網站當來源，所以用 declarativeNetRequest
// 只對「沒有分頁的請求」（擴充功能自己發的）、這個圖片主機，補上漫畫頁的網址。
// 每個主機一條規則，換頁時換掉網址
const refererRules = new Map(); // 主機 → 規則編號
let nextRuleId = 1;

async function setReferer(url, page) {
  if (!page || !chrome.declarativeNetRequest) {
    return;
  }
  const host = new URL(url).hostname;
  const id = refererRules.get(host) ?? nextRuleId++;
  refererRules.set(host, id);
  await chrome.declarativeNetRequest.updateSessionRules({
    removeRuleIds: [id],
    addRules: [{
      id,
      priority: 1,
      action: { type: "modifyHeaders", requestHeaders: [{ header: "referer", operation: "set", value: page }] },
      condition: { requestDomains: [host], tabIds: [chrome.tabs.TAB_ID_NONE], resourceTypes: ["xmlhttprequest", "other"] },
    }],
  });
}

async function fetchPixels(url, page) {
  try {
    await setReferer(url, page);
  } catch {
    // 沒有權限加規則：照樣試，抓不到時 content.js 改在頁面裡讀
  }
  const response = await fetch(url, { credentials: "include" });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  const bitmap = await createImageBitmap(await response.blob());
  try {
    return pixelsOf(bitmap);
  } finally {
    bitmap.close();
  }
}

// 讀不到像素的圖（被跨網域保護的 canvas 之類）：截下分頁目前的畫面，裁出那張圖。
// 瀏覽器限制每秒最多截兩次，所以排隊、每次至少隔 600 毫秒。截好的像素先放著，
// content.js 把面板、譯文恢復顯示之後再用 token 要求翻譯
const captures = new Map(); // token → { width, height, pixels }
let captureToken = 0;
let captureChain = Promise.resolve();
let lastCapture = 0;

function capture(request, sender) {
  const run = async () => {
    if (!sender.tab?.active) {
      return { type: "error", message: "tab-hidden" }; // 使用者切到別的分頁了：截到的不是這一頁
    }
    const wait = lastCapture + 600 - Date.now();
    if (wait > 0) {
      await new Promise((resolve) => setTimeout(resolve, wait));
    }
    lastCapture = Date.now();
    const dataUrl = await chrome.tabs.captureVisibleTab(sender.tab.windowId, { format: "png" });
    const shot = await createImageBitmap(await (await fetch(dataUrl)).blob());
    try {
      const scale = request.scale || 1;
      const left = Math.max(0, Math.round(request.rect.x * scale));
      const top = Math.max(0, Math.round(request.rect.y * scale));
      const width = Math.min(shot.width - left, Math.round(request.rect.width * scale));
      const height = Math.min(shot.height - top, Math.round(request.rect.height * scale));
      if (width <= 0 || height <= 0) {
        return { type: "error", message: "capture-empty" };
      }
      const crop = await createImageBitmap(shot, left, top, width, height);
      try {
        const token = ++captureToken;
        captures.set(token, pixelsOf(crop));
        setTimeout(() => captures.delete(token), 60_000); // 沒來拿就丟掉
        return { type: "captured", token };
      } finally {
        crop.close();
      }
    } finally {
      shot.close();
    }
  };
  const result = captureChain.then(run, run);
  captureChain = result.catch(() => {});
  return result;
}

// 頁面送來的壓縮檔（data: 網址）：在這裡解碼，不佔網頁的主執行緒
async function decodePixels(encoded) {
  const bitmap = await createImageBitmap(await (await fetch(encoded)).blob());
  try {
    return pixelsOf(bitmap);
  } finally {
    bitmap.close();
  }
}

// content.js 的請求：{url} 由這裡抓，{encoded} 是頁面裡拿到的壓縮檔，
// 或 {image:{width,height,pixels}} 是頁面裡已經讀好的
async function translate(request) {
  let image = request.image;
  if (request.captured) {
    image = captures.get(request.captured);
    captures.delete(request.captured);
    if (!image) {
      return { type: "error", message: "capture-expired" };
    }
  }
  if (!image) {
    try {
      image = request.encoded ? await decodePixels(request.encoded) : await fetchPixels(request.url, request.page);
    } catch (error) {
      // 抓不到（沒有權限、防盜連、格式不支援）：content.js 改在頁面裡讀
      return { type: "error", fetchFailed: true, message: String(error?.message || error) };
    }
  }
  const { language } = await chrome.storage.local.get({ language: "auto" });
  const soundEffects = request.soundEffects !== false;
  // 同一張圖用不同的辨識語言、擬聲字翻不翻，是不同的結果
  const id = await sha256(`${language}:${soundEffects ? "" : "no-sfx:"}${image.width}x${image.height}:${image.pixels}`);
  const cached = results.get(id);
  if (cached) {
    return { ...cached, width: image.width, height: image.height };
  }
  const reply = await new Promise((resolve) => {
    const list = waiting.get(id);
    if (list) {
      list.push(resolve); // 同一張圖已經在路上了
      return;
    }
    waiting.set(id, [resolve]);
    try {
      connect().postMessage({
        type: "translate",
        id,
        width: image.width,
        height: image.height,
        pixels: image.pixels,
        language,
        soundEffects,
        site: request.site || "", // 主程式的名詞記憶依網站分開
        order: Number.isInteger(request.order) ? request.order : -1, // 上下文照頁序
        chapter: request.chapter || "",
      });
    } catch (error) {
      port = null;
      failAll(String(error));
    }
  });
  return { ...reply, width: image.width, height: image.height };
}

// 控制視窗問主程式的狀態：還沒連過就連一次，最多等 5 秒
async function currentAppState() {
  if (appState === "connected" && port) {
    return appState;
  }
  const answer = new Promise((resolve) => stateWaiters.push(resolve));
  try {
    connect();
  } catch (error) {
    setAppState(String(error?.message || error));
  }
  return Promise.race([answer, new Promise((resolve) => setTimeout(() => resolve("app-unavailable"), 5000))]);
}

// 用過的翻譯引擎（控制面板的選單）；set 是要改用的第幾個（不給就只是查詢）
function engines(set) {
  return new Promise((resolve) => {
    engineWaiters.push(resolve);
    setTimeout(() => resolve({ type: "error", message: "app-unavailable" }), 10_000);
    try {
      connect().postMessage(set === undefined ? { type: "engines" } : { type: "set-engine", index: set });
    } catch (error) {
      port = null;
      failAll(String(error));
    }
  });
}

// 用過「翻譯這一頁」的分頁：換到別的網頁時自動再開（控制面板一直在），直到按「停止」或關掉瀏覽器。
// 記在 storage.session：瀏覽器關掉就清空
const ACTIVE_TABS = "activeTabs";

async function activeTabs() {
  return (await chrome.storage.session.get({ [ACTIVE_TABS]: [] }))[ACTIVE_TABS];
}

// 每個翻譯中的分頁是在哪個網站開始的：換頁後還在同一個網站才繼續（換到別的網站不自動翻）
const ACTIVE_ORIGINS = "activeOrigins";

function originOf(url) {
  try {
    return new URL(url).origin;
  } catch {
    return "";
  }
}

async function setActive(tabId, active, origin = "") {
  const tabs = (await activeTabs()).filter((id) => id !== tabId);
  const { [ACTIVE_ORIGINS]: origins = {} } = await chrome.storage.session.get({ [ACTIVE_ORIGINS]: {} });
  delete origins[tabId];
  if (active) {
    tabs.push(tabId);
    origins[tabId] = origin;
  }
  await chrome.storage.session.set({ [ACTIVE_TABS]: tabs, [ACTIVE_ORIGINS]: origins });
}

async function start(tabId) {
  await chrome.scripting.executeScript({ target: { tabId }, files: ["content.js"] });
  const tab = await chrome.tabs.get(tabId).catch(() => null);
  await setActive(tabId, true, originOf(tab?.url || ""));
}

chrome.tabs.onUpdated.addListener((tabId, info, tab) => {
  if (info.status !== "complete") {
    return;
  }
  (async () => {
    if (!(await activeTabs()).includes(tabId)) {
      return;
    }
    const { [ACTIVE_ORIGINS]: origins = {} } = await chrome.storage.session.get({ [ACTIVE_ORIGINS]: {} });
    const started = origins[tabId];
    if (started && started !== originOf(tab?.url || "")) {
      await setActive(tabId, false); // 換到別的網站：不自動翻，要翻再按一次
      return;
    }
    // 沒有這個網站的權限（使用者拒絕了「讀取所有網站」）時注入不了：就算了
    await chrome.scripting.executeScript({ target: { tabId }, files: ["content.js"] }).catch(() => {});
  })();
});
chrome.tabs.onRemoved.addListener((tabId) => {
  setActive(tabId, false);
});

// 「這個網站自動翻譯」的網站：網頁載入完成時自動注入 content.js
const AUTO_SCRIPT = "tmw-auto";

async function registerAutoSites() {
  const { autoSites } = await chrome.storage.local.get({ autoSites: [] });
  const existing = await chrome.scripting.getRegisteredContentScripts({ ids: [AUTO_SCRIPT] });
  if (existing.length > 0) {
    await chrome.scripting.unregisterContentScripts({ ids: [AUTO_SCRIPT] });
  }
  if (autoSites.length === 0 || !(await chrome.permissions.contains({ origins: ["<all_urls>"] }))) {
    return;
  }
  await chrome.scripting.registerContentScripts([{
    id: AUTO_SCRIPT,
    matches: autoSites.map((origin) => `${origin}/*`),
    js: ["content.js"],
    runAt: "document_idle",
  }]);
}

async function setAutoSite(origin, enabled) {
  const { autoSites } = await chrome.storage.local.get({ autoSites: [] });
  const next = autoSites.filter((site) => site !== origin);
  if (enabled) {
    next.push(origin);
  }
  await chrome.storage.local.set({ autoSites: next });
  await registerAutoSites();
}

chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  let work = null;
  if (request?.kind === "translate" && sender.tab) {
    work = translate(request);
  } else if (request?.kind === "capture" && sender.tab) {
    work = capture(request, sender);
  } else if (request?.kind === "app-status") {
    work = currentAppState().then((state) => ({ state }));
  } else if (request?.kind === "engines") {
    work = engines();
  } else if (request?.kind === "set-engine") {
    work = engines(request.index);
  } else if (request?.kind === "stopped" && sender.tab) {
    work = setActive(sender.tab.id, false).then(() => ({ ok: true }));
  } else if (request?.kind === "start") {
    work = start(request.tabId).then(() => ({ ok: true }));
  } else if (request?.kind === "auto-site") {
    work = setAutoSite(request.origin, request.enabled).then(() => ({ ok: true }));
  }
  if (!work) {
    return false;
  }
  work.then(sendResponse).catch((error) => sendResponse({ type: "error", message: String(error?.message || error) }));
  return true; // 非同步回覆
});

// 權限被拿掉時，自動翻譯也注入不了：重新登記（沒有權限就不登記）
chrome.permissions.onRemoved.addListener(registerAutoSites);
chrome.permissions.onAdded.addListener(registerAutoSites);
chrome.runtime.onInstalled.addListener(registerAutoSites);
