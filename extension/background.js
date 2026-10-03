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
const results = new Map(); // id → 回覆（這次瀏覽器執行期間的快取）
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
  if (message.type === "error" && !message.id) {
    setAppState(message.message);
    failAll(message.message); // 整條連線不能用（例如主程式叫不起來）
    return;
  }
  if (message.type === "result") {
    results.set(message.id, message);
  }
  const resolvers = waiting.get(message.id) || [];
  waiting.delete(message.id);
  for (const resolve of resolvers) {
    resolve(message);
  }
}

function failAll(reason) {
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

async function fetchPixels(url) {
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

// content.js 的請求：{url} 由這裡抓，或 {image:{width,height,pixels}} 是頁面裡已經讀好的
async function translate(request) {
  let image = request.image;
  if (!image) {
    try {
      image = await fetchPixels(request.url);
    } catch (error) {
      // 抓不到（沒有權限、防盜連、格式不支援）：content.js 改在頁面裡讀
      return { type: "error", fetchFailed: true, message: String(error?.message || error) };
    }
  }
  const { language } = await chrome.storage.local.get({ language: "auto" });
  // 同一張圖用不同的辨識語言是不同的結果
  const id = await sha256(`${language}:${image.width}x${image.height}:${image.pixels}`);
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

async function start(tabId) {
  await chrome.scripting.executeScript({ target: { tabId }, files: ["content.js"] });
}

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
  } else if (request?.kind === "app-status") {
    work = currentAppState().then((state) => ({ state }));
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
