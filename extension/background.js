// Translation Magic Window：網頁漫畫（背景 service worker）
//
// - 工具列按鈕：第一次在這個分頁按 → 注入 content.js 開始翻譯；之後再按 → 切換原文／譯文（content.js 自己判斷）
// - 和電腦上的主程式溝通：chrome.runtime.connectNative → tmw_web_host.exe → 主程式（core/web_protocol.h）
// - 幫 content.js 抓圖：擴充功能有權限時不受網頁的跨網域限制；解碼、縮小、轉成 RGBA 再送給主程式
// - 同一張圖（內容的雜湊相同）只翻一次：結果記在記憶體裡

const HOST = "io.github.unname0925.tmw";
// 送給主程式的圖最多幾個像素：再大就等比例縮小（OCR 不需要更高的解析度，訊息也比較小）
const MAX_PIXELS = 4_000_000;

let port = null;
const waiting = new Map(); // id → [resolve…]：等主程式回覆
const results = new Map(); // id → 回覆（這次瀏覽器執行期間的快取）

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
    failAll(reason.includes("not found") ? "host-not-installed" : reason);
  });
  port.postMessage({ type: "hello", protocol: 1 });
  return port;
}

function onHostMessage(message) {
  if (message.type === "hello") {
    return;
  }
  if (message.type === "error" && !message.id) {
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
  const id = await sha256(`${image.width}x${image.height}:${image.pixels}`);
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
        language: "auto",
      });
    } catch (error) {
      port = null;
      failAll(String(error));
    }
  });
  return { ...reply, width: image.width, height: image.height };
}

chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  if (request?.kind !== "translate" || !sender.tab) {
    return false;
  }
  translate(request)
    .then(sendResponse)
    .catch((error) => sendResponse({ type: "error", message: String(error?.message || error) }));
  return true; // 非同步回覆
});

chrome.action.onClicked.addListener(async (tab) => {
  // 讀跨網域的圖片要「所有網站」的權限：第一次按的時候才問（使用者可以拒絕，
  // 那就只能翻同網域、或網頁本身讀得到像素的圖）
  try {
    await chrome.permissions.request({ origins: ["<all_urls>"] });
  } catch {
    // 已經有權限、或使用者拒絕：照樣繼續
  }
  await chrome.scripting.executeScript({ target: { tabId: tab.id }, files: ["content.js"] });
});
