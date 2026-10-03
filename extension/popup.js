// 控制視窗：工具列按鈕按下去跳出來的面板。
// 只負責顯示狀態、把按鈕轉成指令；實際的工作在 background.js（連主程式、抓圖）和 content.js（找圖、畫譯文）。

const $ = (id) => document.getElementById(id);
let tab = null;
let origin = "";

function setApp(state) {
  const text = {
    connected: ["ok", "已連上 Translation Magic Window"],
    "app-unavailable": ["bad", "主程式沒有回應：請確認它已經開啟"],
    "host-not-installed": ["bad", "找不到主程式：請先安裝 Translation Magic Window"],
    checking: ["", "檢查主程式…"],
  }[state] || ["warn", `主程式：${state}`];
  $("appDot").className = `dot ${text[0]}`;
  $("appText").textContent = text[1];
}

async function pageStatus() {
  try {
    return await chrome.tabs.sendMessage(tab.id, { kind: "page-status" });
  } catch {
    return null; // 這一頁還沒注入 content.js（或是瀏覽器自己的頁面，不能注入）
  }
}

function showPage(status) {
  const active = Boolean(status?.active);
  $("start").classList.toggle("hidden", active);
  $("toggle").disabled = !active;
  $("stop").disabled = !active;
  $("retry").disabled = !active || !status.failed;
  $("toggle").textContent = status?.visible === false ? "顯示譯文" : "顯示原文";
  if (!active) {
    $("pageTitle").textContent = "這一頁還沒開始";
    $("pageDetail").textContent = "";
    $("progress").style.width = "0";
    return;
  }
  const finished = status.done + status.failed;
  if (status.fatal) {
    $("pageTitle").textContent = "連不上主程式";
  } else if (status.total === 0) {
    $("pageTitle").textContent = "這一頁沒有找到漫畫圖片";
  } else if (finished < status.total) {
    $("pageTitle").textContent = `翻譯中 ${status.done} / ${status.total}`;
  } else {
    $("pageTitle").textContent = `完成 ${status.done} 張`;
  }
  $("pageDetail").textContent = status.failed ? `${status.failed} 張失敗` : "";
  $("progress").style.width = status.total ? `${(finished / status.total) * 100}%` : "0";
}

async function refresh() {
  showPage(await pageStatus());
}

async function hasImagePermission() {
  return chrome.permissions.contains({ origins: ["<all_urls>"] });
}

async function showPermission() {
  const granted = await hasImagePermission();
  $("permission").classList.toggle("hidden", granted);
  $("hint").textContent = granted
    ? ""
    : "還沒允許讀取圖片：只能翻和網頁同一個網站、或網頁本身讀得到的圖。";
}

async function init() {
  [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  try {
    origin = new URL(tab.url).origin;
  } catch {
    origin = "";
  }
  const injectable = /^https?:/.test(tab?.url || "");
  if (!injectable) {
    $("start").disabled = true;
    $("auto").disabled = true;
    $("hint").textContent = "瀏覽器自己的頁面不能翻譯。";
  }
  $("autoText").textContent = origin && injectable ? `${new URL(tab.url).host} 自動翻譯` : "這個網站自動翻譯";

  const settings = await chrome.storage.local.get({ language: "auto", autoSites: [] });
  $("language").value = settings.language;
  $("auto").checked = settings.autoSites.includes(origin);

  setApp("checking");
  chrome.runtime.sendMessage({ kind: "app-status" }).then((reply) => setApp(reply?.state || "unknown"));
  if (injectable) {
    await showPermission();
  }
  await refresh();
  setInterval(refresh, 500);
}

$("start").addEventListener("click", async () => {
  // 第一次：問讀取圖片的權限（在面板裡按按鈕算是使用者的操作，瀏覽器才肯問）
  if (!(await hasImagePermission())) {
    await chrome.permissions.request({ origins: ["<all_urls>"] }).catch(() => false);
    await showPermission();
  }
  await chrome.runtime.sendMessage({ kind: "start", tabId: tab.id });
  await refresh();
});

$("toggle").addEventListener("click", async () => {
  await chrome.tabs.sendMessage(tab.id, { kind: "toggle" }).catch(() => {});
  await refresh();
});

$("retry").addEventListener("click", async () => {
  await chrome.tabs.sendMessage(tab.id, { kind: "retry" }).catch(() => {});
  await refresh();
});

$("stop").addEventListener("click", async () => {
  await chrome.tabs.sendMessage(tab.id, { kind: "stop" }).catch(() => {});
  await refresh();
});

$("permission").addEventListener("click", async () => {
  await chrome.permissions.request({ origins: ["<all_urls>"] }).catch(() => false);
  await showPermission();
});

$("language").addEventListener("change", async () => {
  await chrome.storage.local.set({ language: $("language").value });
});

$("auto").addEventListener("change", async () => {
  if (!origin) {
    return;
  }
  // 自動翻譯要在網頁載入時注入，需要讀取那個網站的權限
  if ($("auto").checked && !(await hasImagePermission())) {
    const granted = await chrome.permissions.request({ origins: ["<all_urls>"] }).catch(() => false);
    if (!granted) {
      $("auto").checked = false;
      return;
    }
    await showPermission();
  }
  await chrome.runtime.sendMessage({ kind: "auto-site", origin, enabled: $("auto").checked });
  if ($("auto").checked && !(await pageStatus())?.active) {
    await chrome.runtime.sendMessage({ kind: "start", tabId: tab.id });
  }
});

init();
